#include "internet_service.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"

#include "system_model.h"
#include "wifi_service.h"

#define INTERNET_SERVICE_URL \
    ("https://api.spectra.ridel.com.ua/health")

#define INTERNET_SERVICE_TIMEOUT_MS      (20000U)
#define INTERNET_SERVICE_TASK_STACK_SIZE (6144U)
#define INTERNET_SERVICE_TASK_PRIORITY   (2U)

#define INTERNET_SERVICE_NOTIFY_CHECK \
    (1UL << 0U)

#define INTERNET_SERVICE_NOTIFY_UNAVAILABLE \
    (1UL << 1U)

#define INTERNET_SERVICE_NOTIFY_STOP \
    (1UL << 2U)

static const char *TAG =
    "internet_service";

static TaskHandle_t s_task = NULL;

static esp_event_handler_instance_t
    s_ip_event_instance = NULL;

static esp_event_handler_instance_t
    s_wifi_event_instance = NULL;

static bool internet_service_network_ready(void)
{
    wifi_service_info_t info = {0};

    const esp_err_t result =
        wifi_service_get_info(
            &info
        );

    if (result != ESP_OK) {
        return false;
    }

    if (!info.sta_enabled ||
        !info.sta_connected) {

        return false;
    }

    if ((info.sta_ip_address[0] == '\0') ||
        (strcmp(
            info.sta_ip_address,
            "0.0.0.0"
        ) == 0)) {

        return false;
    }

    return true;
}

static bool internet_service_check(void)
{
    const esp_http_client_config_t config = {
        .url = INTERNET_SERVICE_URL,
        .method = HTTP_METHOD_GET,

        .timeout_ms =
            INTERNET_SERVICE_TIMEOUT_MS,

        /*
         * Verify the backend certificate using the ESP-IDF trusted
         * root certificate bundle.
         */
        .crt_bundle_attach =
            esp_crt_bundle_attach,

        .tls_version =
            ESP_HTTP_CLIENT_TLS_VER_TLS_1_2,

        /*
         * Release the TLS connection after the check to preserve heap
         * memory while the service is idle.
         */
        .keep_alive_enable = false,
    };

    esp_http_client_handle_t client =
        esp_http_client_init(
            &config
        );

    if (client == NULL) {
        ESP_LOGW(
            TAG,
            "Failed to create HTTP client"
        );

        return false;
    }

    ESP_LOGI(
        TAG,
        "Memory before HTTPS: internal_dma=%u, largest=%u",
        (unsigned int)heap_caps_get_free_size(
            MALLOC_CAP_INTERNAL |
            MALLOC_CAP_DMA |
            MALLOC_CAP_8BIT
        ),
        (unsigned int)heap_caps_get_largest_free_block(
            MALLOC_CAP_INTERNAL |
            MALLOC_CAP_DMA |
            MALLOC_CAP_8BIT
        )
    );

    const esp_err_t result =
        esp_http_client_perform(
            client
        );

    bool available = false;

    if (result == ESP_OK) {
        const int status_code =
            esp_http_client_get_status_code(
                client
            );

        available =
            (status_code >= 200) &&
            (status_code < 300);

        if (!available) {
            ESP_LOGD(
                TAG,
                "Backend returned HTTP status %d",
                status_code
            );
        }
    } else {
        ESP_LOGD(
            TAG,
            "Backend availability check failed: %s",
            esp_err_to_name(result)
        );
    }

    esp_http_client_cleanup(
        client
    );

    ESP_LOGI(
        TAG,
        "Memory after HTTPS: internal_dma=%u, "
        "minimum=%u, largest=%u",
        (unsigned int)heap_caps_get_free_size(
            MALLOC_CAP_INTERNAL |
            MALLOC_CAP_DMA |
            MALLOC_CAP_8BIT
        ),
        (unsigned int)heap_caps_get_minimum_free_size(
            MALLOC_CAP_INTERNAL |
            MALLOC_CAP_DMA |
            MALLOC_CAP_8BIT
        ),
        (unsigned int)heap_caps_get_largest_free_block(
            MALLOC_CAP_INTERNAL |
            MALLOC_CAP_DMA |
            MALLOC_CAP_8BIT
        )
    );

    return available;
}

