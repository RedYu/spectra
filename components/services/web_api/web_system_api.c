/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "web_system_api.h"

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "cJSON.h"
#include "esp_log.h"

#include "system_model.h"
#include "storage_sd_service.h"
#include "system_service.h"
#include "web_api_common.h"
#include "time_service.h"

#define WEB_SYSTEM_RESTART_DELAY_MS  (500U)

static const char *TAG =
    "web_system_api";

static const char *web_system_api_reset_reason_name(
    esp_reset_reason_t reason
)
{
    switch (reason) {
        case ESP_RST_POWERON:
            return "power_on";

        case ESP_RST_EXT:
            return "external";

        case ESP_RST_SW:
            return "software";

        case ESP_RST_PANIC:
            return "panic";

        case ESP_RST_INT_WDT:
            return "interrupt_watchdog";

        case ESP_RST_TASK_WDT:
            return "task_watchdog";

        case ESP_RST_WDT:
            return "watchdog";

        case ESP_RST_DEEPSLEEP:
            return "deep_sleep";

        case ESP_RST_BROWNOUT:
            return "brownout";

        case ESP_RST_SDIO:
            return "sdio";

        case ESP_RST_USB:
            return "usb";

        case ESP_RST_JTAG:
            return "jtag";

        case ESP_RST_EFUSE:
            return "efuse";

        case ESP_RST_PWR_GLITCH:
            return "power_glitch";

        case ESP_RST_CPU_LOCKUP:
            return "cpu_lockup";

        case ESP_RST_UNKNOWN:
        default:
            return "unknown";
    }
}

static bool web_system_api_add_time(
    cJSON *response
)
{
    if (response == NULL) {
        return false;
    }

    time_service_info_t info = {0};

    if (time_service_get_info(&info) != ESP_OK) {
        return false;
    }

    cJSON *time_object =
        cJSON_CreateObject();

    if (time_object == NULL) {
        return false;
    }

    bool valid = true;

    valid = valid &&
        (cJSON_AddBoolToObject(
            time_object,
            "service_running",
            info.running
        ) != NULL);

    valid = valid &&
        (cJSON_AddBoolToObject(
            time_object,
            "valid",
            info.time_valid
        ) != NULL);

    valid = valid &&
        (cJSON_AddBoolToObject(
            time_object,
            "synchronized",
            info.synchronized
        ) != NULL);

    valid = valid &&
        (cJSON_AddNumberToObject(
            time_object,
            "synchronization_count",
            info.synchronization_count
        ) != NULL);

    valid = valid &&
        (cJSON_AddStringToObject(
            time_object,
            "timezone",
            info.timezone
        ) != NULL);

    const time_t now = time(NULL);

    char utc_text[32] = {0};
    char local_text[40] = {0};

    if (info.time_valid) {
        struct tm utc_time = {0};
        struct tm local_time = {0};

        const bool utc_valid =
            (gmtime_r(&now, &utc_time) != NULL) &&
            (strftime(
                utc_text,
                sizeof(utc_text),
                "%Y-%m-%dT%H:%M:%SZ",
                &utc_time
            ) > 0U);

        const bool local_valid =
            (localtime_r(&now, &local_time) != NULL) &&
            (strftime(
                local_text,
                sizeof(local_text),
                "%Y-%m-%dT%H:%M:%S%z",
                &local_time
            ) > 0U);

        valid = valid &&
            (cJSON_AddNumberToObject(
                time_object,
                "unix_time",
                (double)now
            ) != NULL);

        if (utc_valid) {
            valid = valid &&
                (cJSON_AddStringToObject(
                    time_object,
                    "utc",
                    utc_text
                ) != NULL);
        } else {
            valid = valid &&
                (cJSON_AddNullToObject(
                    time_object,
                    "utc"
                ) != NULL);
        }

        if (local_valid) {
            valid = valid &&
                (cJSON_AddStringToObject(
                    time_object,
                    "local",
                    local_text
                ) != NULL);
        } else {
            valid = valid &&
                (cJSON_AddNullToObject(
                    time_object,
                    "local"
                ) != NULL);
        }
    } else {
        valid = valid &&
            (cJSON_AddNullToObject(
                time_object,
                "unix_time"
            ) != NULL);

        valid = valid &&
            (cJSON_AddNullToObject(
                time_object,
                "utc"
            ) != NULL);

        valid = valid &&
            (cJSON_AddNullToObject(
                time_object,
                "local"
            ) != NULL);
    }

    if (info.last_synchronization_time > 0) {
        valid = valid &&
            (cJSON_AddNumberToObject(
                time_object,
                "last_synchronization_unix_time",
                (double)info.last_synchronization_time
            ) != NULL);
    } else {
        valid = valid &&
            (cJSON_AddNullToObject(
                time_object,
                "last_synchronization_unix_time"
            ) != NULL);
    }

    if (!valid) {
        cJSON_Delete(time_object);
        return false;
    }

    if (!cJSON_AddItemToObject(
            response,
            "time",
            time_object
        )) {

        cJSON_Delete(time_object);
        return false;
    }

    return true;
}

