/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "xcp_programming_service.h"

#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_task_priorities.h"
#include "firmware_image.h"
#include "storage_sd_service.h"
#include "xcp_commands.h"

#define XCP_PROGRAMMING_JOURNAL_PATH \
    "/logs/firmware/xcp-resume.json"
#define XCP_PROGRAMMING_JOURNAL_TEMP_PATH \
    "/logs/firmware/xcp-resume.tmp"
#define XCP_PROGRAMMING_TASK_STACK_SIZE (8192U)
#define XCP_PROGRAMMING_STOP_TIMEOUT_MS (5000U)

typedef struct
{
    xcp_programming_config_t config;
    bool resume;

} xcp_programming_task_context_t;

typedef struct
{
    FILE *file;

} xcp_programming_reader_context_t;

static const char *TAG = "xcp_programming_service";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task = NULL;
static bool s_resume_checked = false;
static atomic_bool s_cancel_requested = ATOMIC_VAR_INIT(false);
static xcp_programming_info_t s_info = {
    .state = XCP_PROGRAMMING_IDLE,
    .last_error = ESP_OK,
};

static void xcp_programming_task(void *context);

static void xcp_programming_set_state(
    xcp_programming_state_t state,
    esp_err_t error
);

static esp_err_t xcp_programming_file_read(
    uint64_t offset,
    uint8_t *buffer,
    size_t capacity,
    size_t *read_size,
    void *context
);

static esp_err_t xcp_programming_open_image(
    const xcp_programming_config_t *config,
    FILE **file,
    firmware_image_reader_t *reader,
    xcp_programming_reader_context_t *reader_context,
    struct stat *file_stat
);

static esp_err_t xcp_programming_execute(
    uint32_t session_id,
    const uint8_t *command,
    size_t command_size
);

static esp_err_t xcp_programming_set_mta(
    uint32_t session_id,
    const xcp_service_session_info_t *session,
    uint8_t extension,
    uint32_t address
);

static esp_err_t xcp_programming_send_block(
    uint32_t session_id,
    const xcp_service_session_info_t *session,
    const uint8_t *data,
    size_t size
);

static esp_err_t xcp_programming_write_journal(
    const xcp_programming_config_t *config,
    const struct stat *file_stat,
    uint64_t image_size,
    uint64_t confirmed_bytes,
    uint64_t next_address,
    uint32_t confirmed_blocks
);

static esp_err_t xcp_programming_read_journal(
    xcp_programming_config_t *config,
    uint64_t *file_size,
    int64_t *modified_time,
    uint64_t *image_size,
    uint64_t *confirmed_bytes,
    uint64_t *next_address,
    uint32_t *confirmed_blocks
);

static esp_err_t xcp_programming_remove_journal(void)
{
    struct stat file_stat;
    const esp_err_t stat_result = storage_sd_service_stat(
        XCP_PROGRAMMING_JOURNAL_PATH,
        &file_stat
    );

    if (stat_result == ESP_ERR_NOT_FOUND) {
        return ESP_OK;
    }

    return (stat_result == ESP_OK)
        ? storage_sd_service_remove(XCP_PROGRAMMING_JOURNAL_PATH)
        : stat_result;
}

static void xcp_programming_set_state(
    xcp_programming_state_t state,
    esp_err_t error
)
{
    portENTER_CRITICAL(&s_lock);
    s_info.state = state;
    s_info.last_error = error;
    portEXIT_CRITICAL(&s_lock);
}

