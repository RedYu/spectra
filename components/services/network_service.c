#include "network_service.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "dns_server.h"

#define NETWORK_SERVICE_DNS_NAME  ("spectra.device")

static const char *TAG = "network_service";

static bool s_netif_initialized = false;
static bool s_event_loop_initialized = false;

static dns_server_handle_t s_dns_server = NULL;

static esp_err_t network_service_start_dns(void);

esp_err_t network_service_init(void)
{
    if (s_netif_initialized &&
        s_event_loop_initialized) {

        return network_service_start_dns();
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

    const esp_err_t dns_result =
        network_service_start_dns();

    if (dns_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to start DNS server: %s",
            esp_err_to_name(dns_result)
        );

        return dns_result;
    }

    ESP_LOGI(
        TAG,
        "Global network stack and DNS server initialized"
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
        s_event_loop_initialized &&
        (s_dns_server != NULL);

    return ESP_OK;
}

static esp_err_t network_service_start_dns(void)
{
    if (s_dns_server != NULL) {
        return ESP_OK;
    }

    const dns_server_config_t config = {
        .num_of_entries = 2U,

        .item = {
            {
                .name = NETWORK_SERVICE_DNS_NAME,
                .if_key = USB_NETWORK_INTERFACE_KEY,

                .source_network = {
                    .addr = ESP_IP4TOADDR(
                        172U,
                        16U,
                        10U,
                        0U
                    ),
                },

                .source_netmask = {
                    .addr = ESP_IP4TOADDR(
                        255U,
                        255U,
                        255U,
                        0U
                    ),
                },
            },
            {
                .name = NETWORK_SERVICE_DNS_NAME,

                /*
                 * Use the actual if_key assigned to the SoftAP
                 * esp_netif instance.
                 */
                .if_key = WIFI_AP_INTERFACE_KEY,

                .source_network = {
                    .addr = ESP_IP4TOADDR(
                        172U,
                        16U,
                        20U,
                        0U
                    ),
                },

                .source_netmask = {
                    .addr = ESP_IP4TOADDR(
                        255U,
                        255U,
                        255U,
                        0U
                    ),
                },
            },
        },
    };

    s_dns_server =
        start_dns_server(
            &config
        );

    if (s_dns_server == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create DNS server"
        );

        return ESP_FAIL;
    }

    ESP_LOGI(
        TAG,
        "DNS server started for USB and Wi-Fi networks"
    );

    return ESP_OK;
}

esp_err_t network_service_get_dns_info(
    network_service_dns_info_t *info
)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(
        info,
        0,
        sizeof(*info)
    );

    info->started =
        dns_server_get_started(
            s_dns_server
        );

    (void)strlcpy(
        info->local_name,
        NETWORK_SERVICE_DNS_NAME,
        sizeof(info->local_name)
    );

    return ESP_OK;
}