static esp_err_t web_system_api_get_handler(
    httpd_req_t *request
)
{
    if (request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    system_model_t model = {0};

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

        return web_api_send_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to get system information"
        );
    }

    storage_sd_info_t sd_info = {0};
    bool sd_info_available = false;

    if (model.sd_card_mounted) {
        const esp_err_t sd_result =
            storage_sd_service_get_info(
                &sd_info
            );

        if (sd_result == ESP_OK) {
            sd_info_available = true;

        } else if (sd_result !=
                   ESP_ERR_INVALID_STATE) {

            ESP_LOGW(
                TAG,
                "Failed to get SD-card information: %s",
                esp_err_to_name(sd_result)
            );
        }
    }

    cJSON *response =
        cJSON_CreateObject();

    if (response == NULL) {
        return web_api_send_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to create system response"
        );
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
        (cJSON_AddStringToObject(
            response,
            "chip_model",
            model.chip_model
        ) != NULL);

    valid = valid &&
        (cJSON_AddNumberToObject(
            response,
            "chip_cores",
            model.chip_cores
        ) != NULL);

    valid = valid &&
        (cJSON_AddNumberToObject(
            response,
            "chip_revision",
            model.chip_revision
        ) != NULL);

    valid = valid &&
        (cJSON_AddNumberToObject(
            response,
            "cpu_frequency_mhz",
            model.cpu_frequency_mhz
        ) != NULL);

    valid = valid &&
        (cJSON_AddNumberToObject(
            response,
            "flash_size",
            model.flash_size
        ) != NULL);

    valid = valid &&
        (cJSON_AddNumberToObject(
            response,
            "psram_size",
            model.psram_size
        ) != NULL);

    valid = valid &&
        (cJSON_AddNumberToObject(
            response,
            "reset_reason",
            (int)model.reset_reason
        ) != NULL);

    valid = valid &&
        (cJSON_AddStringToObject(
            response,
            "reset_reason_name",
            web_system_api_reset_reason_name(
                model.reset_reason
            )
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
            "psram_free",
            model.psram_free
        ) != NULL);

    valid = valid &&
        (cJSON_AddNumberToObject(
            response,
            "psram_minimum_free",
            model.psram_minimum_free
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
            "chip_temperature_valid",
            model.chip_temperature_valid
        ) != NULL);

    if (model.chip_temperature_valid) {
        valid = valid &&
            (cJSON_AddNumberToObject(
                response,
                "chip_temperature_celsius",
                model.chip_temperature_celsius
            ) != NULL);
    } else {
        valid = valid &&
            (cJSON_AddNullToObject(
                response,
                "chip_temperature_celsius"
            ) != NULL);
    }

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
            "sd_card_info_available",
            sd_info_available
        ) != NULL);

    valid = valid &&
        (cJSON_AddStringToObject(
            response,
            "sd_card_filesystem",
            sd_info_available
                ? sd_info.filesystem
                : ""
        ) != NULL);

    valid = valid &&
        (cJSON_AddNumberToObject(
            response,
            "sd_card_total_bytes",
            sd_info_available
                ? (double)sd_info.total_bytes
                : 0.0
        ) != NULL);

    valid = valid &&
        (cJSON_AddNumberToObject(
            response,
            "sd_card_used_bytes",
            sd_info_available
                ? (double)sd_info.used_bytes
                : 0.0
        ) != NULL);

    valid = valid &&
        (cJSON_AddNumberToObject(
            response,
            "sd_card_free_bytes",
            sd_info_available
                ? (double)sd_info.free_bytes
                : 0.0
        ) != NULL);

    valid = valid &&
        (cJSON_AddBoolToObject(
            response,
            "ota_available",
            model.ota_available
        ) != NULL);

    valid = valid &&
        (cJSON_AddBoolToObject(
            response,
            "internet_available",
            model.internet_available
        ) != NULL);

    valid = valid &&
        web_system_api_add_time(
            response
        );

    if (!valid) {
        cJSON_Delete(response);

        return web_api_send_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to create system response"
        );
    }

    const esp_err_t result =
        web_api_send_json(
            request,
            response
        );

    cJSON_Delete(response);

    return result;
}

static esp_err_t web_system_api_restart_handler(
    httpd_req_t *request
)
{
    if (request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t result =
        system_service_schedule_restart(
            WEB_SYSTEM_RESTART_DELAY_MS
        );

    if (result == ESP_ERR_INVALID_STATE) {
        return web_api_send_message(
            request,
            "409 Conflict",
            false,
            "Restart is already scheduled"
        );
    }

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to schedule restart: %s",
            esp_err_to_name(result)
        );

        return web_api_send_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to schedule restart"
        );
    }

    return web_api_send_message(
        request,
        "202 Accepted",
        true,
        "Graceful restart scheduled"
    );
}

esp_err_t web_system_api_register(
    httpd_handle_t server
)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    static const httpd_uri_t system_get_uri = {
        .uri = "/api/system",
        .method = HTTP_GET,
        .handler =
            web_system_api_get_handler,
        .user_ctx = NULL,
    };

    static const httpd_uri_t restart_post_uri = {
        .uri = "/api/system/restart",
        .method = HTTP_POST,
        .handler =
            web_system_api_restart_handler,
        .user_ctx = NULL,
    };

    esp_err_t result =
        httpd_register_uri_handler(
            server,
            &system_get_uri
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to register GET /api/system: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        httpd_register_uri_handler(
            server,
            &restart_post_uri
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to register POST /api/system/restart: %s",
            esp_err_to_name(result)
        );

        /*
         * Roll back the handler registered by this API module.
         */
        (void)httpd_unregister_uri_handler(
            server,
            "/api/system",
            HTTP_GET
        );

        return result;
    }

    return ESP_OK;
}
