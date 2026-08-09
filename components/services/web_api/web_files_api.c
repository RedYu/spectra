#include "web_files_api.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

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

#define WEB_FILE_DOWNLOAD_BUFFER_SIZE   (2048U)

#define WEB_FILE_ENCODED_NAME_MAX_LENGTH \
    (WEB_FILE_PATH_MAX_LENGTH * 3U)

#define WEB_FILE_QUERY_WORKSPACE_SIZE \
    (WEB_FILE_QUERY_MAX_LENGTH * 2U)

static const char *TAG =
    "web_files_api";

typedef struct
{
    httpd_req_t *request;

    size_t bytes_sent;
    bool transfer_started;

} web_files_api_stream_context_t;

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

static esp_err_t web_files_api_download_sd_file(
    httpd_req_t *request,
    const char *path
)
{
    if ((request == NULL) ||
        (path == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    FILE *file = NULL;

    esp_err_t result =
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

    uint8_t *buffer =
        web_files_api_alloc_psram(
            WEB_FILE_DOWNLOAD_BUFFER_SIZE
        );

    if (buffer == NULL) {
        (void)storage_sd_service_close(
            &file
        );

        return web_files_api_send_download_error(
            request,
            ESP_ERR_NO_MEM
        );
    }

    char *disposition =
        web_files_api_alloc_psram(
            WEB_FILE_DISPOSITION_MAX_LENGTH
        );

    if (disposition == NULL) {
        free(buffer);

        (void)storage_sd_service_close(
            &file
        );

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
        free(buffer);

        (void)storage_sd_service_close(
            &file
        );

        return web_files_api_send_download_error(
            request,
            result
        );
    }

    while (result == ESP_OK) {
        size_t bytes_read = 0U;

        result = storage_sd_service_read(
            file,
            buffer,
            WEB_FILE_DOWNLOAD_BUFFER_SIZE,
            &bytes_read
        );

        if ((result != ESP_OK) ||
            (bytes_read == 0U)) {

            break;
        }

        result = httpd_resp_send_chunk(
            request,
            (const char *)buffer,
            bytes_read
        );
    }

    /*
     * Finish the chunked response only when all file data was sent
     * successfully.
     */
    if (result == ESP_OK) {
        result = httpd_resp_send_chunk(
            request,
            NULL,
            0U
        );
    }

    const esp_err_t close_result =
        storage_sd_service_close(
            &file
        );

    free(disposition);
    free(buffer);

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to send SD-card file '%s': %s",
            path,
            esp_err_to_name(result)
        );

        return result;
    }

    if (close_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to close SD-card file '%s': %s",
            path,
            esp_err_to_name(close_result)
        );

        return close_result;
    }

    return ESP_OK;
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
    }

    return result;
}
