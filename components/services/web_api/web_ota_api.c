/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "web_ota_api.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"

#include "battery_service.h"
#include "can_logger_service.h"
#include "internet_service.h"
#include "ota_service.h"
#include "storage_sd_service.h"
#include "storage_types.h"
#include "system_service.h"
#include "web_api_common.h"

#define WEB_OTA_RECEIVE_BUFFER_SIZE       (4U * 1024U)
#define WEB_OTA_MINIMUM_BATTERY_PERCENT   (20U)
#define WEB_OTA_RESTART_DELAY_MS          (1500U)
#define WEB_OTA_QUERY_MAX_LENGTH          (64U)
#define WEB_OTA_MAX_RECEIVE_TIMEOUTS       (3U)
#define WEB_OTA_SD_FILE_LIMIT             (32U)
#define WEB_OTA_SD_PATH_MAX_LENGTH \
    (STORAGE_FILE_NAME_MAX_LENGTH + 16U)

static const char *TAG = "web_ota_api";

static bool web_ota_api_get_query_value(
    httpd_req_t *request,
    const char *key,
    char *value,
    size_t value_size
);

static const char *web_ota_api_state_name(
    ota_service_state_t state
)
{
    switch (state) {
        case OTA_SERVICE_STATE_UNINITIALIZED:
            return "uninitialized";

        case OTA_SERVICE_STATE_IDLE:
            return "idle";

        case OTA_SERVICE_STATE_RECEIVING:
            return "receiving";

        case OTA_SERVICE_STATE_READY:
            return "ready";

        case OTA_SERVICE_STATE_ERROR:
            return "error";

        default:
            return "unknown";
    }
}

static const char *web_ota_api_image_state_name(
    ota_service_image_state_t state
)
{
    switch (state) {
        case OTA_SERVICE_IMAGE_STATE_NEW:
            return "new";

        case OTA_SERVICE_IMAGE_STATE_PENDING_VERIFY:
            return "pending_verify";

        case OTA_SERVICE_IMAGE_STATE_VALID:
            return "valid";

        case OTA_SERVICE_IMAGE_STATE_INVALID:
            return "invalid";

        case OTA_SERVICE_IMAGE_STATE_ABORTED:
            return "aborted";

        case OTA_SERVICE_IMAGE_STATE_UNKNOWN:
        default:
            return "unknown";
    }
}

static const char *web_ota_api_backend_state_name(
    ota_service_backend_state_t state
)
{
    switch (state) {
        case OTA_SERVICE_BACKEND_STATE_CHECKING:
            return "checking";

        case OTA_SERVICE_BACKEND_STATE_NO_UPDATE:
            return "no_update";

        case OTA_SERVICE_BACKEND_STATE_UPDATE_AVAILABLE:
            return "update_available";

        case OTA_SERVICE_BACKEND_STATE_ERROR:
            return "error";

        case OTA_SERVICE_BACKEND_STATE_NOT_CHECKED:
        default:
            return "not_checked";
    }
}

