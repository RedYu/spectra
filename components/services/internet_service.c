#include "internet_service.h"

#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "system_model.h"
#include "wifi_service.h"

#define INTERNET_SERVICE_URL \
    ("https://api.spectra.ridel.com.ua/health")

#define INTERNET_SERVICE_CHECK_INTERVAL_MS  (30000U)
#define INTERNET_SERVICE_TIMEOUT_MS         (20000U)
#define INTERNET_SERVICE_TASK_STACK_SIZE    (6144U)
#define INTERNET_SERVICE_TASK_PRIORITY      (4U)

static const char *TAG =
    "internet_service";

static TaskHandle_t s_task = NULL;

static bool internet_service_is_network_ready(void)
{
    wifi_service_info_t wifi_info = {0};

    const esp_err_t result =
        wifi_service_get_info(
            &wifi_info
        );

    if (result != ESP_OK) {
        return false;
    }

    return wifi_info.sta_connected &&
           (wifi_info.sta_ip_address[0] != '\0') &&
           (strcmp(
                wifi_info.sta_ip_address,
                "0.0.0.0"
            ) != 0);
}

static bool internet_service_check(
    esp_http_client_handle_t client
)
{
    if (client == NULL) {
        return false;
    }

    const esp_err_t result =
        esp_http_client_perform(client);

    if (result != ESP_OK) {
        /*
         * Discard a failed persistent connection. The next perform()
         * call will establish a new connection.
         */
        (void)esp_http_client_close(client);

        ESP_LOGD(
            TAG,
            "Backend availability check failed: %s",
            esp_err_to_name(result)
        );

        return false;
    }

    const int status_code =
        esp_http_client_get_status_code(
            client
        );

    return (status_code >= 200) &&
           (status_code < 300);
}

static void internet_service_task(
    void *argument
)
{
    (void)argument;

    const esp_http_client_config_t config = {
        .url = INTERNET_SERVICE_URL,
        .method = HTTP_METHOD_GET,

        .timeout_ms =
            INTERNET_SERVICE_TIMEOUT_MS,

        .crt_bundle_attach =
            esp_crt_bundle_attach,

        .tls_version =
            ESP_HTTP_CLIENT_TLS_VER_TLS_1_2,

        .keep_alive_enable = true,

        .keep_alive_idle = 10,
        .keep_alive_interval = 5,
        .keep_alive_count = 3,
    };

    esp_http_client_handle_t client =
        esp_http_client_init(
            &config
        );

    bool previous_available = false;
    bool availability_known = false;

    if (client == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create HTTP client"
        );

        (void)system_model_set_internet_available(
            false
        );

        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        bool available = false;

        if (internet_service_is_network_ready()) {
            available =
                internet_service_check(
                    client
                );
        } else {
            /*
             * Release a persistent connection when the Station
             * interface is no longer available. A new connection will
             * be established after the Station reconnects.
             */
            (void)esp_http_client_close(
                client
            );
        }

        const esp_err_t model_result =
            system_model_set_internet_available(
                available
            );

        if (model_result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Failed to update Internet state: %s",
                esp_err_to_name(model_result)
            );
        }

        /*
         * Log the initial result and all subsequent state transitions.
         */
        if (!availability_known ||
            (available != previous_available)) {

            ESP_LOGI(
                TAG,
                "Spectra backend is %s",
                available
                    ? "available"
                    : "unavailable"
            );

            previous_available =
                available;

            availability_known =
                true;
        }

        if (ulTaskNotifyTake(
                pdTRUE,
                pdMS_TO_TICKS(
                    INTERNET_SERVICE_CHECK_INTERVAL_MS
                )
            ) > 0U) {

            break;
        }
    }

    (void)system_model_set_internet_available(
        false
    );

    (void)esp_http_client_cleanup(
        client
    );

    s_task = NULL;

    vTaskDelete(NULL);
}

esp_err_t internet_service_start(void)
{
    if (s_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t model_result =
        system_model_set_internet_available(
            false
        );

    if (model_result != ESP_OK) {
        return model_result;
    }

    const BaseType_t result =
        xTaskCreate(
            internet_service_task,
            "internet_service",
            INTERNET_SERVICE_TASK_STACK_SIZE,
            NULL,
            INTERNET_SERVICE_TASK_PRIORITY,
            &s_task
        );

    if (result != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void internet_service_stop(void)
{
    if (s_task != NULL) {
        xTaskNotifyGive(
            s_task
        );
    }
}