static esp_err_t xcp_programming_file_read(
    uint64_t offset,
    uint8_t *buffer,
    size_t capacity,
    size_t *read_size,
    void *context
)
{
    xcp_programming_reader_context_t *reader_context = context;

    if ((reader_context == NULL) ||
        (reader_context->file == NULL) ||
        (buffer == NULL) ||
        (read_size == NULL) ||
        (offset > LONG_MAX)) {

        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = storage_sd_service_seek(
        reader_context->file,
        (long)offset,
        SEEK_SET
    );

    if (result == ESP_OK) {
        result = storage_sd_service_read(
            reader_context->file,
            buffer,
            capacity,
            read_size
        );
    }

    return result;
}

static esp_err_t xcp_programming_open_image(
    const xcp_programming_config_t *config,
    FILE **file,
    firmware_image_reader_t *reader,
    xcp_programming_reader_context_t *reader_context,
    struct stat *file_stat
)
{
    firmware_image_format_t format;
    esp_err_t result = firmware_image_format_from_name(
        config->path,
        &format
    );

    if (result != ESP_OK) {
        return result;
    }

    result = storage_sd_service_stat(config->path, file_stat);

    if ((result != ESP_OK) ||
        !S_ISREG(file_stat->st_mode) ||
        (file_stat->st_size <= 0)) {

        return (result == ESP_OK)
            ? ESP_ERR_INVALID_SIZE
            : result;
    }

    result = storage_sd_service_open(config->path, "rb", file);

    if (result != ESP_OK) {
        return result;
    }

    reader_context->file = *file;
    const firmware_image_config_t image_config = {
        .format = format,
        .read = xcp_programming_file_read,
        .read_context = reader_context,
        .file_size = (uint64_t)file_stat->st_size,
        .binary_address = config->binary_address,
    };

    result = firmware_image_open(reader, &image_config);

    if (result != ESP_OK) {
        storage_sd_service_close(file);
    }

    return result;
}

static esp_err_t xcp_programming_execute(
    uint32_t session_id,
    const uint8_t *command,
    size_t command_size
)
{
    uint8_t response[XCP_SERVICE_RESPONSE_MAX_SIZE];
    size_t response_size = 0U;

    return xcp_service_execute_cto(
        session_id,
        command,
        command_size,
        response,
        sizeof(response),
        &response_size
    );
}

static esp_err_t xcp_programming_set_mta(
    uint32_t session_id,
    const xcp_service_session_info_t *session,
    uint8_t extension,
    uint32_t address
)
{
    uint8_t command[XCP_CAN_FD_CTO_MAX_SIZE];
    size_t command_size = 0U;
    esp_err_t result = xcp_command_encode_set_mta(
        extension,
        address,
        session->slave.byte_order_big_endian,
        command,
        sizeof(command),
        &command_size
    );

    if (result == ESP_OK) {
        result = xcp_programming_execute(
            session_id,
            command,
            command_size
        );
    }

    return result;
}

static esp_err_t xcp_programming_send_block(
    uint32_t session_id,
    const xcp_service_session_info_t *session,
    const uint8_t *data,
    size_t size
)
{
    if ((data == NULL) ||
        (size == 0U) ||
        (size > UINT8_MAX) ||
        (session->slave.address_granularity != 0U) ||
        (session->slave.maximum_cto < 3U)) {

        return ESP_ERR_INVALID_SIZE;
    }

    const size_t payload_size = session->slave.maximum_cto - 2U;
    size_t offset = 0U;

    while (offset < size) {
        size_t chunk_size = size - offset;

        if (chunk_size > payload_size) {
            chunk_size = payload_size;
        }

        uint8_t command[XCP_CAN_FD_CTO_MAX_SIZE];
        size_t command_size = 0U;
        esp_err_t result = xcp_command_encode_program_packet(
            XCP_COMMAND_PROGRAM,
            (uint8_t)chunk_size,
            &data[offset],
            chunk_size,
            command,
            sizeof(command),
            &command_size
        );

        if (result != ESP_OK) {
            return result;
        }

        result = xcp_programming_execute(
            session_id,
            command,
            command_size
        );

        if (result != ESP_OK) {
            return result;
        }

        offset += chunk_size;

        if (atomic_load(&s_cancel_requested)) {
            return ESP_ERR_INVALID_STATE;
        }

        taskYIELD();
    }

    return ESP_OK;
}

static cJSON *xcp_programming_encode_config(
    const xcp_programming_config_t *config
)
{
    cJSON *object = cJSON_CreateObject();

    if (object == NULL) {
        return NULL;
    }

    const bool valid =
        (cJSON_AddStringToObject(object, "path", config->path) != NULL) &&
        (cJSON_AddNumberToObject(object, "binary_address", (double)config->binary_address) != NULL) &&
        (cJSON_AddNumberToObject(object, "bus", config->transport.bus) != NULL) &&
        (cJSON_AddNumberToObject(object, "command_identifier", config->transport.command_identifier) != NULL) &&
        (cJSON_AddNumberToObject(object, "response_identifier", config->transport.response_identifier) != NULL) &&
        (cJSON_AddNumberToObject(object, "stim_identifier", config->transport.stim_identifier) != NULL) &&
        (cJSON_AddBoolToObject(object, "extended", config->transport.extended_identifier) != NULL) &&
        (cJSON_AddBoolToObject(object, "can_fd", config->transport.can_fd) != NULL) &&
        (cJSON_AddBoolToObject(object, "brs", config->transport.bit_rate_switch) != NULL) &&
        (cJSON_AddNumberToObject(object, "tx_length", config->transport.transmit_data_length) != NULL) &&
        (cJSON_AddNumberToObject(object, "padding", config->transport.padding_byte) != NULL) &&
        (cJSON_AddNumberToObject(object, "timeout_ms", config->transport.response_timeout_ms) != NULL) &&
        (cJSON_AddNumberToObject(object, "connect_mode", config->connect_mode) != NULL) &&
        (cJSON_AddNumberToObject(object, "address_extension", config->address_extension) != NULL) &&
        (cJSON_AddBoolToObject(object, "clear_enabled", config->clear_enabled) != NULL) &&
        (cJSON_AddNumberToObject(object, "clear_mode", config->clear_mode) != NULL) &&
        (cJSON_AddBoolToObject(object, "format_enabled", config->program_format_enabled) != NULL) &&
        (cJSON_AddNumberToObject(object, "compression", config->compression_method) != NULL) &&
        (cJSON_AddNumberToObject(object, "encryption", config->encryption_method) != NULL) &&
        (cJSON_AddNumberToObject(object, "programming", config->programming_method) != NULL) &&
        (cJSON_AddNumberToObject(object, "access", config->access_method) != NULL) &&
        (cJSON_AddBoolToObject(object, "verify_enabled", config->verify_enabled) != NULL) &&
        (cJSON_AddNumberToObject(object, "verify_mode", config->verify_mode) != NULL) &&
        (cJSON_AddNumberToObject(object, "verify_type", config->verify_type) != NULL) &&
        (cJSON_AddNumberToObject(object, "verify_value", config->verify_value) != NULL) &&
        (cJSON_AddBoolToObject(object, "reset_enabled", config->reset_enabled) != NULL);

    if (!valid) {
        cJSON_Delete(object);
        return NULL;
    }

    return object;
}

static bool xcp_programming_json_number(
    const cJSON *object,
    const char *name,
    uint64_t maximum,
    uint64_t *value
)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);

    if (!cJSON_IsNumber(item) ||
        (item->valuedouble < 0.0) ||
        (item->valuedouble > (double)maximum)) {

        return false;
    }

    *value = (uint64_t)item->valuedouble;
    return true;
}