static void internet_service_event_handler(
    void *argument,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
)
{
    (void)argument;
    (void)event_data;

    TaskHandle_t task =
        s_task;

    if (task == NULL) {
        return;
    }

    if ((event_base == IP_EVENT) &&
        (event_id == IP_EVENT_STA_GOT_IP)) {

        (void)xTaskNotify(
            task,
            INTERNET_SERVICE_NOTIFY_CHECK,
            eSetBits
        );

        return;
    }

    if (((event_base == IP_EVENT) &&
         (event_id == IP_EVENT_STA_LOST_IP)) ||
        ((event_base == WIFI_EVENT) &&
         (event_id ==
          WIFI_EVENT_STA_DISCONNECTED))) {

        (void)xTaskNotify(
            task,
            INTERNET_SERVICE_NOTIFY_UNAVAILABLE,
            eSetBits
        );
    }
}

static void internet_service_unregister_handlers(void)
{
    if (s_wifi_event_instance != NULL) {
        (void)esp_event_handler_instance_unregister(
            WIFI_EVENT,
            WIFI_EVENT_STA_DISCONNECTED,
            s_wifi_event_instance
        );

        s_wifi_event_instance = NULL;
    }

    if (s_ip_event_instance != NULL) {
        (void)esp_event_handler_instance_unregister(
            IP_EVENT,
            ESP_EVENT_ANY_ID,
            s_ip_event_instance
        );

        s_ip_event_instance = NULL;
    }
}

static void internet_service_task(
    void *argument
)
{
    (void)argument;

    bool previous_available = false;
    bool availability_known = false;

    while (true) {
        uint32_t notification = 0U;

        const BaseType_t notified =
            xTaskNotifyWait(
                0U,
                UINT32_MAX,
                &notification,
                portMAX_DELAY
            );

        if (notified != pdTRUE) {
            continue;
        }

        if ((notification &
             INTERNET_SERVICE_NOTIFY_STOP) != 0U) {

            break;
        }

        bool update_required = false;
        bool available =
            previous_available;

        if ((notification &
             INTERNET_SERVICE_NOTIFY_UNAVAILABLE) != 0U) {

            available = false;
            update_required = true;
        }

        if ((notification &
             INTERNET_SERVICE_NOTIFY_CHECK) != 0U) {

            available =
                internet_service_network_ready()
                    ? internet_service_check()
                    : false;

            update_required = true;
        }

        if (!update_required) {
            continue;
        }

        const esp_err_t result =
            system_model_set_internet_available(
                available
            );

        if (result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Failed to update Internet state: %s",
                esp_err_to_name(result)
            );
        }

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
    }

    (void)system_model_set_internet_available(
        false
    );

    internet_service_unregister_handlers();

    s_task = NULL;

    vTaskDeleteWithCaps(NULL);
}

esp_err_t internet_service_start(void)
{
    if ((s_task != NULL) ||
        (s_ip_event_instance != NULL) ||
        (s_wifi_event_instance != NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result =
        system_model_set_internet_available(
            false
        );

    if (result != ESP_OK) {
        return result;
    }

    /*
     * Register handlers before creating the task. If a network event
     * occurs before the task exists, the initial state check below
     * will recover the current state.
     */
    result =
        esp_event_handler_instance_register(
            IP_EVENT,
            ESP_EVENT_ANY_ID,
            internet_service_event_handler,
            NULL,
            &s_ip_event_instance
        );

    if (result != ESP_OK) {
        return result;
    }

    result =
        esp_event_handler_instance_register(
            WIFI_EVENT,
            WIFI_EVENT_STA_DISCONNECTED,
            internet_service_event_handler,
            NULL,
            &s_wifi_event_instance
        );

    if (result != ESP_OK) {
        internet_service_unregister_handlers();

        return result;
    }

    const BaseType_t task_result =
        xTaskCreateWithCaps(
            internet_service_task,
            "internet_service",
            INTERNET_SERVICE_TASK_STACK_SIZE,
            NULL,
            INTERNET_SERVICE_TASK_PRIORITY,
            &s_task,
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

    if (task_result != pdPASS) {
        s_task = NULL;

        internet_service_unregister_handlers();

        return ESP_ERR_NO_MEM;
    }

    /*
     * Handle the case where the Station obtained an address before
     * this service was started.
     */
    if (internet_service_network_ready()) {
        (void)xTaskNotify(
            s_task,
            INTERNET_SERVICE_NOTIFY_CHECK,
            eSetBits
        );
    }

    return ESP_OK;
}

void internet_service_stop(void)
{
    TaskHandle_t task =
        s_task;

    if (task != NULL) {
        (void)xTaskNotify(
            task,
            INTERNET_SERVICE_NOTIFY_STOP,
            eSetBits
        );
    }
}
