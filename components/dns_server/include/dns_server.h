/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_netif_ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DNS_SERVER_MAX_ITEMS
#define DNS_SERVER_MAX_ITEMS  (2U)
#endif

/**
 * @brief Create a configuration containing one unrestricted DNS rule.
 *
 * The rule applies to clients from any IPv4 network.
 */
#define DNS_SERVER_CONFIG_SINGLE(queried_name, netif_key)       \
    {                                                           \
        .num_of_entries = 1U,                                   \
        .item = {                                               \
            {                                                   \
                .name = (queried_name),                         \
                .if_key = (netif_key),                          \
                .source_network = { .addr = 0U },               \
                .source_netmask = { .addr = 0U },               \
            }                                                   \
        }                                                       \
    }

/**
 * @brief Define one DNS response rule.
 *
 * A rule matches both the requested DNS name and the source IPv4
 * network. A zero source netmask makes the rule apply to all clients.
 *
 * The DNS server copies this structure, but it does not copy strings.
 * The name and if_key pointers must remain valid during the complete
 * DNS server lifetime.
 */
typedef struct
{
    /**
     * Exact DNS name to match. The value "*" matches every name.
     */
    const char *name;

    /**
     * Optional ESP-NETIF key whose current IPv4 address is returned.
     * When NULL, the static address stored in ip is returned.
     */
    const char *if_key;

    /**
     * Static response address used when if_key is NULL.
     */
    esp_ip4_addr_t ip;

    /**
     * IPv4 client network to which this rule applies.
     */
    esp_ip4_addr_t source_network;

    /**
     * Client-network mask. A zero mask matches every source address.
     */
    esp_ip4_addr_t source_netmask;

} dns_entry_pair_t;

/**
 * @brief DNS server configuration.
 */
typedef struct
{
    size_t num_of_entries;

    dns_entry_pair_t item[
        DNS_SERVER_MAX_ITEMS
    ];

} dns_server_config_t;

/**
 * @brief Opaque DNS server handle.
 */
typedef struct dns_server_handle *dns_server_handle_t;

/**
 * @brief Start the DNS server.
 *
 * The server answers supported IPv4 DNS requests using the first
 * configuration entry matching both the queried name and the source
 * client network.
 *
 * This function creates the server task asynchronously. A returned
 * handle means that the task was created, but the UDP socket may not
 * yet be bound. Use dns_server_get_started() to check readiness.
 *
 * @param[in] config DNS server configuration.
 *
 * @return DNS server handle when the task is created; otherwise NULL.
 */
dns_server_handle_t start_dns_server(
    const dns_server_config_t *config
);

/**
 * @brief Check whether the DNS server socket is ready.
 *
 * The handle must remain valid while this function is called.
 *
 * @param[in] handle DNS server handle. NULL is accepted.
 *
 * @return true when the server task has successfully bound its UDP
 * socket to port 53; otherwise false.
 */
bool dns_server_get_started(
    dns_server_handle_t handle
);

/**
 * @brief Stop the DNS server and release its resources.
 *
 * This function waits for the server task to terminate. The handle
 * becomes invalid after this function returns.
 *
 * No other task may call dns_server_get_started() concurrently with
 * this function unless access to the handle is synchronized by the
 * caller.
 *
 * @param[in] handle DNS server handle. NULL is accepted.
 */
void stop_dns_server(
    dns_server_handle_t handle
);

#ifdef __cplusplus
}
#endif
