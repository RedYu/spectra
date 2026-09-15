/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "uds_did_catalog_service.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "esp_heap_caps.h"

#include "storage_sd_service.h"

#define UDS_DID_CATALOG_SCHEMA_VERSION (1U)
#define UDS_DID_CATALOG_LIST_PAGE_SIZE (4U)

static const char *s_data_type_names[] = {
    "unsigned",
    "signed",
    "float",
    "ascii",
    "utf8",
    "bytes",
};

static const char *s_byte_order_names[] = {
    "big_endian",
    "little_endian",
};

static bool uds_did_catalog_file_name_valid(
    const char *file_name
)
{
    if (file_name == NULL) {
        return false;
    }

    const size_t length = strnlen(
        file_name,
        UDS_DID_CATALOG_FILE_NAME_MAX_LENGTH
    );

    if ((length <= 5U) ||
        (length >= UDS_DID_CATALOG_FILE_NAME_MAX_LENGTH) ||
        (strcmp(&file_name[length - 5U], ".json") != 0) ||
        (file_name[0] == '.') ||
        (strstr(file_name, "..") != NULL)) {

        return false;
    }

    for (size_t index = 0U; index < length; ++index) {
        const unsigned char character =
            (unsigned char)file_name[index];

        if (!isalnum(character) &&
            (character != '-') &&
            (character != '_') &&
            (character != '.')) {

            return false;
        }
    }

    return true;
}

