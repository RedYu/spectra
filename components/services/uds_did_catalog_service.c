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

static bool uds_did_catalog_text_copy(
    const cJSON *object,
    const char *name,
    char *destination,
    size_t capacity,
    bool required
)
{
    const cJSON *item =
        cJSON_GetObjectItemCaseSensitive(object, name);

    if (!cJSON_IsString(item)) {
        return false;
    }

    const size_t length = strnlen(item->valuestring, capacity);

    if ((length >= capacity) ||
        (required && (length == 0U))) {

        return false;
    }

    memcpy(destination, item->valuestring, length + 1U);
    return true;
}

static bool uds_did_catalog_number(
    const cJSON *object,
    const char *name,
    uint32_t maximum,
    uint32_t *value
)
{
    const cJSON *item =
        cJSON_GetObjectItemCaseSensitive(object, name);

    if (!cJSON_IsNumber(item) ||
        (item->valuedouble < 0.0) ||
        (item->valuedouble > maximum) ||
        ((double)(uint32_t)item->valuedouble != item->valuedouble)) {

        return false;
    }

    *value = (uint32_t)item->valuedouble;
    return true;
}

static bool uds_did_catalog_enum(
    const cJSON *object,
    const char *name,
    const char *const *names,
    size_t count,
    uint32_t *value
)
{
    const cJSON *item =
        cJSON_GetObjectItemCaseSensitive(object, name);

    if (!cJSON_IsString(item)) {
        return false;
    }

    for (size_t index = 0U; index < count; ++index) {
        if (strcmp(item->valuestring, names[index]) == 0) {
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
    const cJSON *object,
    uds_did_definition_t *definition
)
{
    uint32_t identifier = 0U;
    uint32_t data_type = 0U;
    uint32_t byte_order = 0U;
    uint32_t data_length = 0U;
    const cJSON *scale =
        cJSON_GetObjectItemCaseSensitive(object, "scale");
    const cJSON *offset =
        cJSON_GetObjectItemCaseSensitive(object, "offset");

    memset(definition, 0, sizeof(*definition));

    if (!cJSON_IsObject(object) ||
        !uds_did_catalog_number(
            object,
            "identifier",
            UINT16_MAX,
            &identifier
        ) ||
        !uds_did_catalog_text_copy(
            object,
            "name",
            definition->name,
            sizeof(definition->name),
            true
        ) ||
        !uds_did_catalog_text_copy(
            object,
            "unit",
            definition->unit,
            sizeof(definition->unit),
            false
        ) ||
        !uds_did_catalog_text_copy(
            object,
            "description",
            definition->description,
            sizeof(definition->description),
            false
        ) ||
        !uds_did_catalog_enum(
            object,
            "data_type",
            s_data_type_names,
            UDS_DID_DATA_TYPE_COUNT,
            &data_type
        ) ||
        !uds_did_catalog_enum(
            object,
            "byte_order",
            s_byte_order_names,
            UDS_DID_BYTE_ORDER_COUNT,
            &byte_order
        ) ||
        !uds_did_catalog_number(
            object,
            "data_length",
            UDS_DID_DATA_MAX_LENGTH,
            &data_length
        ) ||
        !cJSON_IsNumber(scale) ||
        !cJSON_IsNumber(offset) ||
        !isfinite(scale->valuedouble) ||
        !isfinite(offset->valuedouble)) {

        return ESP_ERR_INVALID_ARG;
    }

    definition->identifier = (uint16_t)identifier;
    definition->data_type = (uds_did_data_type_t)data_type;
    definition->byte_order = (uds_did_byte_order_t)byte_order;
    definition->data_length = data_length;
    definition->scale = scale->valuedouble;
    definition->offset = offset->valuedouble;

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

    cJSON *root = cJSON_ParseWithLengthOpts(
        json,
        strlen(json) + 1U,
        NULL,
        true
    );
    const cJSON *definitions = (root != NULL)
        ? cJSON_GetObjectItemCaseSensitive(
            root,
            "definitions"
        )
        : NULL;
    uint32_t version = 0U;
    esp_err_t result = ESP_ERR_INVALID_ARG;

    if (!cJSON_IsObject(root) ||
        !uds_did_catalog_number(
            root,
            "version",
            UDS_DID_CATALOG_SCHEMA_VERSION,
            &version
        ) ||
        (version != UDS_DID_CATALOG_SCHEMA_VERSION) ||
        !uds_did_catalog_text_copy(
            root,
            "name",
            document->name,
            sizeof(document->name),
            true
        ) ||
        !uds_did_catalog_text_copy(
            root,
            "description",
            document->description,
            sizeof(document->description),
            false
        ) ||
        !cJSON_IsArray(definitions)) {

        goto cleanup;
    }

    const int definition_count =
        cJSON_GetArraySize(definitions);

    if ((definition_count < 0) ||
        ((size_t)definition_count >
         UDS_DID_CATALOG_DEFINITION_MAX_COUNT) ||
        ((size_t)definition_count > document->capacity)) {

        result = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    cJSON *item = NULL;

    cJSON_ArrayForEach(item, definitions) {
        result = uds_did_catalog_decode_definition(
            item,
            &document->definitions[document->count]
        );

        if (result != ESP_OK) {
            goto cleanup;
        }

        ++document->count;
    }

    result = uds_did_catalog_document_validate(document);

cleanup:
    cJSON_Delete(root);

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

esp_err_t uds_did_catalog_service_save(
    const char *file_name,
    const uds_did_catalog_document_t *document
)
{
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

    char *json = NULL;

    if (result == ESP_OK) {
        result = uds_did_catalog_service_encode_json(
            document,
            &json
        );
    }

    if ((result == ESP_OK) &&
        (strlen(json) > UDS_DID_CATALOG_FILE_MAX_SIZE)) {

        result = ESP_ERR_INVALID_SIZE;
    }

    if (result == ESP_OK) {
        result = uds_did_catalog_service_initialize();
    }

    FILE *file = NULL;
    size_t bytes_written = 0U;
    const size_t json_length =
        (json != NULL) ? strlen(json) : 0U;

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

    free(json);

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
                cJSON *root = cJSON_ParseWithLengthOpts(
                    json,
                    strlen(json) + 1U,
                    NULL,
                    true
                );
                const cJSON *definitions = (root != NULL)
                    ? cJSON_GetObjectItemCaseSensitive(
                        root,
                        "definitions"
                    )
                    : NULL;
                const int definition_count =
                    cJSON_IsArray(definitions)
                        ? cJSON_GetArraySize(definitions)
                        : -1;

                if ((definition_count < 0) ||
                    ((size_t)definition_count >
                     UDS_DID_CATALOG_DEFINITION_MAX_COUNT)) {

                    load_result = ESP_ERR_INVALID_ARG;
                }

                cJSON_Delete(root);

                uds_did_definition_t *definition_buffer = NULL;

                if ((load_result == ESP_OK) &&
                    (definition_count > 0)) {

                    definition_buffer = heap_caps_calloc(
                        (size_t)definition_count,
                        sizeof(*definition_buffer),
                        MALLOC_CAP_SPIRAM |
                        MALLOC_CAP_8BIT
                    );

                    if (definition_buffer == NULL) {
                        load_result = ESP_ERR_NO_MEM;
                    }
                }

                uds_did_catalog_document_t document = {
                    .definitions = definition_buffer,
                    .capacity = (definition_count > 0)
                        ? (size_t)definition_count
                        : 0U,
                };

                if (load_result == ESP_OK) {
                    load_result =
                        uds_did_catalog_service_decode_json(
                            json,
                            &document
                        );
                }

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

                heap_caps_free(definition_buffer);
            }

            heap_caps_free(json);

            if (load_result != ESP_OK) {
                continue;
            }

            if (*has_more) {
                return ESP_OK;
            }
        }

        if (!entries_remaining || (entry_count == 0U)) {
            break;
        }
    }

    return result;
}
