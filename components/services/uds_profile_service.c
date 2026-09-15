/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "uds_profile_service.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "storage_sd_service.h"

#define UDS_PROFILE_SCHEMA_VERSION  (1U)
#define UDS_PROFILE_LIST_PAGE_SIZE  (4U)

static bool uds_profile_file_name_valid(
    const char *file_name
);

static esp_err_t uds_profile_build_path(
    const char *file_name,
    const char *suffix,
    char *path,
    size_t path_size
);

static bool uds_profile_json_uint32(
    const cJSON *object,
    const char *name,
    uint32_t maximum,
    uint32_t *value
);

static bool uds_profile_json_boolean(
    const cJSON *object,
    const char *name,
    bool *value
);

static cJSON *uds_profile_encode(
    const uds_ecu_profile_t *profile
);

static esp_err_t uds_profile_read_file(
    const char *path,
    char **json
);

static bool uds_profile_file_name_valid(
    const char *file_name
)
{
    if (file_name == NULL) {
        return false;
    }

    const size_t length = strnlen(
        file_name,
        UDS_PROFILE_FILE_NAME_MAX_LENGTH
    );

    if ((length <= 5U) ||
        (length >= UDS_PROFILE_FILE_NAME_MAX_LENGTH) ||
        (strcmp(&file_name[length - 5U], ".json") != 0) ||
        (file_name[0] == '.')) {

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

    return strstr(file_name, "..") == NULL;
}

static esp_err_t uds_profile_build_path(
    const char *file_name,
    const char *suffix,
    char *path,
    size_t path_size
)
{
    if (!uds_profile_file_name_valid(file_name) ||
        (suffix == NULL) ||
        (path == NULL) ||
        (path_size == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    const int length = snprintf(
        path,
        path_size,
        "%s/%s%s",
        UDS_PROFILE_DIRECTORY,
        file_name,
        suffix
    );

    return ((length < 0) || ((size_t)length >= path_size))
        ? ESP_ERR_INVALID_SIZE
        : ESP_OK;
}

static bool uds_profile_json_uint32(
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
        (item->valuedouble != (double)item->valueint)) {

        return false;
    }

    *value = (uint32_t)item->valuedouble;
    return true;
}

static bool uds_profile_json_boolean(
    const cJSON *object,
    const char *name,
    bool *value
)
{
    const cJSON *item =
        cJSON_GetObjectItemCaseSensitive(object, name);

    if (!cJSON_IsBool(item)) {
        return false;
    }

    *value = cJSON_IsTrue(item);
    return true;
}

static bool uds_profile_decode_hex(
    const char *text,
    uint8_t *data,
    size_t capacity,
    size_t *length
)
{
    *length = 0U;

    while (*text != '\0') {
        while (isspace((unsigned char)*text)) {
            ++text;
        }

        if (*text == '\0') {
            break;
        }

        char *end = NULL;
        const unsigned long value = strtoul(text, &end, 16);

        if ((end == text) ||
            (value > UINT8_MAX) ||
            ((*end != '\0') &&
             !isspace((unsigned char)*end)) ||
            (*length >= capacity)) {

            return false;
        }

        data[(*length)++] = (uint8_t)value;
        text = end;
    }

    return true;
}

static esp_err_t uds_profile_decode_routine(
    const cJSON *object,
    uds_ecu_profile_routine_t *routine
)
{
    uint32_t identifier = 0U;
    uint32_t status_offset = 0U;
    uint32_t pending_value = 0U;
    uint32_t success_value = 0U;
    const cJSON *record =
        cJSON_GetObjectItemCaseSensitive(
            object,
            "option_record"
        );

    if (!cJSON_IsObject(object) ||
        !cJSON_IsString(record) ||
        !uds_profile_json_boolean(
            object,
            "enabled",
            &routine->enabled
        ) ||
        !uds_profile_json_uint32(
            object,
            "identifier",
            UINT16_MAX,
            &identifier
        ) ||
        !uds_profile_json_boolean(
            object,
            "status_enabled",
            &routine->status_enabled
        ) ||
        !uds_profile_json_uint32(
            object,
            "status_offset",
            UINT8_MAX,
            &status_offset
        ) ||
        !uds_profile_json_uint32(
            object,
            "pending_value",
            UINT8_MAX,
            &pending_value
        ) ||
        !uds_profile_json_uint32(
            object,
            "success_value",
            UINT8_MAX,
            &success_value
        ) ||
        !uds_profile_decode_hex(
            record->valuestring,
            routine->option_record,
            sizeof(routine->option_record),
            &routine->option_record_length
        )) {

        return ESP_ERR_INVALID_ARG;
    }

    routine->identifier = (uint16_t)identifier;
    routine->status_offset = (uint8_t)status_offset;
    routine->pending_value = (uint8_t)pending_value;
    routine->success_value = (uint8_t)success_value;

    return ESP_OK;
}

esp_err_t uds_profile_service_decode_json(
    const char *json,
    uds_ecu_profile_t *profile
)
{
    if ((json == NULL) ||
        (profile == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_ParseWithLengthOpts(
        json,
        strlen(json) + 1U,
        NULL,
        true
    );

    if (root == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uds_ecu_profile_initialize(profile);

    const cJSON *name =
        cJSON_GetObjectItemCaseSensitive(root, "name");
    const cJSON *description =
        cJSON_GetObjectItemCaseSensitive(root, "description");
    const cJSON *transport =
        cJSON_GetObjectItemCaseSensitive(root, "transport");
    const cJSON *programming =
        cJSON_GetObjectItemCaseSensitive(root, "programming");
    uint32_t version = 0U;
    uint32_t bus = 0U;
    uint32_t transmit_identifier = 0U;
    uint32_t receive_identifier = 0U;
    uint32_t link_data_length = 0U;
    uint32_t block_size = 0U;
    uint32_t st_min = 0U;
    uint32_t p2_timeout_ms = 0U;
    uint32_t p2_star_timeout_ms = 0U;
    uint32_t tester_present_interval_ms = 0U;
    uint32_t session_type = 0U;
    uint32_t security_level = 0U;
    uint32_t data_format_identifier = 0U;
    uint32_t address_length = 0U;
    uint32_t size_length = 0U;
    uint32_t routine_poll_interval_ms = 0U;
    uint32_t maximum_routine_polls = 0U;
    uint32_t reset_type = 0U;
    esp_err_t result = ESP_ERR_INVALID_ARG;

    if (!cJSON_IsObject(root) ||
        !uds_profile_json_uint32(
            root,
            "version",
            UDS_PROFILE_SCHEMA_VERSION,
            &version
        ) ||
        (version != UDS_PROFILE_SCHEMA_VERSION) ||
        !cJSON_IsString(name) ||
        !cJSON_IsString(description) ||
        (strnlen(
            name->valuestring,
            UDS_ECU_PROFILE_NAME_MAX_LENGTH
        ) >= UDS_ECU_PROFILE_NAME_MAX_LENGTH) ||
        (strnlen(
            description->valuestring,
            UDS_ECU_PROFILE_DESCRIPTION_MAX_LENGTH
        ) >= UDS_ECU_PROFILE_DESCRIPTION_MAX_LENGTH) ||
        !cJSON_IsObject(transport) ||
        !cJSON_IsObject(programming) ||
        !uds_profile_json_uint32(transport, "bus", 1U, &bus) ||
        !uds_profile_json_uint32(
            transport,
            "tx_id",
            CAN_FRAME_EXTENDED_ID_MAX,
            &transmit_identifier
        ) ||
        !uds_profile_json_uint32(
            transport,
            "rx_id",
            CAN_FRAME_EXTENDED_ID_MAX,
            &receive_identifier
        ) ||
        !uds_profile_json_boolean(
            transport,
            "extended",
            &profile->transport.extended_identifier
        ) ||
        !uds_profile_json_boolean(
            transport,
            "fd",
            &profile->transport.can_fd
        ) ||
        !uds_profile_json_boolean(
            transport,
            "brs",
            &profile->transport.bit_rate_switch
        ) ||
        !uds_profile_json_uint32(
            transport,
            "link_data_length",
            64U,
            &link_data_length
        ) ||
        !uds_profile_json_uint32(
            transport,
            "block_size",
            UINT8_MAX,
            &block_size
        ) ||
        !uds_profile_json_uint32(
            transport,
            "st_min",
            UINT8_MAX,
            &st_min
        ) ||
        !uds_profile_json_uint32(
            transport,
            "p2_ms",
            60000U,
            &p2_timeout_ms
        ) ||
        !uds_profile_json_uint32(
            transport,
            "p2_star_ms",
            60000U,
            &p2_star_timeout_ms
        ) ||
        !uds_profile_json_uint32(
            transport,
            "tester_present_ms",
            60000U,
            &tester_present_interval_ms
        ) ||
        !uds_profile_json_uint32(
            programming,
            "session",
            0x7FU,
            &session_type
        ) ||
        !uds_profile_json_uint32(
            programming,
            "security_level",
            0x7DU,
            &security_level
        ) ||
        !uds_profile_json_uint32(
            programming,
            "data_format",
            UINT8_MAX,
            &data_format_identifier
        ) ||
        !uds_profile_json_uint32(
            programming,
            "address_length",
            8U,
            &address_length
        ) ||
        !uds_profile_json_uint32(
            programming,
            "size_length",
            8U,
            &size_length
        ) ||
        !uds_profile_json_uint32(
            programming,
            "routine_poll_interval_ms",
            60000U,
            &routine_poll_interval_ms
        ) ||
        !uds_profile_json_uint32(
            programming,
            "maximum_routine_polls",
            100000U,
            &maximum_routine_polls
        ) ||
        !uds_profile_json_boolean(
            programming,
            "reset_enabled",
            &profile->programming.reset_enabled
        ) ||
        !uds_profile_json_uint32(
            programming,
            "reset_type",
            0x7FU,
            &reset_type
        ) ||
        !uds_profile_json_boolean(
            programming,
            "restore_default_session",
            &profile->programming.restore_default_session
        )) {

        goto cleanup;
    }

    const cJSON *address =
        cJSON_GetObjectItemCaseSensitive(
            programming,
            "default_address"
        );
    const cJSON *erase =
        cJSON_GetObjectItemCaseSensitive(programming, "erase");
    const cJSON *verify =
        cJSON_GetObjectItemCaseSensitive(programming, "verify");

    if (!cJSON_IsString(address) ||
        !cJSON_IsObject(erase) ||
        !cJSON_IsObject(verify)) {

        goto cleanup;
    }

    if ((address->valuestring[0] == '\0') ||
        (address->valuestring[0] == '-') ||
        (address->valuestring[0] == '+')) {

        goto cleanup;
    }

    errno = 0;
    char *address_end = NULL;
    const uint64_t default_address =
        strtoull(address->valuestring, &address_end, 16);

    if ((errno == ERANGE) ||
        (address_end == address->valuestring) ||
        (*address_end != '\0') ||
        (uds_profile_decode_routine(
            erase,
            &profile->programming.erase
        ) != ESP_OK) ||
        (uds_profile_decode_routine(
            verify,
            &profile->programming.verify
        ) != ESP_OK)) {

        goto cleanup;
    }

    (void)strncpy(
        profile->name,
        name->valuestring,
        sizeof(profile->name) - 1U
    );
    (void)strncpy(
        profile->description,
        description->valuestring,
        sizeof(profile->description) - 1U
    );
    profile->transport.bus = (can_bus_id_t)bus;
    profile->transport.transmit_identifier = transmit_identifier;
    profile->transport.receive_identifier = receive_identifier;
    profile->transport.link_data_length = (uint8_t)link_data_length;
    profile->transport.block_size = (uint8_t)block_size;
    profile->transport.st_min = (uint8_t)st_min;
    profile->transport.p2_timeout_ms = p2_timeout_ms;
    profile->transport.p2_star_timeout_ms = p2_star_timeout_ms;
    profile->transport.tester_present_interval_ms =
        tester_present_interval_ms;
    profile->programming.session_type = (uint8_t)session_type;
    profile->programming.security_level = (uint8_t)security_level;
    profile->programming.data_format_identifier =
        (uint8_t)data_format_identifier;
    profile->programming.address_length = (uint8_t)address_length;
    profile->programming.size_length = (uint8_t)size_length;
    profile->programming.default_memory_address = default_address;
    profile->programming.routine_poll_interval_ms =
        routine_poll_interval_ms;
    profile->programming.maximum_routine_polls =
        maximum_routine_polls;
    profile->programming.reset_type = (uint8_t)reset_type;

    result = uds_ecu_profile_validate(profile);

cleanup:
    cJSON_Delete(root);
    return result;
}

esp_err_t uds_profile_service_encode_json(
    const uds_ecu_profile_t *profile,
    char **json
)
{
    if (json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *json = NULL;

    const esp_err_t result =
        uds_ecu_profile_validate(profile);

    if (result != ESP_OK) {
        return result;
    }

    cJSON *root = uds_profile_encode(profile);

    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    *json = cJSON_Print(root);
    cJSON_Delete(root);

    return (*json != NULL)
        ? ESP_OK
        : ESP_ERR_NO_MEM;
}

static char *uds_profile_encode_hex(
    const uint8_t *data,
    size_t length
)
{
    char *text = calloc((length * 3U) + 1U, 1U);

    if (text == NULL) {
        return NULL;
    }

    size_t offset = 0U;

    for (size_t index = 0U; index < length; ++index) {
        const int written = snprintf(
            &text[offset],
            (length * 3U) + 1U - offset,
            (index == 0U) ? "%02X" : " %02X",
            (unsigned int)data[index]
        );

        if (written < 0) {
            free(text);
            return NULL;
        }

        offset += (size_t)written;
    }

    return text;
}

static cJSON *uds_profile_encode_routine(
    const uds_ecu_profile_routine_t *routine
)
{
    cJSON *object = cJSON_CreateObject();
    char *record = uds_profile_encode_hex(
        routine->option_record,
        routine->option_record_length
    );

    const bool valid =
        (object != NULL) &&
        (record != NULL) &&
        (cJSON_AddBoolToObject(
            object,
            "enabled",
            routine->enabled
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            object,
            "identifier",
            routine->identifier
        ) != NULL) &&
        (cJSON_AddStringToObject(
            object,
            "option_record",
            record
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            object,
            "status_enabled",
            routine->status_enabled
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            object,
            "status_offset",
            routine->status_offset
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            object,
            "pending_value",
            routine->pending_value
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            object,
            "success_value",
            routine->success_value
        ) != NULL);

    free(record);

    if (!valid) {
        cJSON_Delete(object);
        return NULL;
    }

    return object;
}

static cJSON *uds_profile_encode(
    const uds_ecu_profile_t *profile
)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *transport = cJSON_CreateObject();
    cJSON *programming = cJSON_CreateObject();
    cJSON *erase =
        uds_profile_encode_routine(&profile->programming.erase);
    cJSON *verify =
        uds_profile_encode_routine(&profile->programming.verify);
    char address[19];

    (void)snprintf(
        address,
        sizeof(address),
        "%" PRIX64,
        profile->programming.default_memory_address
    );

    if ((root == NULL) ||
        (transport == NULL) ||
        (programming == NULL) ||
        (erase == NULL) ||
        (verify == NULL)) {

        cJSON_Delete(root);
        cJSON_Delete(transport);
        cJSON_Delete(programming);
        cJSON_Delete(erase);
        cJSON_Delete(verify);
        return NULL;
    }

    if (!cJSON_AddItemToObject(root, "transport", transport)) {
        cJSON_Delete(root);
        cJSON_Delete(transport);
        cJSON_Delete(programming);
        cJSON_Delete(erase);
        cJSON_Delete(verify);
        return NULL;
    }

    if (!cJSON_AddItemToObject(root, "programming", programming)) {
        cJSON_Delete(root);
        cJSON_Delete(programming);
        cJSON_Delete(erase);
        cJSON_Delete(verify);
        return NULL;
    }

    if (!cJSON_AddItemToObject(programming, "erase", erase)) {
        cJSON_Delete(root);
        cJSON_Delete(erase);
        cJSON_Delete(verify);
        return NULL;
    }

    if (!cJSON_AddItemToObject(programming, "verify", verify)) {
        cJSON_Delete(root);
        cJSON_Delete(verify);
        return NULL;
    }

    const bool valid =
        (cJSON_AddNumberToObject(
            root,
            "version",
            UDS_PROFILE_SCHEMA_VERSION
        ) != NULL) &&
        (cJSON_AddStringToObject(root, "name", profile->name) != NULL) &&
        (cJSON_AddStringToObject(
            root,
            "description",
            profile->description
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            transport,
            "bus",
            profile->transport.bus
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            transport,
            "tx_id",
            profile->transport.transmit_identifier
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            transport,
            "rx_id",
            profile->transport.receive_identifier
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            transport,
            "extended",
            profile->transport.extended_identifier
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            transport,
            "fd",
            profile->transport.can_fd
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            transport,
            "brs",
            profile->transport.bit_rate_switch
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            transport,
            "link_data_length",
            profile->transport.link_data_length
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            transport,
            "block_size",
            profile->transport.block_size
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            transport,
            "st_min",
            profile->transport.st_min
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            transport,
            "p2_ms",
            profile->transport.p2_timeout_ms
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            transport,
            "p2_star_ms",
            profile->transport.p2_star_timeout_ms
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            transport,
            "tester_present_ms",
            profile->transport.tester_present_interval_ms
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            programming,
            "session",
            profile->programming.session_type
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            programming,
            "security_level",
            profile->programming.security_level
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            programming,
            "data_format",
            profile->programming.data_format_identifier
        ) != NULL) &&
        (cJSON_AddStringToObject(
            programming,
            "default_address",
            address
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            programming,
            "address_length",
            profile->programming.address_length
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            programming,
            "size_length",
            profile->programming.size_length
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            programming,
            "routine_poll_interval_ms",
            profile->programming.routine_poll_interval_ms
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            programming,
            "maximum_routine_polls",
            profile->programming.maximum_routine_polls
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            programming,
            "reset_enabled",
            profile->programming.reset_enabled
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            programming,
            "reset_type",
            profile->programming.reset_type
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            programming,
            "restore_default_session",
            profile->programming.restore_default_session
        ) != NULL);

    if (!valid) {
        cJSON_Delete(root);
        return NULL;
    }

    return root;
}

static esp_err_t uds_profile_read_file(
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
        ((size_t)information.st_size > UDS_PROFILE_FILE_MAX_SIZE)) {

        return (result != ESP_OK)
            ? result
            : ESP_ERR_INVALID_SIZE;
    }

    char *buffer = calloc(
        (size_t)information.st_size + 1U,
        1U
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
        free(buffer);
        return result;
    }

    *json = buffer;
    return ESP_OK;
}

esp_err_t uds_profile_service_initialize(void)
{
    return storage_sd_service_ensure_directory(
        UDS_PROFILE_DIRECTORY
    );
}

esp_err_t uds_profile_service_load(
    const char *file_name,
    uds_ecu_profile_t *profile
)
{
    if (profile == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char path[192];
    esp_err_t result = uds_profile_build_path(
        file_name,
        "",
        path,
        sizeof(path)
    );
    char *json = NULL;

    if (result == ESP_OK) {
        result = uds_profile_read_file(path, &json);
    }

    if (result == ESP_OK) {
        result = uds_profile_service_decode_json(json, profile);
    }

    free(json);
    return result;
}

esp_err_t uds_profile_service_save(
    const char *file_name,
    const uds_ecu_profile_t *profile
)
{
    esp_err_t result = uds_ecu_profile_validate(profile);
    char final_path[192];
    char temporary_path[192];

    if (result == ESP_OK) {
        result = uds_profile_build_path(
            file_name,
            "",
            final_path,
            sizeof(final_path)
        );
    }

    if (result == ESP_OK) {
        result = uds_profile_build_path(
            file_name,
            ".tmp",
            temporary_path,
            sizeof(temporary_path)
        );
    }

    if (result != ESP_OK) {
        return result;
    }

    result = uds_profile_service_initialize();

    char *json = NULL;

    if (result == ESP_OK) {
        result = uds_profile_service_encode_json(
            profile,
            &json
        );
    }

    FILE *file = NULL;

    if (result == ESP_OK) {
        result = storage_sd_service_open(
            temporary_path,
            "wb",
            &file
        );
    }

    const size_t json_length =
        (json != NULL) ? strlen(json) : 0U;
    size_t bytes_written = 0U;

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

esp_err_t uds_profile_service_remove(
    const char *file_name
)
{
    char path[192];
    const esp_err_t result = uds_profile_build_path(
        file_name,
        "",
        path,
        sizeof(path)
    );

    return (result == ESP_OK)
        ? storage_sd_service_remove(path)
        : result;
}

esp_err_t uds_profile_service_list(
    size_t offset,
    uds_profile_summary_t *profiles,
    size_t capacity,
    size_t *count,
    bool *has_more
)
{
    if ((profiles == NULL) ||
        (capacity == 0U) ||
        (count == NULL) ||
        (has_more == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *count = 0U;
    *has_more = false;

    esp_err_t result = uds_profile_service_initialize();
    size_t directory_offset = 0U;
    size_t profile_offset = 0U;

    while (result == ESP_OK) {
        storage_file_entry_t entries[UDS_PROFILE_LIST_PAGE_SIZE];
        size_t entry_count = 0U;
        bool entries_remaining = false;

        result = storage_sd_service_list(
            UDS_PROFILE_DIRECTORY,
            directory_offset,
            entries,
            UDS_PROFILE_LIST_PAGE_SIZE,
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
                !uds_profile_file_name_valid(entries[index].name)) {

                continue;
            }

            uds_ecu_profile_t profile;

            if (uds_profile_service_load(
                    entries[index].name,
                    &profile
                ) != ESP_OK) {

                continue;
            }

            if (profile_offset < offset) {
                ++profile_offset;
                continue;
            }

            if (*count == capacity) {
                *has_more = true;
                return ESP_OK;
            }

            uds_profile_summary_t *summary =
                &profiles[*count];

            memset(summary, 0, sizeof(*summary));
            (void)strncpy(
                summary->file_name,
                entries[index].name,
                sizeof(summary->file_name) - 1U
            );
            (void)strncpy(
                summary->name,
                profile.name,
                sizeof(summary->name) - 1U
            );
            (void)strncpy(
                summary->description,
                profile.description,
                sizeof(summary->description) - 1U
            );
            ++(*count);
            ++profile_offset;
        }

        if (!entries_remaining || (entry_count == 0U)) {
            break;
        }

    }

    return result;
}