static bool xcp_programming_json_bool(
    const cJSON *object,
    const char *name,
    bool *value
)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);

    if (!cJSON_IsBool(item)) {
        return false;
    }

    *value = cJSON_IsTrue(item);
    return true;
}

static esp_err_t xcp_programming_write_journal(
    const xcp_programming_config_t *config,
    const struct stat *file_stat,
    uint64_t image_size,
    uint64_t confirmed_bytes,
    uint64_t next_address,
    uint32_t confirmed_blocks
)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *configuration = xcp_programming_encode_config(config);

    if ((root == NULL) ||
        (configuration == NULL) ||
        !cJSON_AddItemToObject(root, "config", configuration) ||
        (cJSON_AddNumberToObject(root, "version", 1U) == NULL) ||
        (cJSON_AddNumberToObject(root, "file_size", (double)file_stat->st_size) == NULL) ||
        (cJSON_AddNumberToObject(root, "modified_time", (double)file_stat->st_mtime) == NULL) ||
        (cJSON_AddNumberToObject(root, "image_size", (double)image_size) == NULL) ||
        (cJSON_AddNumberToObject(root, "confirmed_bytes", (double)confirmed_bytes) == NULL) ||
        (cJSON_AddNumberToObject(root, "next_address", (double)next_address) == NULL) ||
        (cJSON_AddNumberToObject(root, "confirmed_blocks", confirmed_blocks) == NULL)) {

        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (text == NULL) {
        return ESP_ERR_NO_MEM;
    }

    FILE *file = NULL;
    esp_err_t result = storage_sd_service_open(
        XCP_PROGRAMMING_JOURNAL_TEMP_PATH,
        "wb",
        &file
    );

    if (result == ESP_OK) {
        result = storage_sd_service_write(
            file,
            text,
            strlen(text),
            NULL
        );
    }

    if (result == ESP_OK) {
        result = storage_sd_service_sync(file);
    }

    const esp_err_t close_result = storage_sd_service_close(&file);

    if ((result == ESP_OK) && (close_result != ESP_OK)) {
        result = close_result;
    }

    if (result == ESP_OK) {
        const esp_err_t remove_result = xcp_programming_remove_journal();

        if (remove_result != ESP_OK) {

            result = remove_result;
        }
    }

    if (result == ESP_OK) {
        result = storage_sd_service_rename(
            XCP_PROGRAMMING_JOURNAL_TEMP_PATH,
            XCP_PROGRAMMING_JOURNAL_PATH
        );
    }

    if (result == ESP_OK) {
        portENTER_CRITICAL(&s_lock);
        s_info.resume_available = true;
        s_resume_checked = true;
        portEXIT_CRITICAL(&s_lock);
    }

    free(text);
    return result;
}

