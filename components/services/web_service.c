/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "web_service.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "web_content_service.h"

#include "web_system_api.h"
#include "web_power_api.h"
#include "web_settings_api.h"
#include "web_files_api.h"
#include "web_network_api.h"
#include "web_can_stream_service.h"

#define WEB_SERVICE_MAX_URI_HANDLERS  (32U)

static const char *TAG = "web_service";

static httpd_handle_t s_server = NULL;

esp_err_t web_service_start(void)
{
    if (s_server != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_config_t config =
        HTTPD_DEFAULT_CONFIG();

    config.server_port = 80U;
    config.ctrl_port = 32768U;
    config.max_uri_handlers = WEB_SERVICE_MAX_URI_HANDLERS;
    config.stack_size = 6144U;
    config.max_open_sockets = 4U;
    config.backlog_conn = 2U;
    config.lru_purge_enable = true;
    config.send_wait_timeout = 15U;
    config.recv_wait_timeout = 15U;

    ESP_LOGI(
        TAG,
        "HTTP task memory: internal=%u, largest=%u",
        (unsigned int)heap_caps_get_free_size(
            MALLOC_CAP_INTERNAL |
            MALLOC_CAP_8BIT
        ),
        (unsigned int)heap_caps_get_largest_free_block(
            MALLOC_CAP_INTERNAL |
            MALLOC_CAP_8BIT
        )
    );

    esp_err_t result =
        httpd_start(
            &s_server,
            &config
        );

    if (result != ESP_OK) {
        s_server = NULL;

        ESP_LOGE(
            TAG,
            "Failed to start HTTP server: %s, "
            "internal=%u, largest=%u",
            esp_err_to_name(result),
            (unsigned int)heap_caps_get_free_size(
                MALLOC_CAP_INTERNAL |
                MALLOC_CAP_8BIT
            ),
            (unsigned int)heap_caps_get_largest_free_block(
                MALLOC_CAP_INTERNAL |
                MALLOC_CAP_8BIT
            )
        );

        return result;
    }

    result =
        web_content_service_register(
            s_server
        );

    if (result != ESP_OK) {
        goto registration_failed;
    }

    result =
        web_settings_api_register(
            s_server
        );

    if (result != ESP_OK) {
        goto registration_failed;
    }

    result =
        web_system_api_register(
            s_server
        );

    if (result != ESP_OK) {
        goto registration_failed;
    }

    result =
        web_power_api_register(
            s_server
        );

    if (result != ESP_OK) {
        goto registration_failed;
    }

    result =
        web_files_api_register(
            s_server
        );

    if (result != ESP_OK) {
        goto registration_failed;
    }

    result =
        web_network_api_register(
            s_server
        );

    if (result != ESP_OK) {
        goto registration_failed;
    }

    const web_can_stream_service_config_t
        can_stream_config = {

            .queue_depth =
                512U,

            .statistics_interval_ms =
                1000U,
        };

    result =
        web_can_stream_service_start(
            s_server,
            &can_stream_config
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to start CAN WebSocket stream: %s",
            esp_err_to_name(result)
        );

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
        "Failed to initialize web service: %s",
        esp_err_to_name(result)
    );

    if (web_can_stream_service_is_running()) {
        const esp_err_t stream_result =
            web_can_stream_service_stop();

        if (stream_result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Failed to roll back CAN WebSocket stream: %s",
                esp_err_to_name(stream_result)
            );

            /*
             * The stream task may still use s_server. Preserve the
             * server and allow web_service_stop() to retry cleanup.
             */
            return stream_result;
        }
    }

    const esp_err_t stop_result =
        httpd_stop(
            s_server
        );

    if (stop_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to roll back HTTP server: %s",
            esp_err_to_name(stop_result)
        );

        return stop_result;
    }

    s_server = NULL;

    return result;
}

esp_err_t web_service_stop(void)
{
    if (s_server == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (web_can_stream_service_is_running()) {
        const esp_err_t stream_result =
            web_can_stream_service_stop();

        if (stream_result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Failed to stop CAN WebSocket stream: %s",
                esp_err_to_name(stream_result)
            );

            /*
             * The stream task may still be using the HTTP server.
             * Do not destroy the server until stream shutdown succeeds.
             */
            return stream_result;
        }
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
