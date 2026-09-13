/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "web_files_api.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cJSON.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_task_priorities.h"
#include "storage_service.h"
#include "storage_sd_service.h"
#include "storage_types.h"
#include "web_api_common.h"

#define WEB_FILE_LIST_DEFAULT_LIMIT     (16U)
#define WEB_FILE_LIST_MAX_LIMIT         (32U)
#define WEB_FILE_LIST_MAX_SD_OFFSET     (1024U)

#define WEB_FILE_QUERY_MAX_LENGTH       (1024U)
#define WEB_FILE_VOLUME_MAX_LENGTH      (16U)
#define WEB_FILE_PATH_MAX_LENGTH        (256U)

#define WEB_FILE_DISPOSITION_MAX_LENGTH \
    ((WEB_FILE_PATH_MAX_LENGTH * 4U) + 64U)

#define WEB_FILE_DOWNLOAD_BUFFER_SIZE (16U * 1024U)
#define WEB_FILE_DOWNLOAD_BUFFER_COUNT (2U)
#define WEB_FILE_DOWNLOAD_READER_STACK_SIZE (4096U)
#define WEB_FILE_DOWNLOAD_QUEUE_WAIT_MS (50U)
#define WEB_FILE_DOWNLOAD_STOP_TIMEOUT_MS (5000U)

#define WEB_FILE_DOWNLOAD_HEADER_MAX_LENGTH \
    (WEB_FILE_DISPOSITION_MAX_LENGTH + 384U)

#define WEB_FILE_ENCODED_NAME_MAX_LENGTH \
    (WEB_FILE_PATH_MAX_LENGTH * 3U)

#define WEB_FILE_QUERY_WORKSPACE_SIZE \
    (WEB_FILE_QUERY_MAX_LENGTH * 2U)

#define WEB_FILE_ACTION_MAX_LENGTH       (24U)
#define WEB_FILE_UPLOAD_BUFFER_SIZE      (16U * 1024U)
#define WEB_FILE_UPLOAD_MAX_SIZE         (256U * 1024U * 1024U)

static const char *TAG =
    "web_files_api";

typedef struct
{
    httpd_req_t *request;

    size_t bytes_sent;
    bool transfer_started;

} web_files_api_stream_context_t;

typedef struct
{
    uint8_t *data;
    size_t size;
    esp_err_t result;
    bool end_of_file;

} web_files_api_download_block_t;

typedef struct
{
    FILE *file;

    QueueHandle_t free_buffers;
    QueueHandle_t ready_blocks;
    SemaphoreHandle_t stopped;

    uint8_t *buffers[
        WEB_FILE_DOWNLOAD_BUFFER_COUNT
    ];

    atomic_bool stop_requested;

    uint64_t remaining_bytes;
    esp_err_t close_result;

} web_files_api_download_reader_t;

static void *web_files_api_alloc_psram(
    size_t size
)
{
    if (size == 0U) {
        return NULL;
    }

    return heap_caps_malloc(
        size,
        MALLOC_CAP_SPIRAM |
        MALLOC_CAP_8BIT
    );
}

static void *web_files_api_calloc_psram(
    size_t count,
    size_t element_size
)
{
    if ((count == 0U) ||
        (element_size == 0U) ||
        (count > (SIZE_MAX / element_size))) {

        return NULL;
    }

    return heap_caps_calloc(
        count,
        element_size,
        MALLOC_CAP_SPIRAM |
        MALLOC_CAP_8BIT
    );
}