static esp_err_t xcp_programming_read_file_text(
    const char *path,
    char **text
)
{
    struct stat file_stat;
    esp_err_t result = storage_sd_service_stat(path, &file_stat);

    if ((result != ESP_OK) ||
        !S_ISREG(file_stat.st_mode) ||
        (file_stat.st_size <= 0) ||
        (file_stat.st_size > 8192)) {

        return (result == ESP_OK)
            ? ESP_ERR_INVALID_SIZE
            : result;
    }

    char *buffer = malloc((size_t)file_stat.st_size + 1U);

    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    FILE *file = NULL;
    result = storage_sd_service_open(path, "rb", &file);
    size_t read_size = 0U;

    if (result == ESP_OK) {
        result = storage_sd_service_read(
            file,
            buffer,
            (size_t)file_stat.st_size,
            &read_size
        );
    }

    storage_sd_service_close(&file);

    if ((result != ESP_OK) ||
        (read_size != (size_t)file_stat.st_size)) {

        free(buffer);
        return (result == ESP_OK)
            ? ESP_ERR_INVALID_SIZE
            : result;
    }

    buffer[read_size] = '\0';
    *text = buffer;
    return ESP_OK;
}

static esp_err_t xcp_programming_decode_config(
    const cJSON *object,
    xcp_programming_config_t *config
)
{
    const cJSON *path = cJSON_GetObjectItemCaseSensitive(object, "path");
    uint64_t binary_address;
    uint64_t bus;
    uint64_t command_identifier;
    uint64_t response_identifier;
    uint64_t stim_identifier;
    uint64_t tx_length;
    uint64_t padding;
    uint64_t timeout_ms;
    uint64_t connect_mode;
    uint64_t address_extension;
    uint64_t clear_mode;
    uint64_t compression;
    uint64_t encryption;
    uint64_t programming;
    uint64_t access;
    uint64_t verify_mode;
    uint64_t verify_type;
    uint64_t verify_value;

    if (!cJSON_IsString(path) ||
        (strlen(path->valuestring) >= sizeof(config->path)) ||
        !xcp_programming_json_number(object, "binary_address", UINT32_MAX, &binary_address) ||
        !xcp_programming_json_number(object, "bus", CAN_BUS_COUNT - 1U, &bus) ||
        !xcp_programming_json_number(object, "command_identifier", 0x1FFFFFFFU, &command_identifier) ||
        !xcp_programming_json_number(object, "response_identifier", 0x1FFFFFFFU, &response_identifier) ||
        !xcp_programming_json_number(object, "stim_identifier", 0x1FFFFFFFU, &stim_identifier) ||
        !xcp_programming_json_bool(object, "extended", &config->transport.extended_identifier) ||
        !xcp_programming_json_bool(object, "can_fd", &config->transport.can_fd) ||
        !xcp_programming_json_bool(object, "brs", &config->transport.bit_rate_switch) ||
        !xcp_programming_json_number(object, "tx_length", 64U, &tx_length) ||
        !xcp_programming_json_number(object, "padding", UINT8_MAX, &padding) ||
        !xcp_programming_json_number(object, "timeout_ms", UINT32_MAX, &timeout_ms) ||
        !xcp_programming_json_number(object, "connect_mode", UINT8_MAX, &connect_mode) ||
        !xcp_programming_json_number(object, "address_extension", UINT8_MAX, &address_extension) ||
        !xcp_programming_json_bool(object, "clear_enabled", &config->clear_enabled) ||
        !xcp_programming_json_number(object, "clear_mode", UINT8_MAX, &clear_mode) ||
        !xcp_programming_json_bool(object, "format_enabled", &config->program_format_enabled) ||
        !xcp_programming_json_number(object, "compression", UINT8_MAX, &compression) ||
        !xcp_programming_json_number(object, "encryption", UINT8_MAX, &encryption) ||
        !xcp_programming_json_number(object, "programming", UINT8_MAX, &programming) ||
        !xcp_programming_json_number(object, "access", UINT8_MAX, &access) ||
        !xcp_programming_json_bool(object, "verify_enabled", &config->verify_enabled) ||
        !xcp_programming_json_number(object, "verify_mode", UINT8_MAX, &verify_mode) ||
        !xcp_programming_json_number(object, "verify_type", UINT8_MAX, &verify_type) ||
        !xcp_programming_json_number(object, "verify_value", UINT32_MAX, &verify_value) ||
        !xcp_programming_json_bool(object, "reset_enabled", &config->reset_enabled)) {

        return ESP_ERR_INVALID_ARG;
    }

    memset(config, 0, sizeof(*config));
    strlcpy(config->path, path->valuestring, sizeof(config->path));
    config->binary_address = binary_address;
    config->transport.bus = (can_bus_id_t)bus;
    config->transport.command_identifier = command_identifier;
    config->transport.response_identifier = response_identifier;
    config->transport.stim_identifier = stim_identifier;

    xcp_programming_json_bool(object, "extended", &config->transport.extended_identifier);
    xcp_programming_json_bool(object, "can_fd", &config->transport.can_fd);
    xcp_programming_json_bool(object, "brs", &config->transport.bit_rate_switch);
    config->transport.transmit_data_length = (uint8_t)tx_length;
    config->transport.padding_byte = (uint8_t)padding;
    config->transport.response_timeout_ms = (uint32_t)timeout_ms;
    config->connect_mode = (uint8_t)connect_mode;
    config->address_extension = (uint8_t)address_extension;
    xcp_programming_json_bool(object, "clear_enabled", &config->clear_enabled);
    config->clear_mode = (uint8_t)clear_mode;
    xcp_programming_json_bool(object, "format_enabled", &config->program_format_enabled);
    config->compression_method = (uint8_t)compression;
    config->encryption_method = (uint8_t)encryption;
    config->programming_method = (uint8_t)programming;
    config->access_method = (uint8_t)access;
    xcp_programming_json_bool(object, "verify_enabled", &config->verify_enabled);
    config->verify_mode = (uint8_t)verify_mode;
    config->verify_type = (uint8_t)verify_type;
    config->verify_value = (uint32_t)verify_value;
    xcp_programming_json_bool(object, "reset_enabled", &config->reset_enabled);
    return ESP_OK;
}

