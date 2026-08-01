#include "web_service.h"

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "cJSON.h"

#include "esp_http_server.h"
#include "esp_log.h"

#include "settings_model.h"
#include "settings_service.h"
#include "system_model.h"
#include "storage_service.h"
#include "storage_sd_service.h"
#include "storage_types.h"

#define WEB_SERVICE_JSON_BODY_MAX_SIZE  (512U)

#define WEB_FILE_LIST_DEFAULT_LIMIT     (16U)
#define WEB_FILE_LIST_MAX_LIMIT         (32U)
#define WEB_FILE_LIST_MAX_SD_OFFSET     (1024U)

#define WEB_FILE_QUERY_MAX_LENGTH       (1024U)
#define WEB_FILE_VOLUME_MAX_LENGTH      (16U)
#define WEB_FILE_PATH_MAX_LENGTH        (256U)

#define WEB_FILE_DISPOSITION_MAX_LENGTH \
    ((WEB_FILE_PATH_MAX_LENGTH * 4U) + 64U)

#define WEB_FILE_DOWNLOAD_BUFFER_SIZE  (2048U)

static const char *TAG = "web_service";

static httpd_handle_t s_server = NULL;

static bool web_service_is_filename_attr_char(
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

static esp_err_t web_service_encode_filename(
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

        if (web_service_is_filename_attr_char(
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

static esp_err_t web_service_build_content_disposition(
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

    /*
     * filename provides an ASCII fallback for older browsers.
     * filename* preserves the original UTF-8 filename.
     */
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

    char encoded[
        (WEB_FILE_PATH_MAX_LENGTH * 3U)
    ];

    const esp_err_t result =
        web_service_encode_filename(
            filename,
            encoded,
            sizeof(encoded)
        );

    if (result != ESP_OK) {
        return result;
    }

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

        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static esp_err_t web_service_send_json_message(
    httpd_req_t *request,
    const char *status,
    bool success,
    const char *message
)
{
    if ((request == NULL) ||
        (status == NULL) ||
        (message == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    cJSON *response =
        cJSON_CreateObject();

    if (response == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if ((cJSON_AddBoolToObject(
            response,
            "success",
            success
        ) == NULL) ||
        (cJSON_AddStringToObject(
            response,
            "message",
            message
        ) == NULL)) {

        cJSON_Delete(response);

        return ESP_ERR_NO_MEM;
    }

    char *json =
        cJSON_PrintUnformatted(
            response
        );

    cJSON_Delete(response);

    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    httpd_resp_set_status(
        request,
        status
    );

    httpd_resp_set_type(
        request,
        "application/json"
    );

    httpd_resp_set_hdr(
        request,
        "Cache-Control",
        "no-store"
    );

    const esp_err_t result =
        httpd_resp_send(
            request,
            json,
            HTTPD_RESP_USE_STRLEN
        );

    cJSON_free(json);

    return result;
}

static esp_err_t web_service_receive_json(
    httpd_req_t *request,
    char **out_json
)
{
    if ((request == NULL) ||
        (out_json == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *out_json = NULL;

    if ((request->content_len == 0U) ||
        (request->content_len >
         WEB_SERVICE_JSON_BODY_MAX_SIZE)) {

        return ESP_ERR_INVALID_SIZE;
    }

    char *json =
        malloc(
            request->content_len + 1U
        );

    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t received = 0U;

    while (received < request->content_len) {
        const int result =
            httpd_req_recv(
                request,
                json + received,
                request->content_len - received
            );

        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }

        if (result <= 0) {
            free(json);

            return ESP_FAIL;
        }

        received += (size_t)result;
    }

    json[received] = '\0';

    *out_json = json;

    return ESP_OK;
}

static int web_service_hex_value(
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

static esp_err_t web_service_url_decode(
    const char *source,
    char *destination,
    size_t destination_size
)
{
    if ((source == NULL) ||
        (destination == NULL) ||
        (destination_size == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    size_t source_index = 0U;
    size_t destination_index = 0U;

    while (source[source_index] != '\0') {
        char decoded;

        if (source[source_index] == '%') {
            if (source[source_index + 1U] == '\0') {
                return ESP_ERR_INVALID_ARG;
            }

            if (source[source_index + 2U] == '\0') {
                return ESP_ERR_INVALID_ARG;
            }

            const char high_character =
                source[source_index + 1U];

            const char low_character =
                source[source_index + 2U];

            const int high =
                web_service_hex_value(
                    high_character
                );

            const int low =
                web_service_hex_value(
                    low_character
                );

            if ((high < 0) ||
                (low < 0)) {

                return ESP_ERR_INVALID_ARG;
            }

            decoded =
                (char)((high << 4) | low);

            source_index += 3U;

            /*
             * Reject embedded null characters.
             */
            if (decoded == '\0') {
                return ESP_ERR_INVALID_ARG;
            }

        } else if (source[source_index] == '+') {
            decoded = ' ';
            source_index++;

        } else {
            decoded =
                source[source_index];

            source_index++;
        }

        if ((destination_index + 1U) >=
            destination_size) {

            return ESP_ERR_INVALID_SIZE;
        }

        destination[destination_index] =
            decoded;

        destination_index++;
    }

    destination[destination_index] = '\0';

    return ESP_OK;
}

static esp_err_t web_service_parse_size(
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

static esp_err_t web_service_build_internal_path(
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

static esp_err_t web_service_get_file_parameters(
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

    char query[
        WEB_FILE_QUERY_MAX_LENGTH
    ];

    esp_err_t result =
        httpd_req_get_url_query_str(
            request,
            query,
            sizeof(query)
        );

    if (result != ESP_OK) {
        return result;
    }

    result = httpd_query_key_value(
        query,
        "volume",
        volume,
        volume_size
    );

    if (result != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    char encoded_path[
        WEB_FILE_QUERY_MAX_LENGTH
    ];

    result = httpd_query_key_value(
        query,
        "path",
        encoded_path,
        sizeof(encoded_path)
    );

    if (result != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    result = web_service_url_decode(
        encoded_path,
        path,
        path_size
    );

    if ((result != ESP_OK) ||
        (path[0] != '/')) {

        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

static esp_err_t web_service_send_text_error(
    httpd_req_t *request,
    const char *status,
    const char *message
)
{
    httpd_resp_set_status(
        request,
        status
    );

    httpd_resp_set_type(
        request,
        "text/plain; charset=utf-8"
    );

    return httpd_resp_send(
        request,
        message,
        HTTPD_RESP_USE_STRLEN
    );
}

static esp_err_t web_service_send_download_error(
    httpd_req_t *request,
    esp_err_t error
)
{
    if (error == ESP_ERR_NOT_FOUND) {
        return web_service_send_text_error(
            request,
            "404 Not Found",
            "File not found"
        );
    }

    if ((error == ESP_ERR_INVALID_ARG) ||
        (error == ESP_ERR_INVALID_SIZE)) {

        return web_service_send_text_error(
            request,
            "400 Bad Request",
            "Invalid file path"
        );
    }

    if (error == ESP_ERR_INVALID_STATE) {
        return web_service_send_text_error(
            request,
            "409 Conflict",
            "Storage is not available"
        );
    }

    if (error == ESP_ERR_TIMEOUT) {
        return web_service_send_text_error(
            request,
            "503 Service Unavailable",
            "Storage is busy"
        );
    }

    return web_service_send_text_error(
        request,
        "500 Internal Server Error",
        "Failed to read file"
    );
}

static esp_err_t web_service_set_download_headers(
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
        web_service_build_content_disposition(
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

static esp_err_t web_service_download_internal_file(
    httpd_req_t *request,
    const char *path
)
{
    char internal_path[
        WEB_FILE_PATH_MAX_LENGTH
    ];

    esp_err_t result =
        web_service_build_internal_path(
            path,
            internal_path,
            sizeof(internal_path)
        );

    if (result != ESP_OK) {
        return web_service_send_download_error(
            request,
            result
        );
    }

    char *data = NULL;
    size_t data_size = 0U;

    result = storage_service_read_file(
        internal_path,
        &data,
        &data_size
    );

    if (result != ESP_OK) {
        return web_service_send_download_error(
            request,
            result
        );
    }

    char disposition[
        WEB_FILE_DISPOSITION_MAX_LENGTH
    ];

    result = web_service_set_download_headers(
        request,
        path,
        disposition,
        sizeof(disposition)
    );

    if (result != ESP_OK) {
        free(data);

        return web_service_send_download_error(
            request,
            result
        );
    }

    const esp_err_t send_result =
        httpd_resp_send(
            request,
            data,
            data_size
        );

    free(data);

    return send_result;
}

static esp_err_t web_service_download_sd_file(
    httpd_req_t *request,
    const char *path
)
{
    FILE *file = NULL;

    esp_err_t result =
        storage_sd_service_open(
            path,
            "rb",
            &file
        );

    if (result != ESP_OK) {
        return web_service_send_download_error(
            request,
            result
        );
    }

    uint8_t *buffer =
        malloc(
            WEB_FILE_DOWNLOAD_BUFFER_SIZE
        );

    if (buffer == NULL) {
        (void)storage_sd_service_close(
            &file
        );

        return web_service_send_download_error(
            request,
            ESP_ERR_NO_MEM
        );
    }

    char disposition[
        WEB_FILE_DISPOSITION_MAX_LENGTH
    ];

    result = web_service_set_download_headers(
        request,
        path,
        disposition,
        sizeof(disposition)
    );

    if (result != ESP_OK) {
        free(buffer);

        (void)storage_sd_service_close(
            &file
        );

        return web_service_send_download_error(
            request,
            result
        );
    }

    while (true) {
        size_t bytes_read = 0U;

        result = storage_sd_service_read(
            file,
            buffer,
            WEB_FILE_DOWNLOAD_BUFFER_SIZE,
            &bytes_read
        );

        if (result != ESP_OK) {
            break;
        }

        if (bytes_read == 0U) {
            break;
        }

        result = httpd_resp_send_chunk(
            request,
            (const char *)buffer,
            bytes_read
        );

        if (result != ESP_OK) {
            break;
        }
    }

    free(buffer);

    const esp_err_t close_result =
        storage_sd_service_close(
            &file
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to send SD-card file '%s': %s",
            path,
            esp_err_to_name(result)
        );

        /*
         * The HTTP response may already be partially transmitted,
         * so another error response cannot be sent safely.
         */
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

    return httpd_resp_send_chunk(
        request,
        NULL,
        0U
    );
}

static esp_err_t web_service_file_download_handler(
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
        web_service_get_file_parameters(
            request,
            volume,
            sizeof(volume),
            path,
            sizeof(path)
        );

    if (result != ESP_OK) {
        return httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Invalid download parameters"
        );
    }

    if (strcmp(volume, "internal") == 0) {
        return web_service_download_internal_file(
            request,
            path
        );
    }

    if (strcmp(volume, "sd") == 0) {
        return web_service_download_sd_file(
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

static esp_err_t web_service_files_handler(
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

        return web_service_send_json_message(
            request,
            "400 Bad Request",
            false,
            "Invalid query string"
        );
    }

    char query[
        WEB_FILE_QUERY_MAX_LENGTH
    ];

    esp_err_t result =
        httpd_req_get_url_query_str(
            request,
            query,
            sizeof(query)
        );

    if (result != ESP_OK) {
        return web_service_send_json_message(
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
        return web_service_send_json_message(
            request,
            "400 Bad Request",
            false,
            "Missing volume"
        );
    }

    char encoded_path[
        WEB_FILE_QUERY_MAX_LENGTH
    ];

    result = httpd_query_key_value(
        query,
        "path",
        encoded_path,
        sizeof(encoded_path)
    );

    if (result != ESP_OK) {
        return web_service_send_json_message(
            request,
            "400 Bad Request",
            false,
            "Missing path"
        );
    }

    char path[
        WEB_FILE_PATH_MAX_LENGTH
    ];

    result = web_service_url_decode(
        encoded_path,
        path,
        sizeof(path)
    );

    if ((result != ESP_OK) ||
        (path[0] != '/')) {

        return web_service_send_json_message(
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
            web_service_parse_size(
                parameter,
                &offset
            );

        if (result != ESP_OK) {
            return web_service_send_json_message(
                request,
                "400 Bad Request",
                false,
                "Invalid offset"
            );
        }

    } else if (result != ESP_ERR_NOT_FOUND) {
        return web_service_send_json_message(
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
            web_service_parse_size(
                parameter,
                &limit
            );

        if (result != ESP_OK) {
            return web_service_send_json_message(
                request,
                "400 Bad Request",
                false,
                "Invalid limit"
            );
        }

    } else if (result != ESP_ERR_NOT_FOUND) {
        return web_service_send_json_message(
            request,
            "400 Bad Request",
            false,
            "Invalid limit"
        );
    }

    if ((limit == 0U) ||
        (limit >
         WEB_FILE_LIST_MAX_LIMIT)) {

        return web_service_send_json_message(
            request,
            "400 Bad Request",
            false,
            "Invalid pagination parameters"
        );
    }

    storage_file_entry_t *entries =
        calloc(
            limit,
            sizeof(*entries)
        );

    if (entries == NULL) {
        return web_service_send_json_message(
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

            return web_service_send_json_message(
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
            web_service_build_internal_path(
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

            return web_service_send_json_message(
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

        return web_service_send_json_message(
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

        return web_service_send_json_message(
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

        return web_service_send_json_message(
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

        return web_service_send_json_message(
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
        return web_service_send_json_message(
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

static esp_err_t web_service_get_system_handler(
    httpd_req_t *request
)
{
    if (request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    system_model_t model;

    const esp_err_t model_result =
        system_model_get_snapshot(
            &model
        );

    if (model_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to get system model: %s",
            esp_err_to_name(model_result)
        );

        return web_service_send_json_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to get system information"
        );
    }

    cJSON *response =
        cJSON_CreateObject();

    if (response == NULL) {
        return ESP_ERR_NO_MEM;
    }

    bool valid = true;

    valid = valid &&
        (cJSON_AddStringToObject(
            response,
            "device_id",
            model.device_id
        ) != NULL);

    valid = valid &&
        (cJSON_AddStringToObject(
            response,
            "device_name",
            model.device_name
        ) != NULL);

    valid = valid &&
        (cJSON_AddStringToObject(
            response,
            "firmware_version",
            model.firmware_version
        ) != NULL);

    valid = valid &&
        (cJSON_AddStringToObject(
            response,
            "hardware_version",
            model.hardware_version
        ) != NULL);

    valid = valid &&
        (cJSON_AddStringToObject(
            response,
            "serial_number",
            model.serial_number
        ) != NULL);

    valid = valid &&
        (cJSON_AddNumberToObject(
            response,
            "uptime_sec",
            model.uptime_sec
        ) != NULL);

    valid = valid &&
        (cJSON_AddNumberToObject(
            response,
            "free_heap",
            model.free_heap
        ) != NULL);

    valid = valid &&
        (cJSON_AddNumberToObject(
            response,
            "minimum_free_heap",
            model.minimum_free_heap
        ) != NULL);

    valid = valid &&
        (cJSON_AddNumberToObject(
            response,
            "cpu_usage",
            model.cpu_usage
        ) != NULL);

    valid = valid &&
        (cJSON_AddBoolToObject(
            response,
            "storage_ready",
            model.storage_ready
        ) != NULL);

    valid = valid &&
        (cJSON_AddBoolToObject(
            response,
            "sd_card_mounted",
            model.sd_card_mounted
        ) != NULL);

    valid = valid &&
        (cJSON_AddBoolToObject(
            response,
            "ota_available",
            model.ota_available
        ) != NULL);

    if (!valid) {
        cJSON_Delete(response);

        return web_service_send_json_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to create system response"
        );
    }

    char *json =
        cJSON_PrintUnformatted(
            response
        );

    cJSON_Delete(response);

    if (json == NULL) {
        return web_service_send_json_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to serialize system response"
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

    const esp_err_t result =
        httpd_resp_send(
            request,
            json,
            HTTPD_RESP_USE_STRLEN
        );

    cJSON_free(json);

    return result;
}

static esp_err_t web_service_get_settings_handler(
    httpd_req_t *request
)
{
    if (request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    app_settings_t settings;

    const esp_err_t model_result =
        settings_model_get(
            &settings
        );

    if (model_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to get settings: %s",
            esp_err_to_name(model_result)
        );

        return web_service_send_json_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to get settings"
        );
    }

    cJSON *response =
        cJSON_CreateObject();

    if (response == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON *device =
        cJSON_AddObjectToObject(
            response,
            "device"
        );

    cJSON *display =
        cJSON_AddObjectToObject(
            response,
            "display"
        );

    cJSON *logging =
        cJSON_AddObjectToObject(
            response,
            "logging"
        );

    cJSON *ui =
        cJSON_AddObjectToObject(
            response,
            "ui"
        );

    if ((device == NULL) ||
        (display == NULL) ||
        (logging == NULL) ||
        (ui == NULL)) {

        cJSON_Delete(response);

        return ESP_ERR_NO_MEM;
    }

    bool valid = true;

    valid = valid &&
        (cJSON_AddStringToObject(
            device,
            "target",
            settings.device.target
        ) != NULL);

    valid = valid &&
        (cJSON_AddStringToObject(
            device,
            "name",
            settings.device.name
        ) != NULL);

    valid = valid &&
        (cJSON_AddNumberToObject(
            display,
            "brightness",
            settings.display.brightness
        ) != NULL);

    valid = valid &&
        (cJSON_AddBoolToObject(
            logging,
            "sd_enabled",
            settings.logging.sd_enabled
        ) != NULL);

    valid = valid &&
        (cJSON_AddBoolToObject(
            ui,
            "animations_enabled",
            settings.ui.animations_enabled
        ) != NULL);

    if (!valid) {
        cJSON_Delete(response);

        return ESP_ERR_NO_MEM;
    }

    char *json =
        cJSON_PrintUnformatted(
            response
        );

    cJSON_Delete(response);

    if (json == NULL) {
        return ESP_ERR_NO_MEM;
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

    const esp_err_t result =
        httpd_resp_send(
            request,
            json,
            HTTPD_RESP_USE_STRLEN
        );

    cJSON_free(json);

    return result;
}

static esp_err_t web_service_put_settings_handler(
    httpd_req_t *request
)
{
    if (request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char *json = NULL;

    esp_err_t result =
        web_service_receive_json(
            request,
            &json
        );

    if (result != ESP_OK) {
        return web_service_send_json_message(
            request,
            "400 Bad Request",
            false,
            "Invalid request body"
        );
    }

    cJSON *root =
        cJSON_Parse(json);

    free(json);

    if (root == NULL) {
        return web_service_send_json_message(
            request,
            "400 Bad Request",
            false,
            "Invalid JSON"
        );
    }

    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);

        return web_service_send_json_message(
            request,
            "400 Bad Request",
            false,
            "JSON root must be an object"
        );
    }

    const cJSON *display =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "display"
        );

    if (display != NULL) {
        if (!cJSON_IsObject(display)) {
            result = ESP_ERR_INVALID_ARG;
            goto invalid_request;
        }

        const cJSON *brightness =
            cJSON_GetObjectItemCaseSensitive(
                display,
                "brightness"
            );

        if (brightness != NULL) {
            if (!cJSON_IsNumber(brightness) ||
                (brightness->valuedouble !=
                (double)brightness->valueint) ||
                (brightness->valueint <
                (int)SETTINGS_DISPLAY_BRIGHTNESS_MIN) ||
                (brightness->valueint >
                (int)SETTINGS_DISPLAY_BRIGHTNESS_MAX)) {

                result = ESP_ERR_INVALID_ARG;
                goto invalid_request;
            }

            result =
                settings_service_set_brightness(
                    (uint8_t)brightness->valueint
                );

            if (result != ESP_OK) {
                goto service_error;
            }
        }
    }

    const cJSON *logging =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "logging"
        );

    if (logging != NULL) {
        if (!cJSON_IsObject(logging)) {
            result = ESP_ERR_INVALID_ARG;
            goto invalid_request;
        }

        const cJSON *sd_enabled =
            cJSON_GetObjectItemCaseSensitive(
                logging,
                "sd_enabled"
            );

        if (sd_enabled != NULL) {
            if (!cJSON_IsBool(sd_enabled)) {
                result = ESP_ERR_INVALID_ARG;
                goto invalid_request;
            }

            result =
                settings_service_set_sd_logging_enabled(
                    cJSON_IsTrue(sd_enabled)
                );

            if (result != ESP_OK) {
                goto service_error;
            }
        }
    }

    const cJSON *ui =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "ui"
        );

    if (ui != NULL) {
        if (!cJSON_IsObject(ui)) {
            result = ESP_ERR_INVALID_ARG;
            goto invalid_request;
        }

        const cJSON *animations_enabled =
            cJSON_GetObjectItemCaseSensitive(
                ui,
                "animations_enabled"
            );

        if (animations_enabled != NULL) {
            if (!cJSON_IsBool(
                    animations_enabled
                )) {

                result = ESP_ERR_INVALID_ARG;
                goto invalid_request;
            }

            result =
                settings_service_set_animations_enabled(
                    cJSON_IsTrue(
                        animations_enabled
                    )
                );

            if (result != ESP_OK) {
                goto service_error;
            }
        }
    }

    cJSON_Delete(root);

    return web_service_send_json_message(
        request,
        "200 OK",
        true,
        "Settings applied"
    );

invalid_request:
    cJSON_Delete(root);

    return web_service_send_json_message(
        request,
        "400 Bad Request",
        false,
        "Invalid settings"
    );

service_error:
    ESP_LOGE(
        TAG,
        "Failed to apply settings: %s",
        esp_err_to_name(result)
    );

    cJSON_Delete(root);

    return web_service_send_json_message(
        request,
        "500 Internal Server Error",
        false,
        "Failed to apply settings"
    );
}

static esp_err_t web_service_save_settings_handler(
    httpd_req_t *request
)
{
    if (request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t result =
        settings_service_save();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to save settings: %s",
            esp_err_to_name(result)
        );

        return web_service_send_json_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to save settings"
        );
    }

    return web_service_send_json_message(
        request,
        "200 OK",
        true,
        "Settings saved"
    );
}

static esp_err_t web_service_send_storage_file(
    httpd_req_t *request,
    const char *path,
    const char *content_type
)
{
    if ((request == NULL) ||
        (path == NULL) ||
        (content_type == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    char *data = NULL;
    size_t data_size = 0U;

    const esp_err_t result =
        storage_service_read_file(
            path,
            &data,
            &data_size
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to read web resource '%s': %s",
            path,
            esp_err_to_name(result)
        );

        const httpd_err_code_t http_error =
            result == ESP_ERR_NOT_FOUND
                ? HTTPD_404_NOT_FOUND
                : HTTPD_500_INTERNAL_SERVER_ERROR;

        return httpd_resp_send_err(
            request,
            http_error,
            result == ESP_ERR_NOT_FOUND
                ? "Resource not found"
                : "Failed to read web resource"
        );
    }

    httpd_resp_set_type(
        request,
        content_type
    );

    httpd_resp_set_hdr(
        request,
        "Cache-Control",
        "no-store"
    );

    /*
     * Prevent browsers from interpreting a resource as another
     * content type.
     */
    httpd_resp_set_hdr(
        request,
        "X-Content-Type-Options",
        "nosniff"
    );

    /*
     * httpd_resp_send() completes transmission before returning,
     * so the allocated buffer can be released afterwards.
     */
    const esp_err_t send_result =
        httpd_resp_send(
            request,
            data,
            data_size
        );

    free(data);

    return send_result;
}

static esp_err_t web_service_root_handler(
    httpd_req_t *request
)
{
    return web_service_send_storage_file(
        request,
        "/storage/www/index.html",
        "text/html; charset=utf-8"
    );
}

static esp_err_t web_service_files_page_handler(
    httpd_req_t *request
)
{
    return web_service_send_storage_file(
        request,
        "/storage/www/files.html",
        "text/html; charset=utf-8"
    );
}

static esp_err_t web_service_files_script_handler(
    httpd_req_t *request
)
{
    return web_service_send_storage_file(
        request,
        "/storage/www/files.js",
        "application/javascript; charset=utf-8"
    );
}

esp_err_t web_service_start(void)
{
    if (s_server != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_config_t config =
        HTTPD_DEFAULT_CONFIG();

    config.server_port = 80U;
    config.ctrl_port = 32768U;
    config.max_uri_handlers = 12U;
    config.stack_size = 8192U;

    esp_err_t result =
        httpd_start(
            &s_server,
            &config
        );

    if (result != ESP_OK) {
        s_server = NULL;

        ESP_LOGE(
            TAG,
            "Failed to start HTTP server: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    static const httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler =
            web_service_root_handler,
        .user_ctx = NULL,
    };

    static const httpd_uri_t files_page_uri = {
        .uri = "/files",
        .method = HTTP_GET,
        .handler =
            web_service_files_page_handler,
        .user_ctx = NULL,
    };

    static const httpd_uri_t files_script_uri = {
        .uri = "/files.js",
        .method = HTTP_GET,
        .handler =
            web_service_files_script_handler,
        .user_ctx = NULL,
    };

    static const httpd_uri_t get_settings_uri = {
        .uri = "/api/settings",
        .method = HTTP_GET,
        .handler =
            web_service_get_settings_handler,
        .user_ctx = NULL,
    };

    static const httpd_uri_t put_settings_uri = {
        .uri = "/api/settings",
        .method = HTTP_PUT,
        .handler =
            web_service_put_settings_handler,
        .user_ctx = NULL,
    };

    static const httpd_uri_t save_settings_uri = {
        .uri = "/api/settings/save",
        .method = HTTP_POST,
        .handler =
            web_service_save_settings_handler,
        .user_ctx = NULL,
    };

    static const httpd_uri_t get_system_uri = {
        .uri = "/api/system",
        .method = HTTP_GET,
        .handler =
            web_service_get_system_handler,
        .user_ctx = NULL,
    };

    static const httpd_uri_t files_uri = {
        .uri = "/api/files",
        .method = HTTP_GET,
        .handler =
            web_service_files_handler,
        .user_ctx = NULL,
    };

    static const httpd_uri_t file_download_uri = {
        .uri = "/api/files/download",
        .method = HTTP_GET,
        .handler =
            web_service_file_download_handler,
        .user_ctx = NULL,
    };

    result = httpd_register_uri_handler(
        s_server,
        &root_uri
    );

    if (result != ESP_OK) {
        goto registration_failed;
    }

    result = httpd_register_uri_handler(
        s_server,
        &files_page_uri
    );

    if (result != ESP_OK) {
        goto registration_failed;
    }

    result = httpd_register_uri_handler(
        s_server,
        &files_script_uri
    );

    if (result != ESP_OK) {
        goto registration_failed;
    }

    result = httpd_register_uri_handler(
        s_server,
        &get_settings_uri
    );

    if (result != ESP_OK) {
        goto registration_failed;
    }

    result = httpd_register_uri_handler(
        s_server,
        &put_settings_uri
    );

    if (result != ESP_OK) {
        goto registration_failed;
    }

    result = httpd_register_uri_handler(
        s_server,
        &save_settings_uri
    );

    if (result != ESP_OK) {
        goto registration_failed;
    }

    result = httpd_register_uri_handler(
        s_server,
        &get_system_uri
    );

    if (result != ESP_OK) {
        goto registration_failed;
    }

    result = httpd_register_uri_handler(
        s_server,
        &files_uri
    );

    if (result != ESP_OK) {
        goto registration_failed;
    }
    
    result = httpd_register_uri_handler(
        s_server,
        &file_download_uri
    );

    if (result != ESP_OK) {
        goto registration_failed;
    }

    ESP_LOGI(
        TAG,
        "Web server started at http://spectra.device"
    );

    return ESP_OK;

registration_failed:
    ESP_LOGE(
        TAG,
        "Failed to register HTTP handler: %s",
        esp_err_to_name(result)
    );

    (void)httpd_stop(
        s_server
    );

    s_server = NULL;

    return result;
}

esp_err_t web_service_stop(void)
{
    if (s_server == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result =
        httpd_stop(
            s_server
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to stop HTTP server: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    s_server = NULL;

    ESP_LOGI(
        TAG,
        "Web server stopped"
    );

    return ESP_OK;
}
