/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USB_NETWORK_INTERFACE_KEY  ("USB_RNDIS")
#define WIFI_AP_INTERFACE_KEY      ("WIFI_AP_DEF")

#define NETWORK_SERVICE_DNS_NAME_MAX_LENGTH  (64U)

/**
 * @brief Current local DNS service information.
 *
 * The local_name array includes space for the terminating null
 * character.
 */
typedef struct
{
    bool started;

    char local_name[
        NETWORK_SERVICE_DNS_NAME_MAX_LENGTH
    ];

} network_service_dns_info_t;

/**
 * @brief Initialize the global network stack.
 *
 * Initializes ESP-NETIF, creates the default ESP-IDF event loop and
 * starts the local DNS service.
 *
 * This function is idempotent and may be called more than once.
 * The network service must be initialized before creating USB,
 * Wi-Fi, Ethernet, or other ESP-NETIF interfaces.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t network_service_init(void);

/**
 * @brief Get the global network-stack initialization state.
 *
 * The returned state includes successful DNS-server initialization.
 *
 * @param[out] initialized Set to true when ESP-NETIF, the default
 * event loop and the DNS server are initialized.
 *
 * @return ESP_OK on success or ESP_ERR_INVALID_ARG if initialized
 * is NULL.
 */
esp_err_t network_service_get_initialized(
    bool *initialized
);

/**
 * @brief Get the current local DNS service information.
 *
 * This function succeeds even when the DNS server is stopped. In that
 * case, started is false while local_name still contains the configured
 * local DNS name.
 *
 * @param[out] info Destination information structure.
 *
 * @return ESP_OK on success or ESP_ERR_INVALID_ARG if info is NULL.
 */
esp_err_t network_service_get_dns_info(
    network_service_dns_info_t *info
);

/**
 * @brief Request an asynchronous mDNS refresh.
 *
 * The request is processed by the network maintenance task outside
 * the ESP event-loop context. Multiple pending refresh requests may
 * be coalesced into one operation.
 *
 * @return ESP_OK when the request is accepted or a refresh is already
 * pending, or ESP_ERR_INVALID_STATE if the network service is not
 * initialized.
 */
esp_err_t network_service_request_mdns_refresh(void);

/**
 * @brief Stop accepting asynchronous network maintenance requests.
 *
 * Pending mDNS refresh requests are discarded. The global ESP-NETIF
 * and event-loop resources remain initialized until restart.
 *
 * @return ESP_OK on success or ESP_ERR_INVALID_STATE if the
 * maintenance worker is unavailable.
 */
esp_err_t network_service_prepare_shutdown(void);

#ifdef __cplusplus
}
#endif