static esp_err_t xcp_programming_read_journal(
    xcp_programming_config_t *config,
    uint64_t *file_size,
    int64_t *modified_time,
    uint64_t *image_size,
    uint64_t *confirmed_bytes,
    uint64_t *next_address,
    uint32_t *confirmed_blocks
)
{
    char *text = NULL;
    esp_err_t result = xcp_programming_read_file_text(
        XCP_PROGRAMMING_JOURNAL_PATH,
        &text
    );

    if (result != ESP_OK) {
        return result;
    }

    cJSON *root = cJSON_Parse(text);
    free(text);

    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *configuration =
        cJSON_GetObjectItemCaseSensitive(root, "config");
    uint64_t version;
    uint64_t modified;
    uint64_t blocks;

    if (!cJSON_IsObject(configuration) ||
        !xcp_programming_json_number(root, "version", 1U, &version) ||
        (version != 1U) ||
        !xcp_programming_json_number(root, "file_size", UINT64_MAX, file_size) ||
        !xcp_programming_json_number(root, "modified_time", INT64_MAX, &modified) ||
        !xcp_programming_json_number(root, "image_size", UINT64_MAX, image_size) ||
        !xcp_programming_json_number(root, "confirmed_bytes", UINT64_MAX, confirmed_bytes) ||
        !xcp_programming_json_number(root, "next_address", UINT32_MAX, next_address) ||
        !xcp_programming_json_number(root, "confirmed_blocks", UINT32_MAX, &blocks)) {

        result = ESP_ERR_INVALID_RESPONSE;
    } else {
        result = xcp_programming_decode_config(configuration, config);
        *modified_time = (int64_t)modified;
        *confirmed_blocks = (uint32_t)blocks;
    }

    cJSON_Delete(root);
    return result;
}

