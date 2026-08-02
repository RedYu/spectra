#include "network_service.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

static const char *TAG = "network_service";

static bool s_netif_initialized = false;
static bool s_event_loop_initialized = false;

esp_err_t network_service_init(void)
{
    if (s_netif_initialized &&
        s_event_loop_initialized) {

        return ESP_OK;
    }

    if (!s_netif_initialized) {
        const esp_err_t result =
            esp_netif_init();

        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Failed to initialize ESP-NETIF: %s",
                esp_err_to_name(result)
            );

            return result;
        }

        s_netif_initialized = true;
    }

    if (!s_event_loop_initialized) {
        const esp_err_t result =
            esp_event_loop_create_default();

        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Failed to create default event loop: %s",
                esp_err_to_name(result)
            );

            /*
             * ESP-NETIF deinitialization is not currently supported.
             * Keep its initialized state and allow a later call to
             * retry default event-loop creation.
             */
            return result;
        }

        s_event_loop_initialized = true;
    }

    ESP_LOGI(
        TAG,
        "Global network stack initialized"
    );

    return ESP_OK;
}

esp_err_t network_service_get_initialized(
    bool *initialized
)
{
    if (initialized == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *initialized =
        s_netif_initialized &&
        s_event_loop_initialized;

    return ESP_OK;
}
