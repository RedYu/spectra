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
    config.max_uri_handlers = 16U;
    config.stack_size = 6144U;
    config.max_open_sockets = 3U;
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