static esp_err_t web_ota_api_send_info(
    httpd_req_t *request
)
{
    ota_service_info_t info;

    const esp_err_t info_result =
        ota_service_get_info(&info);

    if (info_result != ESP_OK) {
        return web_api_send_message(
            request,
            "503 Service Unavailable",
            false,
            esp_err_to_name(info_result)
        );
    }

    cJSON *response = cJSON_CreateObject();

    if (response == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const bool valid =
        (cJSON_AddBoolToObject(
            response,
            "initialized",
            info.initialized
        ) != NULL) &&
        (cJSON_AddStringToObject(
            response,
            "state",
            web_ota_api_state_name(info.state)
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            response,
            "rollback_enabled",
            info.rollback_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            response,
            "verification_pending",
            info.verification_pending
        ) != NULL) &&
        (cJSON_AddStringToObject(
            response,
            "running_image_state",
            web_ota_api_image_state_name(
                info.running_image_state
            )
        ) != NULL) &&
        (cJSON_AddStringToObject(
            response,
            "backend_state",
            web_ota_api_backend_state_name(
                info.backend_state
            )
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "backend_last_check_ms",
            (double)info.backend_last_check_ms
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "backend_last_error",
            info.backend_last_error
        ) != NULL) &&
        (cJSON_AddStringToObject(
            response,
            "backend_last_error_name",
            esp_err_to_name(info.backend_last_error)
        ) != NULL) &&
        (cJSON_AddStringToObject(
            response,
            "backend_version",
            info.backend_version
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "image_size",
            (double)info.image_size
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "written_size",
            (double)info.written_size
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "progress_percent",
            info.progress_percent
        ) != NULL) &&
        (cJSON_AddStringToObject(
            response,
            "running_partition",
            info.running_partition
        ) != NULL) &&
        (cJSON_AddStringToObject(
            response,
            "boot_partition",
            info.boot_partition
        ) != NULL) &&
        (cJSON_AddStringToObject(
            response,
            "update_partition",
            info.update_partition
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "update_partition_address",
            info.update_partition_address
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "update_partition_size",
            (double)info.update_partition_size
        ) != NULL) &&
        (cJSON_AddStringToObject(
            response,
            "project_name",
            info.project_name
        ) != NULL) &&
        (cJSON_AddStringToObject(
            response,
            "version",
            info.version
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "last_error",
            info.last_error
        ) != NULL) &&
        (cJSON_AddStringToObject(
            response,
            "last_error_name",
            esp_err_to_name(info.last_error)
        ) != NULL);

    if (!valid) {
        cJSON_Delete(response);

        return ESP_ERR_NO_MEM;
    }

    const esp_err_t result =
        web_api_send_json(
            request,
            response
        );

    cJSON_Delete(response);

    return result;
}

static esp_err_t web_ota_api_check_upload_allowed(
    httpd_req_t *request,
    bool *allowed
)
{
    if ((request == NULL) ||
        (allowed == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *allowed = false;

    can_logger_info_t logger_info;

    if (can_logger_service_get_info(
            &logger_info
        ) == ESP_OK) {

        if ((logger_info.state ==
             CAN_LOGGER_STATE_STARTING) ||
            (logger_info.state ==
             CAN_LOGGER_STATE_RECORDING) ||
            (logger_info.state ==
             CAN_LOGGER_STATE_STOPPING)) {

            return web_api_send_message(
                request,
                "409 Conflict",
                false,
                "Stop CAN recording before firmware update"
            );
        }
    }

    battery_service_info_t battery_info;

    if ((battery_service_get_info(
             &battery_info
         ) == ESP_OK) &&
        battery_info.measurement_valid &&
        battery_info.battery_present &&
        (battery_info.level_percent <
         WEB_OTA_MINIMUM_BATTERY_PERCENT)) {

        return web_api_send_message(
            request,
            "409 Conflict",
            false,
            "Battery level is below 20 percent"
        );
    }

    *allowed = true;

    return ESP_OK;
}

static esp_err_t web_ota_api_upload(
    httpd_req_t *request
)
{
    if (request->content_len == 0U) {
        return web_api_send_message(
            request,
            "400 Bad Request",
            false,
            "Firmware image is empty"
        );
    }

    bool upload_allowed = false;

    esp_err_t result =
        web_ota_api_check_upload_allowed(
            request,
            &upload_allowed
        );

    if ((result != ESP_OK) ||
        !upload_allowed) {

        return result;
    }

    result = ota_service_begin(
        request->content_len
    );

    if (result != ESP_OK) {
        const char *status =
            result == ESP_ERR_INVALID_SIZE
                ? "413 Payload Too Large"
                : result == ESP_ERR_INVALID_STATE
                    ? "409 Conflict"
                    : "500 Internal Server Error";

        return web_api_send_message(
            request,
            status,
            false,
            esp_err_to_name(result)
        );
    }

    uint8_t *buffer = malloc(
        WEB_OTA_RECEIVE_BUFFER_SIZE
    );

    if (buffer == NULL) {
        (void)ota_service_cancel();

        return web_api_send_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to allocate OTA receive buffer"
        );
    }

    size_t remaining = request->content_len;
    uint32_t receive_timeouts = 0U;

    while ((result == ESP_OK) &&
           (remaining > 0U)) {

        const size_t requested =
            remaining < WEB_OTA_RECEIVE_BUFFER_SIZE
                ? remaining
                : WEB_OTA_RECEIVE_BUFFER_SIZE;

        const int received = httpd_req_recv(
            request,
            (char *)buffer,
            requested
        );

        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            ++receive_timeouts;

            if (receive_timeouts >=
                WEB_OTA_MAX_RECEIVE_TIMEOUTS) {

                result = ESP_ERR_TIMEOUT;
            }

            continue;
        }

        if (received <= 0) {
            result = ESP_FAIL;
            break;
        }

        result = ota_service_write(
            buffer,
            (size_t)received
        );

        receive_timeouts = 0U;
        remaining -= (size_t)received;
    }

    free(buffer);

    if (result == ESP_OK) {
        result = ota_service_finish();
    }

    if (result != ESP_OK) {
        (void)ota_service_cancel();

        ESP_LOGE(
            TAG,
            "OTA upload failed: %s",
            esp_err_to_name(result)
        );

        if (remaining > 0U) {
            return result;
        }

        return web_api_send_message(
            request,
            "400 Bad Request",
            false,
            esp_err_to_name(result)
        );
    }

    ESP_LOGI(
        TAG,
        "OTA upload completed: %u bytes",
        (unsigned int)request->content_len
    );

    return web_ota_api_send_info(request);
}

static bool web_ota_api_file_has_bin_extension(
    const char *name
)
{
    if (name == NULL) {
        return false;
    }

    const size_t length = strlen(name);

    if (length < 5U) {
        return false;
    }

    const char *extension = name + length - 4U;

    return
        (extension[0] == '.') &&
        ((extension[1] == 'b') ||
         (extension[1] == 'B')) &&
        ((extension[2] == 'i') ||
         (extension[2] == 'I')) &&
        ((extension[3] == 'n') ||
         (extension[3] == 'N'));
}

static esp_err_t web_ota_api_send_sd_files(
    httpd_req_t *request
)
{
    esp_err_t result =
        storage_sd_service_ensure_directory(
            OTA_SERVICE_SD_UPDATE_DIRECTORY
        );

    if (result != ESP_OK) {
        return web_api_send_message(
            request,
            "503 Service Unavailable",
            false,
            esp_err_to_name(result)
        );
    }

    storage_file_entry_t *entries =
        calloc(
            WEB_OTA_SD_FILE_LIMIT,
            sizeof(*entries)
        );

    if (entries == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t entry_count = 0U;
    bool has_more = false;

    result = storage_sd_service_list(
        OTA_SERVICE_SD_UPDATE_DIRECTORY,
        0U,
        entries,
        WEB_OTA_SD_FILE_LIMIT,
        &entry_count,
        &has_more
    );

    if (result != ESP_OK) {
        free(entries);

        return web_api_send_message(
            request,
            "503 Service Unavailable",
            false,
            esp_err_to_name(result)
        );
    }

    cJSON *response = cJSON_CreateObject();
    cJSON *files = cJSON_CreateArray();

    if ((response == NULL) ||
        (files == NULL)) {

        cJSON_Delete(response);
        cJSON_Delete(files);
        free(entries);

        return ESP_ERR_NO_MEM;
    }

    cJSON_AddItemToObject(response, "files", files);

    bool valid =
        (cJSON_AddStringToObject(
            response,
            "directory",
            OTA_SERVICE_SD_UPDATE_DIRECTORY
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            response,
            "truncated",
            has_more
        ) != NULL);

    for (size_t index = 0U;
         valid && (index < entry_count);
         ++index) {

        if (entries[index].is_directory ||
            !web_ota_api_file_has_bin_extension(
                entries[index].name
            )) {

            continue;
        }

        cJSON *file = cJSON_CreateObject();

        if (file == NULL) {
            valid = false;
            break;
        }

        valid =
            (cJSON_AddStringToObject(
                file,
                "name",
                entries[index].name
            ) != NULL) &&
            (cJSON_AddNumberToObject(
                file,
                "size",
                (double)entries[index].size
            ) != NULL);

        if (!valid) {
            cJSON_Delete(file);
            break;
        }

        cJSON_AddItemToArray(files, file);
    }

    free(entries);

    if (!valid) {
        cJSON_Delete(response);
        return ESP_ERR_NO_MEM;
    }

    result = web_api_send_json(
        request,
        response
    );

    cJSON_Delete(response);

    return result;
}

static bool web_ota_api_get_query_value(
    httpd_req_t *request,
    const char *key,
    char *value,
    size_t value_size
)
{
    const size_t query_length =
        httpd_req_get_url_query_len(request);

    if ((query_length == 0U) ||
        (query_length >= WEB_OTA_QUERY_MAX_LENGTH)) {

        return false;
    }

    char query[WEB_OTA_QUERY_MAX_LENGTH];

    if (httpd_req_get_url_query_str(
            request,
            query,
            sizeof(query)
        ) != ESP_OK) {

        return false;
    }

    return httpd_query_key_value(
        query,
        key,
        value,
        value_size
    ) == ESP_OK;
}

static bool web_ota_api_get_action(
    httpd_req_t *request,
    char *action,
    size_t action_size
)
{
    return web_ota_api_get_query_value(
        request,
        "action",
        action,
        action_size
    );
}

static esp_err_t web_ota_api_get_handler(
    httpd_req_t *request
)
{
    char action[16] = {0};

    if (web_ota_api_get_action(
            request,
            action,
            sizeof(action)
        ) &&
        (strcmp(action, "files") == 0)) {

        return web_ota_api_send_sd_files(request);
    }

    return web_ota_api_send_info(request);
}

static esp_err_t web_ota_api_post_handler(
    httpd_req_t *request
)
{
    char action[16] = {0};

    if (!web_ota_api_get_action(
            request,
            action,
            sizeof(action)
        )) {

        return web_ota_api_upload(request);
    }

    if (strcmp(action, "cancel") == 0) {
        const esp_err_t result =
            ota_service_cancel();

        return web_api_send_message(
            request,
            result == ESP_OK
                ? "200 OK"
                : "500 Internal Server Error",
            result == ESP_OK,
            result == ESP_OK
                ? "OTA update cancelled"
                : esp_err_to_name(result)
        );
    }

    if (strcmp(action, "restart") == 0) {
        ota_service_info_t info;

        if ((ota_service_get_info(&info) != ESP_OK) ||
            (info.state != OTA_SERVICE_STATE_READY)) {

            return web_api_send_message(
                request,
                "409 Conflict",
                false,
                "No validated OTA image is ready"
            );
        }

        const esp_err_t result =
            system_service_schedule_restart(
                WEB_OTA_RESTART_DELAY_MS
            );

        return web_api_send_message(
            request,
            result == ESP_OK
                ? "202 Accepted"
                : "409 Conflict",
            result == ESP_OK,
            result == ESP_OK
                ? "Restart scheduled"
                : esp_err_to_name(result)
        );
    }

    if (strcmp(action, "install-sd") == 0) {
        if ((request->content_len == 0U) ||
            (request->content_len >=
             WEB_OTA_SD_PATH_MAX_LENGTH)) {

            return web_api_send_message(
                request,
                "400 Bad Request",
                false,
                "Invalid SD firmware path length"
            );
        }

        char path[WEB_OTA_SD_PATH_MAX_LENGTH] = {0};
        size_t received_size = 0U;
        uint32_t receive_timeouts = 0U;
        esp_err_t result = ESP_OK;

        while ((result == ESP_OK) &&
               (received_size < request->content_len)) {

            const int received = httpd_req_recv(
                request,
                path + received_size,
                request->content_len - received_size
            );

            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                ++receive_timeouts;

                if (receive_timeouts >=
                    WEB_OTA_MAX_RECEIVE_TIMEOUTS) {

                    result = ESP_ERR_TIMEOUT;
                }

                continue;
            }

            if (received <= 0) {
                result = ESP_FAIL;
                break;
            }

            receive_timeouts = 0U;
            received_size += (size_t)received;
        }

        if ((result == ESP_OK) &&
            (memchr(path, '\0', received_size) != NULL)) {

            result = ESP_ERR_INVALID_ARG;
        }

        path[received_size] = '\0';

        bool install_allowed = false;

        if (result == ESP_OK) {
            result = web_ota_api_check_upload_allowed(
                request,
                &install_allowed
            );
        }

        if ((result != ESP_OK) ||
            !install_allowed) {

            return result;
        }

        result = ota_service_install_from_sd(path);

        if (result != ESP_OK) {
            return web_api_send_message(
                request,
                result == ESP_ERR_INVALID_ARG
                    ? "400 Bad Request"
                    : result == ESP_ERR_INVALID_STATE
                        ? "409 Conflict"
                        : "500 Internal Server Error",
                false,
                esp_err_to_name(result)
            );
        }

        return web_ota_api_send_info(request);
    }

    if (strcmp(action, "check-backend") == 0) {
        const esp_err_t result =
            internet_service_request_check();

        return web_api_send_message(
            request,
            result == ESP_OK
                ? "202 Accepted"
                : "503 Service Unavailable",
            result == ESP_OK,
            result == ESP_OK
                ? "Backend firmware check requested"
                : esp_err_to_name(result)
        );
    }

    return web_api_send_message(
        request,
        "400 Bad Request",
        false,
        "Unknown OTA action"
    );
}

esp_err_t web_ota_api_register(
    httpd_handle_t server
)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    static const httpd_uri_t get_uri = {
        .uri = "/api/ota",
        .method = HTTP_GET,
        .handler = web_ota_api_get_handler,
        .user_ctx = NULL,
    };

    static const httpd_uri_t post_uri = {
        .uri = "/api/ota",
        .method = HTTP_POST,
        .handler = web_ota_api_post_handler,
        .user_ctx = NULL,
    };

    esp_err_t result = httpd_register_uri_handler(
        server,
        &get_uri
    );

    if (result != ESP_OK) {
        return result;
    }

    result = httpd_register_uri_handler(
        server,
        &post_uri
    );

    if (result != ESP_OK) {
        (void)httpd_unregister_uri_handler(
            server,
            get_uri.uri,
            get_uri.method
        );
    }

    return result;
}