static esp_err_t uds_did_catalog_build_path(
    const char *file_name,
    const char *suffix,
    char *path,
    size_t path_size
)
{
    if (!uds_did_catalog_file_name_valid(file_name) ||
        (suffix == NULL) ||
        (path == NULL) ||
        (path_size == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    const int length = snprintf(
        path,
        path_size,
        "%s/%s%s",
        UDS_DID_CATALOG_DIRECTORY,
        file_name,
        suffix
    );

    return ((length < 0) || ((size_t)length >= path_size))
        ? ESP_ERR_INVALID_SIZE
        : ESP_OK;
}

typedef struct
{
    const char *cursor;
    const char *end;

} uds_did_json_reader_t;

static void uds_did_json_skip_space(
    uds_did_json_reader_t *reader
)
{
    while ((reader->cursor < reader->end) &&
           isspace((unsigned char)*reader->cursor)) {

        reader->cursor++;
    }
}

static bool uds_did_json_consume(
    uds_did_json_reader_t *reader,
    char character
)
{
    uds_did_json_skip_space(reader);

    if ((reader->cursor >= reader->end) ||
        (*reader->cursor != character)) {

        return false;
    }

    reader->cursor++;
    return true;
}

static bool uds_did_json_hex_digit(
    char character,
    uint32_t *value
)
{
    if ((character >= '0') && (character <= '9')) {
        *value = (uint32_t)(character - '0');
        return true;
    }

    if ((character >= 'A') && (character <= 'F')) {
        *value = (uint32_t)(character - 'A' + 10);
        return true;
    }

    if ((character >= 'a') && (character <= 'f')) {
        *value = (uint32_t)(character - 'a' + 10);
        return true;
    }

    return false;
}

static bool uds_did_json_unicode(
    uds_did_json_reader_t *reader,
    uint32_t *code_point
)
{
    uint32_t value = 0U;

    for (size_t index = 0U; index < 4U; ++index) {
        uint32_t digit = 0U;

        if ((reader->cursor >= reader->end) ||
            !uds_did_json_hex_digit(*reader->cursor++, &digit)) {

            return false;
        }

        value = (value << 4U) | digit;
    }

    if ((value >= 0xD800U) && (value <= 0xDBFFU)) {
        if (((reader->end - reader->cursor) < 6) ||
            (reader->cursor[0] != '\\') ||
            (reader->cursor[1] != 'u')) {

            return false;
        }

        reader->cursor += 2;
        uint32_t low = 0U;

        for (size_t index = 0U; index < 4U; ++index) {
            uint32_t digit = 0U;

            if (!uds_did_json_hex_digit(
                    *reader->cursor++,
                    &digit
                )) {

                return false;
            }

            low = (low << 4U) | digit;
        }

        if ((low < 0xDC00U) || (low > 0xDFFFU)) {
            return false;
        }

        value = 0x10000U +
            ((value - 0xD800U) << 10U) +
            (low - 0xDC00U);
    } else if ((value >= 0xDC00U) &&
               (value <= 0xDFFFU)) {

        return false;
    }

    *code_point = value;
    return true;
}

static bool uds_did_json_append_utf8(
    uint32_t code_point,
    char *destination,
    size_t capacity,
    size_t *length
)
{
    uint8_t bytes[4];
    size_t count = 0U;

    if (code_point <= 0x7FU) {
        bytes[0] = (uint8_t)code_point;
        count = 1U;
    } else if (code_point <= 0x7FFU) {
        bytes[0] = (uint8_t)(0xC0U | (code_point >> 6U));
        bytes[1] = (uint8_t)(0x80U | (code_point & 0x3FU));
        count = 2U;
    } else if (code_point <= 0xFFFFU) {
        bytes[0] = (uint8_t)(0xE0U | (code_point >> 12U));
        bytes[1] = (uint8_t)(0x80U |
            ((code_point >> 6U) & 0x3FU));
        bytes[2] = (uint8_t)(0x80U | (code_point & 0x3FU));
        count = 3U;
    } else if (code_point <= 0x10FFFFU) {
        bytes[0] = (uint8_t)(0xF0U | (code_point >> 18U));
        bytes[1] = (uint8_t)(0x80U |
            ((code_point >> 12U) & 0x3FU));
        bytes[2] = (uint8_t)(0x80U |
            ((code_point >> 6U) & 0x3FU));
        bytes[3] = (uint8_t)(0x80U | (code_point & 0x3FU));
        count = 4U;
    } else {
        return false;
    }

    if ((*length + count) >= capacity) {
        return false;
    }

    for (size_t index = 0U; index < count; ++index) {
        destination[(*length)++] = (char)bytes[index];
    }

    return true;
}

static bool uds_did_json_string(
    uds_did_json_reader_t *reader,
    char *destination,
    size_t capacity
)
{
    if ((destination == NULL) ||
        (capacity == 0U) ||
        !uds_did_json_consume(reader, '"')) {

        return false;
    }

    size_t length = 0U;

    while (reader->cursor < reader->end) {
        unsigned char character =
            (unsigned char)*reader->cursor++;

        if (character == '"') {
            destination[length] = '\0';
            return true;
        }

        if (character < 0x20U) {
            return false;
        }

        if (character != '\\') {
            if ((length + 1U) >= capacity) {
                return false;
            }

            destination[length++] = (char)character;
            continue;
        }

        if (reader->cursor >= reader->end) {
            return false;
        }

        const char escape = *reader->cursor++;

        if (escape == 'u') {
            uint32_t code_point = 0U;

            if (!uds_did_json_unicode(reader, &code_point) ||
                (code_point == 0U) ||
                !uds_did_json_append_utf8(
                    code_point,
                    destination,
                    capacity,
                    &length
                )) {

                return false;
            }

            continue;
        }

        char decoded = '\0';

        switch (escape) {
            case '"': decoded = '"'; break;
            case '\\': decoded = '\\'; break;
            case '/': decoded = '/'; break;
            case 'b': decoded = '\b'; break;
            case 'f': decoded = '\f'; break;
            case 'n': decoded = '\n'; break;
            case 'r': decoded = '\r'; break;
            case 't': decoded = '\t'; break;
            default: return false;
        }

        if ((length + 1U) >= capacity) {
            return false;
        }

        destination[length++] = decoded;
    }

    return false;
}

static bool uds_did_json_number(
    uds_did_json_reader_t *reader,
    double *value
)
{
    uds_did_json_skip_space(reader);
    const char *start = reader->cursor;

    if ((start >= reader->end) ||
        ((*start != '-') && !isdigit((unsigned char)*start))) {

        return false;
    }

    char *end = NULL;
    *value = strtod(start, &end);

    if ((end == start) ||
        (end > reader->end) ||
        !isfinite(*value)) {

        return false;
    }

    reader->cursor = end;
    return true;
}

static bool uds_did_json_uint32(
    uds_did_json_reader_t *reader,
    uint32_t maximum,
    uint32_t *value
)
{
    double number = 0.0;

    if (!uds_did_json_number(reader, &number) ||
        (number < 0.0) ||
        (number > maximum) ||
        ((double)(uint32_t)number != number)) {

        return false;
    }

    *value = (uint32_t)number;
    return true;
}

static bool uds_did_json_enum(
    uds_did_json_reader_t *reader,
    const char *const *names,
    size_t count,
    uint32_t *value
)
{
    char name[24];

    if (!uds_did_json_string(reader, name, sizeof(name))) {
        return false;
    }

    for (size_t index = 0U; index < count; ++index) {
        if (strcmp(name, names[index]) == 0) {
            *value = (uint32_t)index;
            return true;
        }
    }

    return false;
}

static esp_err_t uds_did_catalog_document_validate(
    const uds_did_catalog_document_t *document
)
{
    if ((document == NULL) ||
        (strnlen(document->name, sizeof(document->name)) >=
         sizeof(document->name)) ||
        (document->name[0] == '\0') ||
        (strnlen(
            document->description,
            sizeof(document->description)
        ) >= sizeof(document->description)) ||
        ((document->definitions == NULL) &&
         (document->count != 0U)) ||
        (document->count > document->capacity) ||
        (document->count >
         UDS_DID_CATALOG_DEFINITION_MAX_COUNT)) {

        return ESP_ERR_INVALID_ARG;
    }

    const uds_did_catalog_t catalog = {
        .definitions = document->definitions,
        .count = document->count,
    };

    return uds_did_catalog_validate(&catalog);
}

static esp_err_t uds_did_catalog_decode_definition(
    uds_did_json_reader_t *reader,
    uds_did_definition_t *definition
)
{
    memset(definition, 0, sizeof(*definition));
    uint32_t fields = 0U;

    if (!uds_did_json_consume(reader, '{')) {
        return ESP_ERR_INVALID_ARG;
    }

    while (true) {
        uds_did_json_skip_space(reader);

        if (uds_did_json_consume(reader, '}')) {
            break;
        }

        char key[24];

        if (!uds_did_json_string(reader, key, sizeof(key)) ||
            !uds_did_json_consume(reader, ':')) {

            return ESP_ERR_INVALID_ARG;
        }

        uint32_t bit = 0U;

        if (strcmp(key, "identifier") == 0) {
            uint32_t value = 0U;
            bit = 1U << 0U;

            if (!uds_did_json_uint32(reader, UINT16_MAX, &value)) {
                return ESP_ERR_INVALID_ARG;
            }

            definition->identifier = (uint16_t)value;
        } else if (strcmp(key, "name") == 0) {
            bit = 1U << 1U;

            if (!uds_did_json_string(
                    reader,
                    definition->name,
                    sizeof(definition->name)
                )) {

                return ESP_ERR_INVALID_ARG;
            }
        } else if (strcmp(key, "unit") == 0) {
            bit = 1U << 2U;

            if (!uds_did_json_string(
                    reader,
                    definition->unit,
                    sizeof(definition->unit)
                )) {

                return ESP_ERR_INVALID_ARG;
            }
        } else if (strcmp(key, "description") == 0) {
            bit = 1U << 3U;

            if (!uds_did_json_string(
                    reader,
                    definition->description,
                    sizeof(definition->description)
                )) {

                return ESP_ERR_INVALID_ARG;
            }
        } else if (strcmp(key, "data_type") == 0) {
            uint32_t value = 0U;
            bit = 1U << 4U;

            if (!uds_did_json_enum(
                    reader,
                    s_data_type_names,
                    UDS_DID_DATA_TYPE_COUNT,
                    &value
                )) {

                return ESP_ERR_INVALID_ARG;
            }

            definition->data_type = (uds_did_data_type_t)value;
        } else if (strcmp(key, "byte_order") == 0) {
            uint32_t value = 0U;
            bit = 1U << 5U;

            if (!uds_did_json_enum(
                    reader,
                    s_byte_order_names,
                    UDS_DID_BYTE_ORDER_COUNT,
                    &value
                )) {

                return ESP_ERR_INVALID_ARG;
            }

            definition->byte_order = (uds_did_byte_order_t)value;
        } else if (strcmp(key, "data_length") == 0) {
            uint32_t value = 0U;
            bit = 1U << 6U;

            if (!uds_did_json_uint32(
                    reader,
                    UDS_DID_DATA_MAX_LENGTH,
                    &value
                )) {

                return ESP_ERR_INVALID_ARG;
            }

            definition->data_length = value;
        } else if (strcmp(key, "scale") == 0) {
            bit = 1U << 7U;

            if (!uds_did_json_number(reader, &definition->scale)) {
                return ESP_ERR_INVALID_ARG;
            }
        } else if (strcmp(key, "offset") == 0) {
            bit = 1U << 8U;

            if (!uds_did_json_number(reader, &definition->offset)) {
                return ESP_ERR_INVALID_ARG;
            }
        } else {
            return ESP_ERR_INVALID_ARG;
        }

        if ((fields & bit) != 0U) {
            return ESP_ERR_INVALID_ARG;
        }

        fields |= bit;
        uds_did_json_skip_space(reader);

        if (uds_did_json_consume(reader, '}')) {
            break;
        }

        if (!uds_did_json_consume(reader, ',')) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    if (fields != 0x1FFU) {
        return ESP_ERR_INVALID_ARG;
    }

    return uds_did_definition_validate(definition);
}

esp_err_t uds_did_catalog_service_decode_json(
    const char *json,
    uds_did_catalog_document_t *document
)
{
    if ((json == NULL) ||
        (document == NULL) ||
        ((document->definitions == NULL) &&
         (document->capacity != 0U))) {

        return ESP_ERR_INVALID_ARG;
    }

    document->name[0] = '\0';
    document->description[0] = '\0';
    document->count = 0U;

    uds_did_json_reader_t reader = {
        .cursor = json,
        .end = json + strlen(json),
    };
    uint32_t version = 0U;
    uint32_t fields = 0U;
    esp_err_t result = ESP_ERR_INVALID_ARG;

    if (!uds_did_json_consume(&reader, '{')) {
        return result;
    }

    while (true) {
        uds_did_json_skip_space(&reader);

        if (uds_did_json_consume(&reader, '}')) {
            break;
        }

        char key[24];

        if (!uds_did_json_string(&reader, key, sizeof(key)) ||
            !uds_did_json_consume(&reader, ':')) {

            goto cleanup;
        }

        uint32_t bit = 0U;

        if (strcmp(key, "version") == 0) {
            bit = 1U << 0U;

            if (!uds_did_json_uint32(
                    &reader,
                    UDS_DID_CATALOG_SCHEMA_VERSION,
                    &version
                )) {

                goto cleanup;
            }
        } else if (strcmp(key, "name") == 0) {
            bit = 1U << 1U;

            if (!uds_did_json_string(
                    &reader,
                    document->name,
                    sizeof(document->name)
                )) {

                goto cleanup;
            }
        } else if (strcmp(key, "description") == 0) {
            bit = 1U << 2U;

            if (!uds_did_json_string(
                    &reader,
                    document->description,
                    sizeof(document->description)
                )) {

                goto cleanup;
            }
        } else if (strcmp(key, "definitions") == 0) {
            bit = 1U << 3U;

            if (!uds_did_json_consume(&reader, '[')) {
                goto cleanup;
            }

            uds_did_json_skip_space(&reader);

            while (!uds_did_json_consume(&reader, ']')) {
                if ((document->count >= document->capacity) ||
                    (document->count >=
                     UDS_DID_CATALOG_DEFINITION_MAX_COUNT)) {

                    result = ESP_ERR_INVALID_SIZE;
                    goto cleanup;
                }

                result = uds_did_catalog_decode_definition(
                    &reader,
                    &document->definitions[document->count]
                );

                if (result != ESP_OK) {
                    goto cleanup;
                }

                document->count++;
                uds_did_json_skip_space(&reader);

                if (uds_did_json_consume(&reader, ']')) {
                    break;
                }

                if (!uds_did_json_consume(&reader, ',')) {
                    result = ESP_ERR_INVALID_ARG;
                    goto cleanup;
                }
            }
        } else {
            goto cleanup;
        }

        if ((fields & bit) != 0U) {
            goto cleanup;
        }

        fields |= bit;
        uds_did_json_skip_space(&reader);

        if (uds_did_json_consume(&reader, '}')) {
            break;
        }

        if (!uds_did_json_consume(&reader, ',')) {
            goto cleanup;
        }
    }

    uds_did_json_skip_space(&reader);

    if ((reader.cursor != reader.end) ||
        (fields != 0x0FU) ||
        (version != UDS_DID_CATALOG_SCHEMA_VERSION)) {

        goto cleanup;
    }

    result = uds_did_catalog_document_validate(document);

cleanup:

    if (result != ESP_OK) {
        document->count = 0U;
    }

    return result;
}

static cJSON *uds_did_catalog_encode_definition(
    const uds_did_definition_t *definition
)
{
    cJSON *object = cJSON_CreateObject();

    if ((object == NULL) ||
        (cJSON_AddNumberToObject(
            object,
            "identifier",
            definition->identifier
        ) == NULL) ||
        (cJSON_AddStringToObject(
            object,
            "name",
            definition->name
        ) == NULL) ||
        (cJSON_AddStringToObject(
            object,
            "unit",
            definition->unit
        ) == NULL) ||
        (cJSON_AddStringToObject(
            object,
            "description",
            definition->description
        ) == NULL) ||
        (cJSON_AddStringToObject(
            object,
            "data_type",
            s_data_type_names[definition->data_type]
        ) == NULL) ||
        (cJSON_AddStringToObject(
            object,
            "byte_order",
            s_byte_order_names[definition->byte_order]
        ) == NULL) ||
        (cJSON_AddNumberToObject(
            object,
            "data_length",
            definition->data_length
        ) == NULL) ||
        (cJSON_AddNumberToObject(
            object,
            "scale",
            definition->scale
        ) == NULL) ||
        (cJSON_AddNumberToObject(
            object,
            "offset",
            definition->offset
        ) == NULL)) {

        cJSON_Delete(object);
        return NULL;
    }

    return object;
}

esp_err_t uds_did_catalog_service_encode_json(
    const uds_did_catalog_document_t *document,
    char **json
)
{
    if (json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *json = NULL;
    esp_err_t result =
        uds_did_catalog_document_validate(document);

    if (result != ESP_OK) {
        return result;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *definitions = cJSON_CreateArray();

    if ((root == NULL) ||
        (definitions == NULL) ||
        !cJSON_AddItemToObject(
            root,
            "definitions",
            definitions
        )) {

        cJSON_Delete(root);
        cJSON_Delete(definitions);
        return ESP_ERR_NO_MEM;
    }

    if ((cJSON_AddNumberToObject(
            root,
            "version",
            UDS_DID_CATALOG_SCHEMA_VERSION
        ) == NULL) ||
        (cJSON_AddStringToObject(
            root,
            "name",
            document->name
        ) == NULL) ||
        (cJSON_AddStringToObject(
            root,
            "description",
            document->description
        ) == NULL)) {

        result = ESP_ERR_NO_MEM;
    }

    for (size_t index = 0U;
         (result == ESP_OK) && (index < document->count);
         ++index) {

        cJSON *definition =
            uds_did_catalog_encode_definition(
                &document->definitions[index]
            );

        if ((definition == NULL) ||
            !cJSON_AddItemToArray(definitions, definition)) {

            cJSON_Delete(definition);
            result = ESP_ERR_NO_MEM;
        }
    }

    if (result == ESP_OK) {
        *json = cJSON_Print(root);
        result = (*json != NULL)
            ? ESP_OK
            : ESP_ERR_NO_MEM;
    }

    cJSON_Delete(root);
    return result;
}

static esp_err_t uds_did_catalog_read_file(
    const char *path,
    char **json
)
{
    struct stat information = {0};
    esp_err_t result = storage_sd_service_stat(
        path,
        &information
    );

    if ((result != ESP_OK) ||
        !S_ISREG(information.st_mode) ||
        (information.st_size <= 0) ||
        ((size_t)information.st_size >
         UDS_DID_CATALOG_FILE_MAX_SIZE)) {

        return (result != ESP_OK)
            ? result
            : ESP_ERR_INVALID_SIZE;
    }

    char *buffer = heap_caps_calloc(
        (size_t)information.st_size + 1U,
        1U,
        MALLOC_CAP_SPIRAM |
        MALLOC_CAP_8BIT
    );

    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    FILE *file = NULL;
    result = storage_sd_service_open(path, "rb", &file);
    size_t bytes_read = 0U;

    if (result == ESP_OK) {
        result = storage_sd_service_read(
            file,
            buffer,
            (size_t)information.st_size,
            &bytes_read
        );
    }

    const esp_err_t close_result =
        storage_sd_service_close(&file);

    if ((result == ESP_OK) &&
        (close_result != ESP_OK)) {

        result = close_result;
    }

    if ((result == ESP_OK) &&
        (bytes_read != (size_t)information.st_size)) {

        result = ESP_ERR_INVALID_SIZE;
    }

    if (result != ESP_OK) {
        heap_caps_free(buffer);
        return result;
    }

    *json = buffer;
    return ESP_OK;
}

static esp_err_t uds_did_catalog_validate_json(
    const char *json
)
{
    if (json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uds_did_definition_t *definitions = heap_caps_calloc(
        UDS_DID_CATALOG_DEFINITION_MAX_COUNT,
        sizeof(*definitions),
        MALLOC_CAP_SPIRAM |
        MALLOC_CAP_8BIT
    );

    if (definitions == NULL) {
        return ESP_ERR_NO_MEM;
    }

    uds_did_catalog_document_t document = {
        .definitions = definitions,
        .capacity = UDS_DID_CATALOG_DEFINITION_MAX_COUNT,
    };
    const esp_err_t result =
        uds_did_catalog_service_decode_json(
            json,
            &document
        );

    heap_caps_free(definitions);
    return result;
}

static esp_err_t uds_did_catalog_write_json(
    const char *file_name,
    const char *json
)
{
    if (json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t json_length = strnlen(
        json,
        UDS_DID_CATALOG_FILE_MAX_SIZE + 1U
    );

    if ((json_length == 0U) ||
        (json_length > UDS_DID_CATALOG_FILE_MAX_SIZE)) {

        return ESP_ERR_INVALID_SIZE;
    }

    char final_path[192];
    char temporary_path[192];
    esp_err_t result = uds_did_catalog_build_path(
        file_name,
        "",
        final_path,
        sizeof(final_path)
    );

    if (result == ESP_OK) {
        result = uds_did_catalog_build_path(
            file_name,
            ".tmp",
            temporary_path,
            sizeof(temporary_path)
        );
    }

    if (result == ESP_OK) {
        result = uds_did_catalog_service_initialize();
    }

    FILE *file = NULL;
    size_t bytes_written = 0U;

    if (result == ESP_OK) {
        result = storage_sd_service_open(
            temporary_path,
            "wb",
            &file
        );
    }

    if (result == ESP_OK) {
        result = storage_sd_service_write(
            file,
            json,
            json_length,
            &bytes_written
        );
    }

    if ((result == ESP_OK) &&
        (bytes_written != json_length)) {

        result = ESP_FAIL;
    }

    if (result == ESP_OK) {
        result = storage_sd_service_flush(file);
    }

    if (result == ESP_OK) {
        result = storage_sd_service_sync(file);
    }

    const esp_err_t close_result =
        storage_sd_service_close(&file);

    if ((result == ESP_OK) &&
        (close_result != ESP_OK)) {

        result = close_result;
    }

    if (result != ESP_OK) {
        (void)storage_sd_service_remove(temporary_path);
        return result;
    }

    const esp_err_t remove_result =
        storage_sd_service_remove(final_path);

    if ((remove_result != ESP_OK) &&
        (remove_result != ESP_ERR_NOT_FOUND)) {

        (void)storage_sd_service_remove(temporary_path);
        return remove_result;
    }

    result = storage_sd_service_rename(
        temporary_path,
        final_path
    );

    if (result != ESP_OK) {
        (void)storage_sd_service_remove(temporary_path);
    }

    return result;
}

esp_err_t uds_did_catalog_service_initialize(void)
{
    return storage_sd_service_ensure_directory(
        UDS_DID_CATALOG_DIRECTORY
    );
}

esp_err_t uds_did_catalog_service_load(
    const char *file_name,
    uds_did_catalog_document_t *document
)
{
    if (document == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char path[192];
    esp_err_t result = uds_did_catalog_build_path(
        file_name,
        "",
        path,
        sizeof(path)
    );
    char *json = NULL;

    if (result == ESP_OK) {
        result = uds_did_catalog_read_file(path, &json);
    }

    if (result == ESP_OK) {
        result = uds_did_catalog_service_decode_json(
            json,
            document
        );
    }

    heap_caps_free(json);
    return result;
}

esp_err_t uds_did_catalog_service_load_json(
    const char *file_name,
    char **json
)
{
    if (json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *json = NULL;
    char path[192];
    esp_err_t result = uds_did_catalog_build_path(
        file_name,
        "",
        path,
        sizeof(path)
    );

    if (result == ESP_OK) {
        result = uds_did_catalog_read_file(path, json);
    }

    if (result == ESP_OK) {
        result = uds_did_catalog_validate_json(*json);
    }

    if (result != ESP_OK) {
        heap_caps_free(*json);
        *json = NULL;
    }

    return result;
}

esp_err_t uds_did_catalog_service_save(
    const char *file_name,
    const uds_did_catalog_document_t *document
)
{
    char *json = NULL;
    esp_err_t result = uds_did_catalog_service_encode_json(
        document,
        &json
    );

    if (result == ESP_OK) {
        result = uds_did_catalog_write_json(
            file_name,
            json
        );
    }

    free(json);
    return result;
}

esp_err_t uds_did_catalog_service_save_json(
    const char *file_name,
    const char *json
)
{
    esp_err_t result = uds_did_catalog_validate_json(json);

    if (result == ESP_OK) {
        result = uds_did_catalog_write_json(
            file_name,
            json
        );
    }

    return result;
}

esp_err_t uds_did_catalog_service_remove(
    const char *file_name
)
{
    char path[192];
    const esp_err_t result = uds_did_catalog_build_path(
        file_name,
        "",
        path,
        sizeof(path)
    );

    return (result == ESP_OK)
        ? storage_sd_service_remove(path)
        : result;
}

esp_err_t uds_did_catalog_service_list(
    size_t offset,
    uds_did_catalog_summary_t *catalogs,
    size_t capacity,
    size_t *count,
    bool *has_more
)
{
    if ((catalogs == NULL) ||
        (capacity == 0U) ||
        (count == NULL) ||
        (has_more == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *count = 0U;
    *has_more = false;
    esp_err_t result = uds_did_catalog_service_initialize();
    size_t directory_offset = 0U;
    size_t catalog_offset = 0U;

    uds_did_definition_t *definition_buffer = NULL;

    if (result == ESP_OK) {
        definition_buffer = heap_caps_calloc(
            UDS_DID_CATALOG_DEFINITION_MAX_COUNT,
            sizeof(*definition_buffer),
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

        if (definition_buffer == NULL) {
            result = ESP_ERR_NO_MEM;
        }
    }

    while (result == ESP_OK) {
        storage_file_entry_t entries[UDS_DID_CATALOG_LIST_PAGE_SIZE];
        size_t entry_count = 0U;
        bool entries_remaining = false;

        result = storage_sd_service_list(
            UDS_DID_CATALOG_DIRECTORY,
            directory_offset,
            entries,
            UDS_DID_CATALOG_LIST_PAGE_SIZE,
            &entry_count,
            &entries_remaining
        );

        if (result != ESP_OK) {
            break;
        }

        for (size_t index = 0U;
             index < entry_count;
             ++index) {

            ++directory_offset;

            if (entries[index].is_directory ||
                !uds_did_catalog_file_name_valid(entries[index].name)) {

                continue;
            }

            char path[192];
            char *json = NULL;
            esp_err_t load_result = uds_did_catalog_build_path(
                entries[index].name,
                "",
                path,
                sizeof(path)
            );

            if (load_result == ESP_OK) {
                load_result = uds_did_catalog_read_file(path, &json);
            }

            if (load_result == ESP_OK) {
                uds_did_catalog_document_t document = {
                    .definitions = definition_buffer,
                    .capacity =
                        UDS_DID_CATALOG_DEFINITION_MAX_COUNT,
                };

                load_result =
                    uds_did_catalog_service_decode_json(
                        json,
                        &document
                    );

                if (load_result == ESP_OK) {
                    if (catalog_offset < offset) {
                        ++catalog_offset;
                    } else if (*count == capacity) {
                        *has_more = true;
                    } else {
                        uds_did_catalog_summary_t *summary =
                            &catalogs[*count];

                        memset(summary, 0, sizeof(*summary));
                        (void)strncpy(
                            summary->file_name,
                            entries[index].name,
                            sizeof(summary->file_name) - 1U
                        );
                        (void)strncpy(
                            summary->name,
                            document.name,
                            sizeof(summary->name) - 1U
                        );
                        (void)strncpy(
                            summary->description,
                            document.description,
                            sizeof(summary->description) - 1U
                        );
                        summary->definition_count = document.count;
                        ++(*count);
                        ++catalog_offset;
                    }
                }
            }

            heap_caps_free(json);

            if (load_result != ESP_OK) {
                continue;
            }

            if (*has_more) {
                result = ESP_OK;
                goto cleanup;
            }
        }

        if (!entries_remaining || (entry_count == 0U)) {
            break;
        }
    }

cleanup:

    heap_caps_free(definition_buffer);
    return result;
}