static esp_err_t web_files_api_stream_callback(
    const void *data,
    size_t size,
    void *context
)
{
    if ((data == NULL) ||
        (size == 0U) ||
        (context == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    web_files_api_stream_context_t *stream_context =
        context;

    if (stream_context->request == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    stream_context->transfer_started = true;

    errno = 0;

    const esp_err_t result =
        httpd_resp_send_chunk(
            stream_context->request,
            (const char *)data,
            size
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to send file chunk: "
            "socket=%d, size=%u, sent=%u, "
            "errno=%d (%s), error=%s",
            httpd_req_to_sockfd(
                stream_context->request
            ),
            (unsigned int)size,
            (unsigned int)
                stream_context->bytes_sent,
            errno,
            strerror(errno),
            esp_err_to_name(result)
        );

        return result;
    }

    stream_context->bytes_sent += size;

    return ESP_OK;
}

static esp_err_t web_files_api_send_text_error(
    httpd_req_t *request,
    const char *status,
    const char *message
)
{
    if ((request == NULL) ||
        (status == NULL) ||
        (message == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    httpd_resp_set_status(
        request,
        status
    );

    httpd_resp_set_type(
        request,
        "text/plain"
    );

    httpd_resp_set_hdr(
        request,
        "Cache-Control",
        "no-store"
    );

    return httpd_resp_send(
        request,
        message,
        HTTPD_RESP_USE_STRLEN
    );
}

static bool web_files_api_is_filename_attr_char(
    uint8_t character
)
{
    return
        ((character >= 'A') && (character <= 'Z')) ||
        ((character >= 'a') && (character <= 'z')) ||
        ((character >= '0') && (character <= '9')) ||
        (character == '!') ||
        (character == '#') ||
        (character == '$') ||
        (character == '&') ||
        (character == '+') ||
        (character == '-') ||
        (character == '.') ||
        (character == '^') ||
        (character == '_') ||
        (character == '`') ||
        (character == '|') ||
        (character == '~');
}

static esp_err_t web_files_api_encode_filename(
    const char *filename,
    char *encoded,
    size_t encoded_size
)
{
    if ((filename == NULL) ||
        (encoded == NULL) ||
        (encoded_size == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    static const char HEX[] =
        "0123456789ABCDEF";

    size_t output_index = 0U;

    for (size_t input_index = 0U;
         filename[input_index] != '\0';
         ++input_index) {

        const uint8_t character =
            (uint8_t)filename[input_index];

        if (web_files_api_is_filename_attr_char(
                character
            )) {

            if ((output_index + 1U) >=
                encoded_size) {

                return ESP_ERR_INVALID_SIZE;
            }

            encoded[output_index] =
                (char)character;

            ++output_index;

        } else {
            if ((output_index + 3U) >=
                encoded_size) {

                return ESP_ERR_INVALID_SIZE;
            }

            encoded[output_index] = '%';
            encoded[output_index + 1U] =
                HEX[(character >> 4U) & 0x0FU];
            encoded[output_index + 2U] =
                HEX[character & 0x0FU];

            output_index += 3U;
        }
    }

    encoded[output_index] = '\0';

    return ESP_OK;
}

static esp_err_t web_files_api_build_content_disposition(
    const char *path,
    char *header,
    size_t header_size
)
{
    if ((path == NULL) ||
        (header == NULL) ||
        (header_size == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    const char *filename =
        strrchr(
            path,
            '/'
        );

    if (filename != NULL) {
        ++filename;
    } else {
        filename = path;
    }

    if (filename[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    char fallback[
        WEB_FILE_PATH_MAX_LENGTH
    ];

    size_t fallback_index = 0U;

    while ((filename[fallback_index] != '\0') &&
           ((fallback_index + 1U) <
            sizeof(fallback))) {

        const uint8_t character =
            (uint8_t)filename[fallback_index];

        if ((character >= 0x20U) &&
            (character <= 0x7EU) &&
            (character != '"') &&
            (character != '\\')) {

            fallback[fallback_index] =
                (char)character;
        } else {
            fallback[fallback_index] = '_';
        }

        ++fallback_index;
    }

    fallback[fallback_index] = '\0';

    char *encoded =
        web_files_api_alloc_psram(
            WEB_FILE_ENCODED_NAME_MAX_LENGTH
        );

    if (encoded == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result =
        web_files_api_encode_filename(
            filename,
            encoded,
            WEB_FILE_ENCODED_NAME_MAX_LENGTH
        );

    if (result == ESP_OK) {
        const int written =
            snprintf(
                header,
                header_size,
                "attachment; filename=\"%s\"; "
                "filename*=UTF-8''%s",
                fallback,
                encoded
            );

        if ((written < 0) ||
            ((size_t)written >= header_size)) {

            result = ESP_ERR_INVALID_SIZE;
        }
    }

    free(encoded);

    return result;
}

static int web_files_api_hex_value(
    char character
)
{
    if ((character >= '0') &&
        (character <= '9')) {

        return character - '0';
    }

    if ((character >= 'a') &&
        (character <= 'f')) {

        return character - 'a' + 10;
    }

    if ((character >= 'A') &&
        (character <= 'F')) {

        return character - 'A' + 10;
    }

    return -1;
}

static esp_err_t web_files_api_url_decode(
    const char *encoded,
    char *decoded,
    size_t decoded_size
)
{
    if ((encoded == NULL) ||
        (decoded == NULL) ||
        (decoded_size == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    decoded[0] = '\0';

    size_t input_index = 0U;
    size_t output_index = 0U;

    while (encoded[input_index] != '\0') {
        uint8_t character;

        if (encoded[input_index] == '%') {
            if ((encoded[input_index + 1U] == '\0') ||
                (encoded[input_index + 2U] == '\0')) {

                return ESP_ERR_INVALID_ARG;
            }

            const int high =
                web_files_api_hex_value(
                    encoded[input_index + 1U]
                );

            const int low =
                web_files_api_hex_value(
                    encoded[input_index + 2U]
                );

            if ((high < 0) ||
                (low < 0)) {

                return ESP_ERR_INVALID_ARG;
            }

            character =
                (uint8_t)(
                    ((uint8_t)high << 4U) |
                    (uint8_t)low
                );

            input_index += 3U;
        } else {
            character =
                encoded[input_index] == '+'
                    ? (uint8_t)' '
                    : (uint8_t)encoded[input_index];

            ++input_index;
        }

        /*
         * Reject encoded null characters because they could truncate
         * a validated path before it reaches the storage service.
         */
        if (character == 0U) {
            return ESP_ERR_INVALID_ARG;
        }

        if ((output_index + 1U) >=
            decoded_size) {

            return ESP_ERR_INVALID_SIZE;
        }

        decoded[output_index] =
            (char)character;

        ++output_index;
    }

    decoded[output_index] = '\0';

    return ESP_OK;
}

static esp_err_t web_files_api_parse_size(
    const char *text,
    size_t *out_value
)
{
    if ((text == NULL) ||
        (out_value == NULL) ||
        (text[0] == '\0') ||
        (text[0] == '-')) {

        return ESP_ERR_INVALID_ARG;
    }

    errno = 0;

    char *end = NULL;

    const unsigned long long value =
        strtoull(
            text,
            &end,
            10
        );

    if ((errno == ERANGE) ||
        (end == text) ||
        (*end != '\0') ||
        (value >
         (unsigned long long)SIZE_MAX)) {

        return ESP_ERR_INVALID_ARG;
    }

    *out_value =
        (size_t)value;

    return ESP_OK;
}

static esp_err_t web_files_api_build_internal_path(
    const char *path,
    char *out_path,
    size_t out_path_size
)
{
    if ((path == NULL) ||
        (out_path == NULL) ||
        (out_path_size == 0U) ||
        (path[0] != '/')) {

        return ESP_ERR_INVALID_ARG;
    }

    const int written =
        snprintf(
            out_path,
            out_path_size,
            "/storage%s",
            strcmp(path, "/") == 0
                ? ""
                : path
        );

    if ((written < 0) ||
        ((size_t)written >= out_path_size)) {

        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static esp_err_t web_files_api_get_file_parameters(
    httpd_req_t *request,
    char *volume,
    size_t volume_size,
    char *path,
    size_t path_size
)
{
    if ((request == NULL) ||
        (volume == NULL) ||
        (volume_size == 0U) ||
        (path == NULL) ||
        (path_size == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    volume[0] = '\0';
    path[0] = '\0';

    const size_t query_length =
        httpd_req_get_url_query_len(
            request
        );

    if ((query_length == 0U) ||
        (query_length >=
         WEB_FILE_QUERY_MAX_LENGTH)) {

        return ESP_ERR_INVALID_ARG;
    }

    char *workspace =
        web_files_api_alloc_psram(
            WEB_FILE_QUERY_WORKSPACE_SIZE
        );

    if (workspace == NULL) {
        return ESP_ERR_NO_MEM;
    }

    char *query = workspace;

    char *encoded_path =
        workspace +
        WEB_FILE_QUERY_MAX_LENGTH;

    esp_err_t result =
        httpd_req_get_url_query_str(
            request,
            query,
            WEB_FILE_QUERY_MAX_LENGTH
        );

    if (result == ESP_OK) {
        result = httpd_query_key_value(
            query,
            "volume",
            volume,
            volume_size
        );

        if (result != ESP_OK) {
            result = ESP_ERR_INVALID_ARG;
        }
    }

    if (result == ESP_OK) {
        result = httpd_query_key_value(
            query,
            "path",
            encoded_path,
            WEB_FILE_QUERY_MAX_LENGTH
        );

        if (result != ESP_OK) {
            result = ESP_ERR_INVALID_ARG;
        }
    }

    if (result == ESP_OK) {
        result = web_files_api_url_decode(
            encoded_path,
            path,
            path_size
        );

        if ((result != ESP_OK) ||
            (path[0] != '/')) {

            result = ESP_ERR_INVALID_ARG;
        }
    }

    free(workspace);

    return result;
}

static esp_err_t web_files_api_get_query_value(
    httpd_req_t *request,
    const char *name,
    char *value,
    size_t value_size
)
{
    if ((request == NULL) ||
        (name == NULL) ||
        (value == NULL) ||
        (value_size == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    const size_t query_length =
        httpd_req_get_url_query_len(request);

    if ((query_length == 0U) ||
        (query_length >= WEB_FILE_QUERY_MAX_LENGTH)) {

        return ESP_ERR_INVALID_ARG;
    }

    char *query =
        web_files_api_alloc_psram(
            WEB_FILE_QUERY_MAX_LENGTH
        );

    if (query == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result =
        httpd_req_get_url_query_str(
            request,
            query,
            WEB_FILE_QUERY_MAX_LENGTH
        );

    if (result == ESP_OK) {
        result = httpd_query_key_value(
            query,
            name,
            value,
            value_size
        );
    }

    free(query);

    return result;
}

static esp_err_t web_files_api_send_operation_result(
    httpd_req_t *request,
    esp_err_t result,
    const char *success_message
)
{
    if (result == ESP_OK) {
        return web_api_send_message(
            request,
            "200 OK",
            true,
            success_message
        );
    }

    const char *status =
        result == ESP_ERR_INVALID_ARG
            ? "400 Bad Request"
            : result == ESP_ERR_NOT_FOUND
                ? "404 Not Found"
                : result == ESP_ERR_INVALID_STATE
                    ? "409 Conflict"
                    : result == ESP_ERR_TIMEOUT
                        ? "503 Service Unavailable"
                        : "500 Internal Server Error";

    return web_api_send_message(
        request,
        status,
        false,
        esp_err_to_name(result)
    );
}

static esp_err_t web_files_api_upload_sd_file(
    httpd_req_t *request,
    const char *path
)
{
    if (request->content_len >
        WEB_FILE_UPLOAD_MAX_SIZE) {

        return ESP_ERR_INVALID_SIZE;
    }

    FILE *file = NULL;
    esp_err_t result =
        storage_sd_service_open(
            path,
            "wb",
            &file
        );

    if (result != ESP_OK) {
        return result;
    }

    uint8_t *buffer =
        web_files_api_alloc_psram(
            WEB_FILE_UPLOAD_BUFFER_SIZE
        );

    if (buffer == NULL) {
        (void)storage_sd_service_close(&file);
        (void)storage_sd_service_remove(path);
        return ESP_ERR_NO_MEM;
    }

    size_t remaining =
        (size_t)request->content_len;

    while ((result == ESP_OK) &&
           (remaining > 0U)) {

        const size_t requested =
            remaining < WEB_FILE_UPLOAD_BUFFER_SIZE
                ? remaining
                : WEB_FILE_UPLOAD_BUFFER_SIZE;

        const int received =
            httpd_req_recv(
                request,
                (char *)buffer,
                requested
            );

        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }

        if (received <= 0) {
            result = ESP_FAIL;
            break;
        }

        size_t written = 0U;
        result = storage_sd_service_write(
            file,
            buffer,
            (size_t)received,
            &written
        );

        if ((result == ESP_OK) &&
            (written != (size_t)received)) {

            result = ESP_FAIL;
        }

        remaining -= (size_t)received;
    }

    if (result == ESP_OK) {
        result = storage_sd_service_sync(file);
    }

    const esp_err_t close_result =
        storage_sd_service_close(&file);

    free(buffer);

    if ((result == ESP_OK) &&
        (close_result != ESP_OK)) {

        result = close_result;
    }

    if (result != ESP_OK) {
        (void)storage_sd_service_remove(path);
    }

    return result;
}

static esp_err_t web_files_api_mutation_handler(
    httpd_req_t *request
)
{
    if (request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char action[WEB_FILE_ACTION_MAX_LENGTH];
    char volume[WEB_FILE_VOLUME_MAX_LENGTH];
    char path[WEB_FILE_PATH_MAX_LENGTH];

    esp_err_t result =
        web_files_api_get_query_value(
            request,
            "action",
            action,
            sizeof(action)
        );

    if (result != ESP_OK) {
        return web_api_send_message(
            request,
            "400 Bad Request",
            false,
            "Missing file operation"
        );
    }

    result = web_files_api_get_file_parameters(
        request,
        volume,
        sizeof(volume),
        path,
        sizeof(path)
    );

    if ((result != ESP_OK) ||
        (strcmp(volume, "sd") != 0)) {

        return web_api_send_message(
            request,
            "400 Bad Request",
            false,
            "Write operations require the SD volume"
        );
    }

    if (strcmp(action, "mkdir") == 0) {
        result = storage_sd_service_ensure_directory(path);
        return web_files_api_send_operation_result(
            request,
            result,
            "Folder created"
        );
    }

    if (strcmp(action, "create") == 0) {
        if (request->content_len != 0) {
            result = ESP_ERR_INVALID_ARG;
        } else {
            FILE *file = NULL;
            result = storage_sd_service_open(path, "wb", &file);

            if (result == ESP_OK) {
                result = storage_sd_service_close(&file);
            }
        }

        return web_files_api_send_operation_result(
            request,
            result,
            "File created"
        );
    }

    if (strcmp(action, "upload") == 0) {
        result = web_files_api_upload_sd_file(request, path);
        return web_files_api_send_operation_result(
            request,
            result,
            "File uploaded"
        );
    }

    if (strcmp(action, "delete") == 0) {
        result = strcmp(path, "/") == 0
            ? ESP_ERR_INVALID_ARG
            : storage_sd_service_remove(path);

        return web_files_api_send_operation_result(
            request,
            result,
            "Entry deleted"
        );
    }

    if (strcmp(action, "format") == 0) {
        char confirmation[16];

        result = web_files_api_get_query_value(
            request,
            "confirm",
            confirmation,
            sizeof(confirmation)
        );

        if ((result != ESP_OK) ||
            (strcmp(confirmation, "FORMAT") != 0)) {

            result = ESP_ERR_INVALID_ARG;
        } else {
            result = storage_sd_service_format();
        }

        return web_files_api_send_operation_result(
            request,
            result,
            "SD card formatted"
        );
    }

    return web_api_send_message(
        request,
        "400 Bad Request",
        false,
        "Unknown file operation"
    );
}


static esp_err_t web_files_api_send_download_error(
    httpd_req_t *request,
    esp_err_t error
)
{
    if (error == ESP_ERR_NOT_FOUND) {
        return web_files_api_send_text_error(
            request,
            "404 Not Found",
            "File not found"
        );
    }

    if ((error == ESP_ERR_INVALID_ARG) ||
        (error == ESP_ERR_INVALID_SIZE)) {

        return web_files_api_send_text_error(
            request,
            "400 Bad Request",
            "Invalid file path"
        );
    }

    if (error == ESP_ERR_INVALID_STATE) {
        return web_files_api_send_text_error(
            request,
            "409 Conflict",
            "Storage is not available"
        );
    }

    if (error == ESP_ERR_TIMEOUT) {
        return web_files_api_send_text_error(
            request,
            "503 Service Unavailable",
            "Storage is busy"
        );
    }

    return web_files_api_send_text_error(
        request,
        "500 Internal Server Error",
        "Failed to read file"
    );
}

static esp_err_t web_files_api_set_download_headers(
    httpd_req_t *request,
    const char *path,
    char *disposition,
    size_t disposition_size
)
{
    if ((request == NULL) ||
        (path == NULL) ||
        (disposition == NULL) ||
        (disposition_size == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        web_files_api_build_content_disposition(
            path,
            disposition,
            disposition_size
        );

    if (result != ESP_OK) {
        return result;
    }

    result = httpd_resp_set_type(
        request,
        "application/octet-stream"
    );

    if (result != ESP_OK) {
        return result;
    }

    result = httpd_resp_set_hdr(
        request,
        "Content-Disposition",
        disposition
    );

    if (result != ESP_OK) {
        return result;
    }

    result = httpd_resp_set_hdr(
        request,
        "Cache-Control",
        "no-store"
    );

    if (result != ESP_OK) {
        return result;
    }

    return httpd_resp_set_hdr(
        request,
        "X-Content-Type-Options",
        "nosniff"
    );
}

static esp_err_t web_files_api_send_all(
    httpd_req_t *request,
    const void *data,
    size_t size
)
{
    if ((request == NULL) ||
        ((data == NULL) && (size > 0U))) {

        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t *cursor = data;
    size_t remaining = size;

    while (remaining > 0U) {
        const int sent = httpd_send(
            request,
            (const char *)cursor,
            remaining
        );

        if (sent <= 0) {
            return ESP_ERR_HTTPD_RESP_SEND;
        }

        const size_t sent_size =
            (size_t)sent;

        if (sent_size > remaining) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        cursor += sent_size;
        remaining -= sent_size;
    }

    return ESP_OK;
}

/*
 * Overflow-safe decimal parsing; signs and whitespace are not numbers.
 */
static bool web_files_api_range_number(
    const char **cursor,
    uint64_t *value
)
{
    const char *p = *cursor;

    *value = 0U;

    if ((*p < '0') ||
        (*p > '9')) {

        return false;
    }

    while ((*p >= '0') &&
           (*p <= '9')) {

        const unsigned digit =
            (unsigned)(*p - '0');

        if (*value > (UINT64_MAX - digit) / 10U) {
            return false;
        }

        *value = *value * 10U + digit;
        ++p;
    }

    *cursor = p;

    return true;
}

/*
 * Invalid/unsupported syntax is ignored (200); unsatisfiable ranges
 * get 416.
 */
static int web_files_api_parse_range(
    const char *text,
    uint64_t size,
    uint64_t *start,
    uint64_t *length
)
{
    if (strncmp(
            text,
            "bytes=",
            6U
        ) != 0) {

        return 0;
    }

    const char *p = text + 6U;

    uint64_t first = 0U;
    uint64_t last = 0U;

    const bool suffix = (*p == '-');

    if (suffix) {
        ++p;

        if (!web_files_api_range_number(
                &p,
                &last
            ) ||
            (*p != '\0')) {

            return 0;
        }

        if ((last == 0U) ||
            (size == 0U)) {

            return -1;
        }

        *length =
            last < size
                ? last
                : size;

        *start = size - *length;

        return 1;
    }

    if (!web_files_api_range_number(
            &p,
            &first
        ) ||
        (*p++ != '-')) {

        return 0;
    }

    if (*p == '\0') {
        last =
            size == 0U
                ? 0U
                : size - 1U;

    } else if (!web_files_api_range_number(
                   &p,
                   &last
               ) ||
               (*p != '\0') ||
               (last < first)) {

        return 0;
    }

    if (first >= size) {
        return -1;
    }

    if (last >= size) {
        last = size - 1U;
    }

    *start = first;
    *length = last - first + 1U;

    return 1;
}

static esp_err_t web_files_api_send_sd_download_header(
    httpd_req_t *request,
    const char *disposition,
    uint64_t content_length,
    uint64_t file_size,
    uint64_t range_start,
    bool partial
)
{
    if ((request == NULL) ||
        (disposition == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    char *header =
        web_files_api_alloc_psram(
            WEB_FILE_DOWNLOAD_HEADER_MAX_LENGTH
        );

    if (header == NULL) {
        return ESP_ERR_NO_MEM;
    }

    char content_range[112] = {0};

    if (partial) {
        snprintf(
            content_range,
            sizeof(content_range),
            "Content-Range: bytes %llu-%llu/%llu\r\n",
            (unsigned long long)range_start,
            (unsigned long long)(
                range_start + content_length - 1U
            ),
            (unsigned long long)file_size
        );
    }

    const int written = snprintf(
        header,
        WEB_FILE_DOWNLOAD_HEADER_MAX_LENGTH,
        "HTTP/1.1 %s\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: %llu\r\n"
        "Content-Disposition: %s\r\n"
        "Accept-Ranges: bytes\r\n"
        "%s"
        "Cache-Control: no-store\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "\r\n",
        partial
            ? "206 Partial Content"
            : "200 OK",
        (unsigned long long)content_length,
        disposition,
        content_range
    );

    esp_err_t result;

    if ((written < 0) ||
        ((size_t)written >=
         WEB_FILE_DOWNLOAD_HEADER_MAX_LENGTH)) {

        result = ESP_ERR_INVALID_SIZE;
    } else {
        result = web_files_api_send_all(
            request,
            header,
            (size_t)written
        );
    }

    free(header);

    return result;
}

static esp_err_t web_files_api_download_internal_file(
    httpd_req_t *request,
    const char *path
)
{
    if ((request == NULL) ||
        (path == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    char internal_path[
        WEB_FILE_PATH_MAX_LENGTH
    ];

    esp_err_t result =
        web_files_api_build_internal_path(
            path,
            internal_path,
            sizeof(internal_path)
        );

    if (result != ESP_OK) {
        return web_files_api_send_download_error(
            request,
            result
        );
    }

    char *disposition =
        web_files_api_alloc_psram(
            WEB_FILE_DISPOSITION_MAX_LENGTH
        );

    if (disposition == NULL) {
        return web_files_api_send_download_error(
            request,
            ESP_ERR_NO_MEM
        );
    }

    result = web_files_api_set_download_headers(
        request,
        path,
        disposition,
        WEB_FILE_DISPOSITION_MAX_LENGTH
    );

    if (result != ESP_OK) {
        free(disposition);

        return web_files_api_send_download_error(
            request,
            result
        );
    }

    web_files_api_stream_context_t context = {
        .request = request,
        .bytes_sent = 0U,
        .transfer_started = false,
    };

    result = storage_service_stream_file(
        internal_path,
        web_files_api_stream_callback,
        &context
    );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to stream internal file '%s' "
            "after %u bytes: %s",
            internal_path,
            (unsigned int)context.bytes_sent,
            esp_err_to_name(result)
        );

        free(disposition);

        if (context.transfer_started) {
            return result;
        }

        return web_files_api_send_download_error(
            request,
            result
        );
    }

    result = httpd_resp_send_chunk(
        request,
        NULL,
        0U
    );

    free(disposition);

    return result;
}

static bool web_files_api_download_stop_requested(
    const web_files_api_download_reader_t *reader
)
{
    return atomic_load_explicit(
        &reader->stop_requested,
        memory_order_acquire
    );
}

static bool web_files_api_download_queue_block(
    web_files_api_download_reader_t *reader,
    const web_files_api_download_block_t *block
)
{
    while (!web_files_api_download_stop_requested(
               reader
           )) {

        if (xQueueSend(
                reader->ready_blocks,
                block,
                pdMS_TO_TICKS(
                    WEB_FILE_DOWNLOAD_QUEUE_WAIT_MS
                )
            ) == pdTRUE) {

            return true;
        }
    }

    return false;
}

static void web_files_api_download_reader_task(
    void *argument
)
{
    web_files_api_download_reader_t *reader =
        argument;

    while (!web_files_api_download_stop_requested(
               reader
           )) {

        if (reader->remaining_bytes == 0U) {
            const web_files_api_download_block_t block = {
                .data = NULL,
                .size = 0U,
                .result = ESP_OK,
                .end_of_file = true,
            };

            (void)web_files_api_download_queue_block(
                reader,
                &block
            );

            break;
        }

        uint8_t *buffer = NULL;

        if (xQueueReceive(
                reader->free_buffers,
                &buffer,
                pdMS_TO_TICKS(
                    WEB_FILE_DOWNLOAD_QUEUE_WAIT_MS
                )
            ) != pdTRUE) {

            continue;
        }

        if (web_files_api_download_stop_requested(
                reader
            )) {

            break;
        }

        size_t bytes_read = 0U;

        const size_t read_size =
            reader->remaining_bytes <
            WEB_FILE_DOWNLOAD_BUFFER_SIZE
                ? (size_t)reader->remaining_bytes
                : WEB_FILE_DOWNLOAD_BUFFER_SIZE;

        const esp_err_t read_result =
            storage_sd_service_read(
                reader->file,
                buffer,
                read_size,
                &bytes_read
            );

        if ((read_result != ESP_OK) ||
            (bytes_read == 0U) ||
            (bytes_read > read_size)) {

            esp_err_t block_result =
                read_result;

            if ((block_result == ESP_OK) &&
                ((bytes_read == 0U) ||
                 (bytes_read > read_size))) {

                block_result =
                    ESP_ERR_INVALID_SIZE;
            }

            const web_files_api_download_block_t block = {
                .data = NULL,
                .size = 0U,
                .result = block_result,
                .end_of_file = false,
            };

            (void)web_files_api_download_queue_block(
                reader,
                &block
            );

            break;
        }

        reader->remaining_bytes -=
            bytes_read;

        const web_files_api_download_block_t block = {
            .data = buffer,
            .size = bytes_read,
            .result = ESP_OK,
            .end_of_file = false,
        };

        if (!web_files_api_download_queue_block(
                reader,
                &block
            )) {

            break;
        }
    }

    reader->close_result =
        storage_sd_service_close(
            &reader->file
        );

    (void)xSemaphoreGive(
        reader->stopped
    );

    vTaskDelete(NULL);
}

static void web_files_api_download_reader_destroy(
    web_files_api_download_reader_t *reader
)
{
    if (reader == NULL) {
        return;
    }

    if (reader->free_buffers != NULL) {
        vQueueDelete(reader->free_buffers);
    }

    if (reader->ready_blocks != NULL) {
        vQueueDelete(reader->ready_blocks);
    }

    if (reader->stopped != NULL) {
        vSemaphoreDelete(reader->stopped);
    }

    for (size_t index = 0U;
         index < WEB_FILE_DOWNLOAD_BUFFER_COUNT;
         ++index) {

        free(reader->buffers[index]);
    }

    free(reader);
}

static esp_err_t web_files_api_download_reader_create(
    FILE *file,
    uint64_t content_length,
    web_files_api_download_reader_t **out_reader
)
{
    if ((file == NULL) ||
        (out_reader == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *out_reader = NULL;

    web_files_api_download_reader_t *reader =
        web_files_api_calloc_psram(
            1U,
            sizeof(*reader)
        );

    if (reader == NULL) {
        return ESP_ERR_NO_MEM;
    }

    reader->file = file;
    reader->remaining_bytes =
        content_length;
    reader->close_result = ESP_OK;

    atomic_init(
        &reader->stop_requested,
        false
    );

    reader->free_buffers = xQueueCreate(
        WEB_FILE_DOWNLOAD_BUFFER_COUNT,
        sizeof(uint8_t *)
    );

    reader->ready_blocks = xQueueCreate(
        WEB_FILE_DOWNLOAD_BUFFER_COUNT,
        sizeof(web_files_api_download_block_t)
    );

    reader->stopped =
        xSemaphoreCreateBinary();

    if ((reader->free_buffers == NULL) ||
        (reader->ready_blocks == NULL) ||
        (reader->stopped == NULL)) {

        web_files_api_download_reader_destroy(
            reader
        );

        return ESP_ERR_NO_MEM;
    }

    for (size_t index = 0U;
         index < WEB_FILE_DOWNLOAD_BUFFER_COUNT;
         ++index) {

        reader->buffers[index] =
            web_files_api_alloc_psram(
                WEB_FILE_DOWNLOAD_BUFFER_SIZE
            );

        if (reader->buffers[index] == NULL) {
            web_files_api_download_reader_destroy(
                reader
            );

            return ESP_ERR_NO_MEM;
        }

        if (xQueueSend(
                reader->free_buffers,
                &reader->buffers[index],
                0U
            ) != pdTRUE) {

            web_files_api_download_reader_destroy(
                reader
            );

            return ESP_FAIL;
        }
    }

    const BaseType_t task_result =
        xTaskCreate(
            web_files_api_download_reader_task,
            "web_file_reader",
            WEB_FILE_DOWNLOAD_READER_STACK_SIZE,
            reader,
            APP_TASK_PRIORITY_NETWORK_WORKER,
            NULL
        );

    if (task_result != pdPASS) {
        web_files_api_download_reader_destroy(
            reader
        );

        return ESP_ERR_NO_MEM;
    }

    *out_reader = reader;

    return ESP_OK;
}

static esp_err_t web_files_api_download_sd_file(
    httpd_req_t *request,
    const char *path
)
{
    if ((request == NULL) ||
        (path == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    struct stat file_status;

    esp_err_t result =
        storage_sd_service_stat(
            path,
            &file_status
        );

    if (result != ESP_OK) {
        return web_files_api_send_download_error(
            request,
            result
        );
    }

    if (!S_ISREG(file_status.st_mode) ||
        (file_status.st_size < 0)) {

        return web_files_api_send_download_error(
            request,
            ESP_ERR_INVALID_ARG
        );
    }

    const uint64_t file_size =
        (uint64_t)file_status.st_size;

    uint64_t content_length = file_size;
    uint64_t range_start = 0U;

    int range_result = 0;

    char range[128];

    const size_t range_size =
        httpd_req_get_hdr_value_len(
            request,
            "Range"
        );

    /*
     * Without a strong validator, If-Range must fall back to a full
     * response.
     */
    if ((range_size > 0U) &&
        (range_size < sizeof(range)) &&
        (httpd_req_get_hdr_value_len(
            request,
            "If-Range"
        ) == 0U) &&
        (httpd_req_get_hdr_value_str(
            request,
            "Range",
            range,
            sizeof(range)
        ) == ESP_OK)) {

        range_result = web_files_api_parse_range(
            range,
            file_size,
            &range_start,
            &content_length
        );
    }

    if (range_result < 0) {
        char value[48];

        snprintf(
            value,
            sizeof(value),
            "bytes */%llu",
            (unsigned long long)file_size
        );

        httpd_resp_set_status(
            request,
            "416 Range Not Satisfiable"
        );

        httpd_resp_set_hdr(
            request,
            "Content-Range",
            value
        );

        httpd_resp_set_hdr(
            request,
            "Accept-Ranges",
            "bytes"
        );

        return httpd_resp_send(
            request,
            "",
            0
        );
    }

    FILE *file = NULL;

    result =
        storage_sd_service_open(
            path,
            "rb",
            &file
        );

    if (result != ESP_OK) {
        return web_files_api_send_download_error(
            request,
            result
        );
    }

    /*
     * The storage seek API uses long; advance in bounded steps for
     * large files.
     */
    uint64_t seek_remaining = range_start;

    while (seek_remaining > 0U) {
        const long step =
            seek_remaining > LONG_MAX
                ? LONG_MAX
                : (long)seek_remaining;

        result = storage_sd_service_seek(
            file,
            step,
            SEEK_CUR
        );

        if (result != ESP_OK) {
            (void)storage_sd_service_close(
                &file
            );

            return web_files_api_send_download_error(
                request,
                result
            );
        }

        seek_remaining -= (uint64_t)step;
    }

    char *disposition =
        web_files_api_alloc_psram(
            WEB_FILE_DISPOSITION_MAX_LENGTH
        );

    if (disposition == NULL) {
        (void)storage_sd_service_close(&file);

        return web_files_api_send_download_error(
            request,
            ESP_ERR_NO_MEM
        );
    }

    result = web_files_api_build_content_disposition(
        path,
        disposition,
        WEB_FILE_DISPOSITION_MAX_LENGTH
    );

    if (result != ESP_OK) {
        free(disposition);
        (void)storage_sd_service_close(&file);

        return web_files_api_send_download_error(
            request,
            result
        );
    }

    web_files_api_download_reader_t *reader = NULL;

    result = web_files_api_download_reader_create(
        file,
        content_length,
        &reader
    );

    if (result != ESP_OK) {
        free(disposition);
        (void)storage_sd_service_close(&file);

        return web_files_api_send_download_error(
            request,
            result
        );
    }

    /*
     * The reader task now owns the file. The HTTP task remains the
     * sole owner of the request and sends every response chunk.
     */
    file = NULL;

    result = web_files_api_send_sd_download_header(
        request,
        disposition,
        content_length,
        file_size,
        range_start,
        range_result > 0
    );

    if (result != ESP_OK) {
        atomic_store_explicit(
            &reader->stop_requested,
            true,
            memory_order_release
        );

        if (xSemaphoreTake(
                reader->stopped,
                pdMS_TO_TICKS(
                    WEB_FILE_DOWNLOAD_STOP_TIMEOUT_MS
                )
            ) == pdTRUE) {

            web_files_api_download_reader_destroy(
                reader
            );
        } else {
            ESP_LOGE(
                TAG,
                "SD download reader did not stop "
                "after header send failure"
            );
        }

        free(disposition);

        return result;
    }

    while (result == ESP_OK) {
        web_files_api_download_block_t block;

        if (xQueueReceive(
                reader->ready_blocks,
                &block,
                portMAX_DELAY
            ) != pdTRUE) {

            result = ESP_FAIL;
            break;
        }

        if (block.end_of_file) {
            break;
        }

        if (block.result != ESP_OK) {
            result = block.result;
            break;
        }

        if ((block.data == NULL) ||
            (block.size == 0U)) {

            result = ESP_ERR_INVALID_RESPONSE;
            break;
        }

        result = web_files_api_send_all(
            request,
            block.data,
            block.size
        );

        if (xQueueSend(
                reader->free_buffers,
                &block.data,
                0U
            ) != pdTRUE) {

            if (result == ESP_OK) {
                result = ESP_FAIL;
            }
        }
    }

    atomic_store_explicit(
        &reader->stop_requested,
        true,
        memory_order_release
    );

    const BaseType_t reader_stopped =
        xSemaphoreTake(
            reader->stopped,
            pdMS_TO_TICKS(
                WEB_FILE_DOWNLOAD_STOP_TIMEOUT_MS
            )
        );

    if (reader_stopped != pdTRUE) {
        ESP_LOGE(
            TAG,
            "SD download reader did not stop in time"
        );

        free(disposition);

        /*
         * The running reader still owns its context, queues, buffers
         * and file, so none of those resources may be released here.
         */
        return ESP_ERR_TIMEOUT;
    }

    const esp_err_t close_result =
        reader->close_result;

    if ((result == ESP_OK) &&
        (close_result != ESP_OK)) {

        result = close_result;
    }

    web_files_api_download_reader_destroy(reader);
    free(disposition);

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to send SD-card file '%s': %s",
            path,
            esp_err_to_name(result)
        );
    }

    return result;
}

static esp_err_t web_files_api_download_handler(
    httpd_req_t *request
)
{
    if (request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char volume[
        WEB_FILE_VOLUME_MAX_LENGTH
    ];

    char path[
        WEB_FILE_PATH_MAX_LENGTH
    ];

    const esp_err_t result =
        web_files_api_get_file_parameters(
            request,
            volume,
            sizeof(volume),
            path,
            sizeof(path)
        );

    if (result == ESP_ERR_NO_MEM) {
        return httpd_resp_send_err(
            request,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Insufficient memory"
        );
    }

    if (result != ESP_OK) {
        return httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Invalid download parameters"
        );
    }

    if (strcmp(volume, "internal") == 0) {
        return web_files_api_download_internal_file(
            request,
            path
        );
    }

    if (strcmp(volume, "sd") == 0) {
        return web_files_api_download_sd_file(
            request,
            path
        );
    }

    return httpd_resp_send_err(
        request,
        HTTPD_400_BAD_REQUEST,
        "Unknown storage volume"
    );
}

static esp_err_t web_files_api_files_handler(
    httpd_req_t *request
)
{
    if (request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t query_length =
        httpd_req_get_url_query_len(
            request
        );

    if ((query_length == 0U) ||
        (query_length >=
         WEB_FILE_QUERY_MAX_LENGTH)) {

        return web_api_send_message(
            request,
            "400 Bad Request",
            false,
            "Invalid query string"
        );
    }

    char *workspace =
        web_files_api_alloc_psram(
            WEB_FILE_QUERY_WORKSPACE_SIZE
        );

    if (workspace == NULL) {
        return web_api_send_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to allocate query workspace"
        );
    }

    char *query = workspace;

    char *encoded_path =
        workspace +
        WEB_FILE_QUERY_MAX_LENGTH;

    esp_err_t result =
        httpd_req_get_url_query_str(
            request,
            query,
            WEB_FILE_QUERY_MAX_LENGTH
        );

    if (result != ESP_OK) {
        free(workspace);
        return web_api_send_message(
            request,
            "400 Bad Request",
            false,
            "Failed to read query string"
        );
    }

    char volume[
        WEB_FILE_VOLUME_MAX_LENGTH
    ];

    result = httpd_query_key_value(
        query,
        "volume",
        volume,
        sizeof(volume)
    );

    if (result != ESP_OK) {
        free(workspace);
        return web_api_send_message(
            request,
            "400 Bad Request",
            false,
            "Missing volume"
        );
    }

    result = httpd_query_key_value(
        query,
        "path",
        encoded_path,
        WEB_FILE_QUERY_MAX_LENGTH
    );

    if (result != ESP_OK) {
        free(workspace);
        return web_api_send_message(
            request,
            "400 Bad Request",
            false,
            "Missing path"
        );
    }

    char path[
        WEB_FILE_PATH_MAX_LENGTH
    ];

    result = web_files_api_url_decode(
        encoded_path,
        path,
        sizeof(path)
    );

    if ((result != ESP_OK) ||
        (path[0] != '/')) {

        free(workspace);

        return web_api_send_message(
            request,
            "400 Bad Request",
            false,
            "Invalid path"
        );
    }

    size_t offset = 0U;
    size_t limit =
        WEB_FILE_LIST_DEFAULT_LIMIT;

    char parameter[32];

    result = httpd_query_key_value(
        query,
        "offset",
        parameter,
        sizeof(parameter)
    );

    if (result == ESP_OK) {
        result =
            web_files_api_parse_size(
                parameter,
                &offset
            );

        if (result != ESP_OK) {
            free(workspace);
            return web_api_send_message(
                request,
                "400 Bad Request",
                false,
                "Invalid offset"
            );
        }

    } else if (result != ESP_ERR_NOT_FOUND) {
        free(workspace);
        return web_api_send_message(
            request,
            "400 Bad Request",
            false,
            "Invalid offset"
        );
    }

    result = httpd_query_key_value(
        query,
        "limit",
        parameter,
        sizeof(parameter)
    );

    if (result == ESP_OK) {
        result =
            web_files_api_parse_size(
                parameter,
                &limit
            );

        if (result != ESP_OK) {
            free(workspace);
            return web_api_send_message(
                request,
                "400 Bad Request",
                false,
                "Invalid limit"
            );
        }

    } else if (result != ESP_ERR_NOT_FOUND) {
        free(workspace);
        return web_api_send_message(
            request,
            "400 Bad Request",
            false,
            "Invalid limit"
        );
    }

    if ((limit == 0U) ||
        (limit >
         WEB_FILE_LIST_MAX_LIMIT)) {
            
        free(workspace);
        return web_api_send_message(
            request,
            "400 Bad Request",
            false,
            "Invalid pagination parameters"
        );
    }

    free(workspace);
    workspace = NULL;

    storage_file_entry_t *entries =
        web_files_api_calloc_psram(
            limit,
            sizeof(*entries)
        );

    if (entries == NULL) {
        return web_api_send_message(
            request,
            "500 Internal Server Error",
            false,
            "Not enough memory"
        );
    }

    size_t entry_count = 0U;
    bool has_more = false;

    if (strcmp(volume, "internal") == 0) {
        if ((offset >
             STORAGE_LIST_MAX_RESULT_COUNT) ||
            (limit >
             STORAGE_LIST_MAX_RESULT_COUNT) ||
            (offset >
             STORAGE_LIST_MAX_RESULT_COUNT -
             limit)) {

            free(entries);

            return web_api_send_message(
                request,
                "400 Bad Request",
                false,
                "Internal storage offset is too large"
            );
        }

        char internal_path[
            WEB_FILE_PATH_MAX_LENGTH
        ];

        result =
            web_files_api_build_internal_path(
                path,
                internal_path,
                sizeof(internal_path)
            );

        if (result == ESP_OK) {
            result = storage_service_list(
                internal_path,
                offset,
                entries,
                limit,
                &entry_count,
                &has_more
            );
        }

    } else if (strcmp(volume, "sd") == 0) {
        if (offset >
            WEB_FILE_LIST_MAX_SD_OFFSET) {

            free(entries);

            return web_api_send_message(
                request,
                "400 Bad Request",
                false,
                "SD-card offset is too large"
            );
        }

        result = storage_sd_service_list(
            path,
            offset,
            entries,
            limit,
            &entry_count,
            &has_more
        );

    } else {
        free(entries);

        return web_api_send_message(
            request,
            "400 Bad Request",
            false,
            "Unknown storage volume"
        );
    }

    if (result != ESP_OK) {
        free(entries);

        const char *status;

        if ((result == ESP_ERR_INVALID_ARG) ||
            (result == ESP_ERR_INVALID_SIZE)) {

            status = "400 Bad Request";

        } else if (result == ESP_ERR_NOT_FOUND) {
            status = "404 Not Found";

        } else if (result == ESP_ERR_INVALID_STATE) {
            status = "409 Conflict";

        } else if (result == ESP_ERR_TIMEOUT) {
            status = "503 Service Unavailable";

        } else {
            status = "500 Internal Server Error";
        }

        return web_api_send_message(
            request,
            status,
            false,
            "Failed to list storage directory"
        );
    }

    cJSON *response =
        cJSON_CreateObject();

    cJSON *json_entries =
        response != NULL
            ? cJSON_AddArrayToObject(
                response,
                "entries"
            )
            : NULL;

    if ((response == NULL) ||
        (json_entries == NULL)) {

        cJSON_Delete(response);
        free(entries);

        return web_api_send_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to create JSON response"
        );
    }

    bool json_valid = true;

    json_valid = json_valid &&
        (cJSON_AddStringToObject(
            response,
            "volume",
            volume
        ) != NULL);

    json_valid = json_valid &&
        (cJSON_AddStringToObject(
            response,
            "path",
            path
        ) != NULL);

    json_valid = json_valid &&
        (cJSON_AddNumberToObject(
            response,
            "offset",
            (double)offset
        ) != NULL);

    json_valid = json_valid &&
        (cJSON_AddNumberToObject(
            response,
            "count",
            (double)entry_count
        ) != NULL);

    json_valid = json_valid &&
        (cJSON_AddBoolToObject(
            response,
            "has_more",
            has_more
        ) != NULL);

    for (size_t index = 0U;
         json_valid &&
         (index < entry_count);
         index++) {

        cJSON *json_entry =
            cJSON_CreateObject();

        if (json_entry == NULL) {
            json_valid = false;
            break;
        }

        const storage_file_entry_t *entry =
            &entries[index];

        const bool entry_valid =
            (cJSON_AddStringToObject(
                json_entry,
                "name",
                entry->name
            ) != NULL) &&
            (cJSON_AddStringToObject(
                json_entry,
                "type",
                entry->is_directory
                    ? "directory"
                    : "file"
            ) != NULL) &&
            (cJSON_AddNumberToObject(
                json_entry,
                "size",
                (double)entry->size
            ) != NULL);

        if (!entry_valid) {
            cJSON_Delete(json_entry);
            json_valid = false;
            break;
        }

        cJSON_AddItemToArray(
            json_entries,
            json_entry
        );
    }

    free(entries);

    if (!json_valid) {
        cJSON_Delete(response);

        return web_api_send_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to create JSON response"
        );
    }

    char *json =
        cJSON_PrintUnformatted(
            response
        );

    cJSON_Delete(response);

    if (json == NULL) {
        return web_api_send_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to serialize JSON response"
        );
    }

    httpd_resp_set_type(
        request,
        "application/json"
    );

    httpd_resp_set_hdr(
        request,
        "Cache-Control",
        "no-store"
    );

    const esp_err_t send_result =
        httpd_resp_send(
            request,
            json,
            HTTPD_RESP_USE_STRLEN
        );

    cJSON_free(json);

    return send_result;
}

esp_err_t web_files_api_register(
    httpd_handle_t server
)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    static const httpd_uri_t files_uri = {
        .uri = "/api/files",
        .method = HTTP_GET,
        .handler =
            web_files_api_files_handler,
        .user_ctx = NULL,
    };

    static const httpd_uri_t download_uri = {
        .uri = "/api/files/download",
        .method = HTTP_GET,
        .handler =
            web_files_api_download_handler,
        .user_ctx = NULL,
    };

    static const httpd_uri_t mutation_uri = {
        .uri = "/api/files",
        .method = HTTP_POST,
        .handler =
            web_files_api_mutation_handler,
        .user_ctx = NULL,
    };

    esp_err_t result =
        httpd_register_uri_handler(
            server,
            &files_uri
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to register GET /api/files: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        httpd_register_uri_handler(
            server,
            &download_uri
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to register GET /api/files/download: %s",
            esp_err_to_name(result)
        );
        return result;
    }

    result =
        httpd_register_uri_handler(
            server,
            &mutation_uri
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to register POST /api/files: %s",
            esp_err_to_name(result)
        );
    }

    return result;
}