static void xcp_programming_task(void *context)
{
    xcp_programming_task_context_t *task_context = context;
    xcp_programming_config_t config = task_context->config;
    const bool resume = task_context->resume;
    free(task_context);

    FILE *file = NULL;
    firmware_image_reader_t *reader = heap_caps_calloc(
        1U,
        sizeof(*reader),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    xcp_programming_reader_context_t reader_context = {0};
    struct stat file_stat = {0};
    firmware_image_info_t image_info = {0};
    uint64_t resume_file_size = 0U;
    int64_t resume_modified_time = 0;
    uint64_t resume_image_size = 0U;
    uint64_t resume_bytes = 0U;
    uint64_t resume_address = 0U;
    uint32_t resume_blocks = 0U;
    uint32_t session_id = XCP_SERVICE_SESSION_ID_NONE;
    esp_err_t result = (reader != NULL)
        ? ESP_OK
        : ESP_ERR_NO_MEM;

    xcp_programming_set_state(XCP_PROGRAMMING_VALIDATING, ESP_OK);

    if (resume && (result == ESP_OK)) {
        result = xcp_programming_read_journal(
            &config,
            &resume_file_size,
            &resume_modified_time,
            &resume_image_size,
            &resume_bytes,
            &resume_address,
            &resume_blocks
        );
    }

    if (result == ESP_OK) {
        result = xcp_programming_open_image(
            &config,
            &file,
            reader,
            &reader_context,
            &file_stat
        );
    }

    if (result == ESP_OK) {
        result = firmware_image_inspect(reader, &image_info);
    }

    if (resume &&
        (result == ESP_OK) &&
        (((uint64_t)file_stat.st_size != resume_file_size) ||
         ((int64_t)file_stat.st_mtime != resume_modified_time) ||
         (image_info.data_size != resume_image_size) ||
         (resume_bytes > image_info.data_size))) {

        result = ESP_ERR_INVALID_STATE;
    }

    if (result == ESP_OK) {
        storage_sd_service_close(&file);
        memset(reader, 0, sizeof(*reader));
        result = xcp_programming_open_image(
            &config,
            &file,
            reader,
            &reader_context,
            &file_stat
        );
    }

    portENTER_CRITICAL(&s_lock);
    s_info.config = config;
    s_info.image_size = image_info.data_size;
    s_info.total_blocks = image_info.block_count;
    s_info.confirmed_bytes = resume ? resume_bytes : 0U;
    s_info.confirmed_blocks = resume ? resume_blocks : 0U;
    s_info.current_address = resume ? resume_address : image_info.lowest_address;
    s_info.resumed = resume;
    portEXIT_CRITICAL(&s_lock);

    if (result == ESP_OK) {
        xcp_programming_set_state(XCP_PROGRAMMING_CONNECTING, ESP_OK);
        config.transport.callback = NULL;
        config.transport.callback_context = NULL;
        result = xcp_service_open_session(
            &config.transport,
            &session_id
        );
    }

    if (result == ESP_OK) {
        result = xcp_service_connect(session_id, config.connect_mode);
    }

    xcp_service_session_info_t session = {0};

    if (result == ESP_OK) {
        result = xcp_service_get_session_info(session_id, &session);
    }

    if ((result == ESP_OK) &&
        (session.slave.address_granularity != 0U)) {

        result = ESP_ERR_NOT_SUPPORTED;
    }

    uint8_t command[XCP_CAN_FD_CTO_MAX_SIZE];
    size_t command_size = 0U;

    if (result == ESP_OK) {
        xcp_programming_set_state(XCP_PROGRAMMING_PREPARING, ESP_OK);
        result = xcp_command_encode_program_start(
            command,
            sizeof(command),
            &command_size
        );

        if (result == ESP_OK) {
            result = xcp_programming_execute(
                session_id,
                command,
                command_size
            );
        }
    }

    if ((result == ESP_OK) && config.program_format_enabled) {
        result = xcp_command_encode_program_format(
            config.compression_method,
            config.encryption_method,
            config.programming_method,
            config.access_method,
            command,
            sizeof(command),
            &command_size
        );

        if (result == ESP_OK) {
            result = xcp_programming_execute(
                session_id,
                command,
                command_size
            );
        }
    }

    if ((result == ESP_OK) && config.clear_enabled && !resume) {
        xcp_programming_set_state(XCP_PROGRAMMING_ERASING, ESP_OK);
        result = xcp_programming_set_mta(
            session_id,
            &session,
            config.address_extension,
            (uint32_t)image_info.lowest_address
        );

        if (result == ESP_OK) {
            const uint64_t clear_size =
                image_info.highest_address - image_info.lowest_address + 1U;

            if (clear_size > UINT32_MAX) {
                result = ESP_ERR_INVALID_SIZE;
            } else {
                result = xcp_command_encode_program_clear(
                    config.clear_mode,
                    (uint32_t)clear_size,
                    session.slave.byte_order_big_endian,
                    command,
                    sizeof(command),
                    &command_size
                );

                if (result == ESP_OK) {
                    result = xcp_programming_execute(
                        session_id,
                        command,
                        command_size
                    );
                }
            }
        }

        if (result == ESP_OK) {
            result = xcp_programming_write_journal(
                &config,
                &file_stat,
                image_info.data_size,
                0U,
                image_info.lowest_address,
                0U
            );
        }
    }

    uint64_t consumed = 0U;
    uint32_t block_index = 0U;
    firmware_image_block_t block;

    if (result == ESP_OK) {
        xcp_programming_set_state(XCP_PROGRAMMING_TRANSFERRING, ESP_OK);
    }

    while ((result == ESP_OK) &&
           ((result = firmware_image_next(reader, &block)) == ESP_OK)) {

        if (atomic_load(&s_cancel_requested)) {
            result = ESP_ERR_INVALID_STATE;
            break;
        }

        if (resume && ((consumed + block.size) <= resume_bytes)) {
            consumed += block.size;
            block_index++;
            continue;
        }

        if (resume && (consumed != resume_bytes)) {
            result = ESP_ERR_INVALID_STATE;
            break;
        }

        if (resume &&
            (consumed == resume_bytes) &&
            (block.address != resume_address)) {

            result = ESP_ERR_INVALID_STATE;
            break;
        }

        if (block.address > UINT32_MAX) {
            result = ESP_ERR_INVALID_SIZE;
            break;
        }

        result = xcp_programming_set_mta(
            session_id,
            &session,
            config.address_extension,
            (uint32_t)block.address
        );

        if (result == ESP_OK) {
            result = xcp_programming_send_block(
                session_id,
                &session,
                block.data,
                block.size
            );
        }

        if (result == ESP_OK) {
            consumed += block.size;
            block_index++;
            const uint64_t next_address = block.address + block.size;
            result = xcp_programming_write_journal(
                &config,
                &file_stat,
                image_info.data_size,
                consumed,
                next_address,
                block_index
            );

            portENTER_CRITICAL(&s_lock);
            s_info.confirmed_bytes = consumed;
            s_info.confirmed_blocks = block_index;
            s_info.current_address = next_address;
            portEXIT_CRITICAL(&s_lock);
        }

        taskYIELD();
    }

    if (result == ESP_ERR_NOT_FOUND) {
        result = ESP_OK;
    }

    if ((result == ESP_OK) && config.verify_enabled) {
        xcp_programming_set_state(XCP_PROGRAMMING_VERIFYING, ESP_OK);
        result = xcp_command_encode_program_verify(
            config.verify_mode,
            config.verify_type,
            config.verify_value,
            session.slave.byte_order_big_endian,
            command,
            sizeof(command),
            &command_size
        );

        if (result == ESP_OK) {
            result = xcp_programming_execute(
                session_id,
                command,
                command_size
            );
        }
    }

    if ((result == ESP_OK) && config.reset_enabled) {
        xcp_programming_set_state(XCP_PROGRAMMING_RESETTING, ESP_OK);
        result = xcp_command_encode_program_reset(
            command,
            sizeof(command),
            &command_size
        );

        if (result == ESP_OK) {
            result = xcp_programming_execute(
                session_id,
                command,
                command_size
            );
        }
    }

    if (session_id != XCP_SERVICE_SESSION_ID_NONE) {
        xcp_service_close_session(session_id);
    }

    storage_sd_service_close(&file);
    free(reader);

    if (result == ESP_OK) {
        xcp_programming_remove_journal();
        xcp_programming_set_state(XCP_PROGRAMMING_COMPLETE, ESP_OK);
    } else if (atomic_load(&s_cancel_requested)) {
        xcp_programming_set_state(XCP_PROGRAMMING_CANCELLED, result);
    } else {
        xcp_programming_set_state(XCP_PROGRAMMING_ERROR, result);
    }

    const bool resume_available =
        storage_sd_service_stat(
            XCP_PROGRAMMING_JOURNAL_PATH,
            &file_stat
        ) == ESP_OK;

    portENTER_CRITICAL(&s_lock);
    s_info.resume_available = resume_available;
    s_resume_checked = true;
    s_task = NULL;
    portEXIT_CRITICAL(&s_lock);
    vTaskDelete(NULL);
}

static esp_err_t xcp_programming_launch(
    const xcp_programming_config_t *config,
    bool resume
)
{
    if (!xcp_service_is_running()) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_lock);
    const bool busy = s_task != NULL;
    portEXIT_CRITICAL(&s_lock);

    if (busy) {
        return ESP_ERR_INVALID_STATE;
    }

    xcp_programming_task_context_t *context = heap_caps_calloc(
        1U,
        sizeof(*context),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (context == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (config != NULL) {
        context->config = *config;
        context->config.transport.callback = NULL;
        context->config.transport.callback_context = NULL;
    }

    context->resume = resume;
    atomic_store(&s_cancel_requested, false);

    portENTER_CRITICAL(&s_lock);
    memset(&s_info, 0, sizeof(s_info));
    s_info.state = XCP_PROGRAMMING_VALIDATING;
    s_info.last_error = ESP_OK;
    portEXIT_CRITICAL(&s_lock);

    const BaseType_t task_result = xTaskCreateWithCaps(
        xcp_programming_task,
        "xcp_program",
        XCP_PROGRAMMING_TASK_STACK_SIZE,
        context,
        APP_TASK_PRIORITY_XCP_PROGRAM,
        &s_task,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (task_result != pdPASS) {
        free(context);
        portENTER_CRITICAL(&s_lock);
        s_task = NULL;
        s_info.state = XCP_PROGRAMMING_ERROR;
        s_info.last_error = ESP_ERR_NO_MEM;
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t xcp_programming_service_start(
    const xcp_programming_config_t *config
)
{
    if ((config == NULL) ||
        (config->path[0] == '\0') ||
        ((strncmp(config->path, "/firmwares/", 11U) != 0) &&
         (strncmp(config->path, "firmwares/", 10U) != 0)) ||
        (strstr(config->path, "..") != NULL) ||
        (config->transport.transmit_data_length == 0U) ||
        (config->transport.response_timeout_ms == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    return xcp_programming_launch(config, false);
}

esp_err_t xcp_programming_service_resume(void)
{
    struct stat file_stat;

    if (storage_sd_service_stat(
            XCP_PROGRAMMING_JOURNAL_PATH,
            &file_stat
        ) != ESP_OK) {

        return ESP_ERR_NOT_FOUND;
    }

    return xcp_programming_launch(NULL, true);
}

esp_err_t xcp_programming_service_cancel(void)
{
    portENTER_CRITICAL(&s_lock);
    const bool busy = s_task != NULL;
    portEXIT_CRITICAL(&s_lock);

    if (!busy) {
        return ESP_ERR_INVALID_STATE;
    }

    atomic_store(&s_cancel_requested, true);
    return ESP_OK;
}

esp_err_t xcp_programming_service_stop(void)
{
    portENTER_CRITICAL(&s_lock);
    const bool busy = s_task != NULL;
    portEXIT_CRITICAL(&s_lock);

    if (!busy) {
        return ESP_OK;
    }

    atomic_store(&s_cancel_requested, true);
    const TickType_t started = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(
        XCP_PROGRAMMING_STOP_TIMEOUT_MS
    );

    while (true) {
        portENTER_CRITICAL(&s_lock);
        const bool stopped = s_task == NULL;
        portEXIT_CRITICAL(&s_lock);

        if (stopped) {
            return ESP_OK;
        }

        if ((xTaskGetTickCount() - started) >= timeout) {
            return ESP_ERR_TIMEOUT;
        }

        vTaskDelay(1U);
    }
}

esp_err_t xcp_programming_service_discard_resume(void)
{
    portENTER_CRITICAL(&s_lock);
    const bool busy = s_task != NULL;
    portEXIT_CRITICAL(&s_lock);

    if (busy) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result = xcp_programming_remove_journal();

    if (result == ESP_OK) {
        portENTER_CRITICAL(&s_lock);
        s_info.resume_available = false;
        s_resume_checked = true;
        portEXIT_CRITICAL(&s_lock);
    }

    return result;
}

esp_err_t xcp_programming_service_get_info(
    xcp_programming_info_t *info
)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_lock);
    *info = s_info;
    const bool busy = s_task != NULL;
    const bool resume_checked = s_resume_checked;
    portEXIT_CRITICAL(&s_lock);

    if (!busy && !resume_checked) {
        struct stat file_stat;
        info->resume_available =
            storage_sd_service_stat(
                XCP_PROGRAMMING_JOURNAL_PATH,
                &file_stat
            ) == ESP_OK;

        portENTER_CRITICAL(&s_lock);
        s_info.resume_available = info->resume_available;
        s_resume_checked = true;
        portEXIT_CRITICAL(&s_lock);
    }

    return ESP_OK;
}
