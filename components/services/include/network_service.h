#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USB_NETWORK_INTERFACE_KEY  ("USB_RNDIS")
#define WIFI_AP_INTERFACE_KEY      ("WIFI_AP_DEF")

/**
 * @brief Initialize the global network stack.
 *
 * Initializes ESP-NETIF and creates the default ESP-IDF event loop.
 * This function is idempotent and may be called more than once.
 *
 * The network service must be initialized before creating USB,
 * Wi-Fi, Ethernet, or other ESP-NETIF interfaces.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t network_service_init(void);

/**
 * @brief Get the global network-stack initialization state.
 *
 * @param[out] initialized Set to true when ESP-NETIF and the default
 * event loop are initialized.
 *
 * @return ESP_OK on success or ESP_ERR_INVALID_ARG if initialized
 * is NULL.
 */
esp_err_t network_service_get_initialized(
    bool *initialized
);

#ifdef __cplusplus
}
#endif
