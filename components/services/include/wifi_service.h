#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SERVICE_AP_SSID_MAX_LENGTH       (33U)
#define WIFI_SERVICE_AP_PASSWORD_MIN_LENGTH   (8U)
#define WIFI_SERVICE_AP_PASSWORD_MAX_LENGTH   (64U)

#define WIFI_SERVICE_STA_SSID_MAX_LENGTH       (33U)
#define WIFI_SERVICE_STA_PASSWORD_MIN_LENGTH   (8U)
#define WIFI_SERVICE_STA_PASSWORD_MAX_LENGTH   (64U)

#define WIFI_SERVICE_IPV4_ADDRESS_MAX_LENGTH  (16U)
#define WIFI_SERVICE_MAC_ADDRESS_LENGTH       (6U)
#define WIFI_SERVICE_MAX_CLIENT_COUNT         (2U)
/**
 * @brief Information about a connected Wi-Fi station.
 */
typedef struct
{
    uint8_t mac[
        WIFI_SERVICE_MAC_ADDRESS_LENGTH
    ];

    int8_t rssi;

    /*
     * Empty when the client's IPv4 address is unavailable.
     */
    char ip_address[
        WIFI_SERVICE_IPV4_ADDRESS_MAX_LENGTH
    ];

} wifi_service_client_info_t;

/**
 * @brief Current Wi-Fi service information.
 *
 * Contains the common driver state, SoftAP configuration and clients,
 * and the current Station connection information.
 */
typedef struct
{
    bool initialized;
    bool started;

    bool ap_enabled;
    bool sta_enabled;
    bool sta_connected;

    char ssid[
        WIFI_SERVICE_AP_SSID_MAX_LENGTH
    ];

    /*
     * Indicates whether the configured SoftAP uses a password.
     * The password itself is intentionally not exposed.
     */
    bool password_configured;

    uint8_t channel;

    char ip_address[
        WIFI_SERVICE_IPV4_ADDRESS_MAX_LENGTH
    ];

    char netmask[
        WIFI_SERVICE_IPV4_ADDRESS_MAX_LENGTH
    ];

    char dhcp_start[
        WIFI_SERVICE_IPV4_ADDRESS_MAX_LENGTH
    ];

    char dhcp_end[
        WIFI_SERVICE_IPV4_ADDRESS_MAX_LENGTH
    ];

    char dns_address[
        WIFI_SERVICE_IPV4_ADDRESS_MAX_LENGTH
    ];

    size_t client_count;

    wifi_service_client_info_t clients[
        WIFI_SERVICE_MAX_CLIENT_COUNT
    ];

    char sta_ssid[
        WIFI_SERVICE_STA_SSID_MAX_LENGTH
    ];

    int8_t sta_rssi;

    char sta_ip_address[
        WIFI_SERVICE_IPV4_ADDRESS_MAX_LENGTH
    ];

    char sta_netmask[
        WIFI_SERVICE_IPV4_ADDRESS_MAX_LENGTH
    ];

    char sta_gateway[
        WIFI_SERVICE_IPV4_ADDRESS_MAX_LENGTH
    ];

    char sta_dns_address[
        WIFI_SERVICE_IPV4_ADDRESS_MAX_LENGTH
    ];

} wifi_service_info_t;

/**
 * @brief Initialize the Wi-Fi service.
 *
 * The global ESP-NETIF stack and default event loop must already be
 * initialized. This function creates the synchronization resources,
 * SoftAP and Station network interfaces, event handlers and Wi-Fi
 * driver, but does not start the driver.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * already initialized or a required network service is unavailable,
 * ESP_ERR_NO_MEM if required resources cannot be allocated,
 * ESP_ERR_TIMEOUT if the service lock cannot be acquired, otherwise
 * an ESP-IDF error code.
 */
esp_err_t wifi_service_init(void);

/**
 * @brief Start the Wi-Fi driver and enabled interfaces.
 *
 * The effective mode is selected from the configured SoftAP and
 * Station states. At least one interface must be enabled.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not initialized, already running, or no interface is enabled,
 * ESP_ERR_TIMEOUT if the service lock cannot be acquired, otherwise
 * an ESP-IDF error code.
 */
esp_err_t wifi_service_start(void);

/**
 * @brief Stop all Wi-Fi interfaces.
 *
 * This function stops both SoftAP and Station while preserving the
 * driver, network interfaces and synchronization resources. Calling
 * it when the service is already stopped returns ESP_OK.
 */
esp_err_t wifi_service_stop(void);

/**
 * @brief Deinitialize the Wi-Fi service.
 *
 * If Wi-Fi is running, both interfaces are stopped first. The Wi-Fi
 * driver, event handlers, network interfaces and service resources
 * are then released.
 *
 * The persistent service mutex is retained to keep repeated
 * initialization and deinitialization synchronized.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not initialized, ESP_ERR_TIMEOUT if the service lock cannot be
 * acquired, otherwise an ESP-IDF error code.
 */
esp_err_t wifi_service_deinit(void);

/**
 * @brief Get the common Wi-Fi driver running state.
 *
 * @param[out] started Set to true when the Wi-Fi driver is running.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if started is NULL,
 * ESP_ERR_INVALID_STATE if the service is not initialized, or
 * ESP_ERR_TIMEOUT if the service lock cannot be acquired.
 */
esp_err_t wifi_service_get_started(
    bool *started
);

/**
 * @brief Copy the current SoftAP SSID.
 *
 * @param[out] ssid Destination buffer.
 * @param[in] ssid_size Destination buffer size in bytes.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if an argument is
 * invalid, ESP_ERR_INVALID_SIZE if the destination buffer is too
 * small, ESP_ERR_INVALID_STATE if the service is not initialized, or
 * ESP_ERR_TIMEOUT if the service lock cannot be acquired.
 */
esp_err_t wifi_service_get_ap_ssid(
    char *ssid,
    size_t ssid_size
);

/**
 * @brief Copy the current SoftAP IPv4 address.
 *
 * @param[out] address Destination buffer.
 * @param[in] address_size Destination buffer size in bytes.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if an argument is
 * invalid, ESP_ERR_INVALID_SIZE if the destination buffer is too
 * small, ESP_ERR_INVALID_STATE if the service is not initialized, or
 * ESP_ERR_TIMEOUT if the service lock cannot be acquired.
 */
esp_err_t wifi_service_get_ap_ip_address(
    char *address,
    size_t address_size
);

/**
 * @brief Get the current Wi-Fi service information.
 *
 * The connected-client list is queried directly from the Wi-Fi driver.
 * It currently contains the MAC address and RSSI of each station.
 * Client IPv4 addresses remain empty until DHCP lease lookup support
 * is implemented.
 *
 * The result contains SoftAP state and clients as well as Station
 * connection state and IPv4 configuration. Passwords are never
 * returned.
 *
 * @param[out] info Destination information structure.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if info is NULL,
 * ESP_ERR_INVALID_STATE if the synchronization resources have not
 * been created, ESP_ERR_TIMEOUT if the service lock cannot be
 * acquired, otherwise an ESP-IDF error code.
 */
esp_err_t wifi_service_get_info(
    wifi_service_info_t *info
);

/**
 * @brief Enable or disable the SoftAP interface.
 *
 * The Wi-Fi driver remains initialized. The effective Wi-Fi mode is
 * selected automatically from the enabled AP and Station interfaces.
 *
 * @param[in] enabled True to enable SoftAP.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not initialized, ESP_ERR_TIMEOUT if the service lock cannot be
 * acquired, otherwise an ESP-IDF error code.
 */
esp_err_t wifi_service_set_ap_enabled(
    bool enabled
);

/**
 * @brief Configure the Wi-Fi SoftAP credentials.
 *
 * The supplied SSID is a base name. The service appends a hyphen and
 * the final six hexadecimal characters of the SoftAP MAC address. For
 * example, the base name "Spectra" may become "Spectra-A1B2C3".
 *
 * The base SSID may contain at most 25 bytes. This function may be
 * called before wifi_service_init(). When the service is already
 * initialized, the new configuration is applied immediately and
 * connected clients may be disconnected.
 *
 * An empty password configures an open network. A non-empty password
 * must contain from 8 to 63 characters.
 *
 * @param[in] ssid Null-terminated base SoftAP SSID.
 * @param[in] password Null-terminated SoftAP password.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if an argument or
 * password is invalid, ESP_ERR_INVALID_SIZE if the SSID or password
 * is too long, ESP_ERR_NO_MEM if synchronization resources cannot be
 * created, ESP_ERR_TIMEOUT if the service lock cannot be acquired,
 * otherwise an ESP-IDF error code.
 */
esp_err_t wifi_service_set_ap_credentials(
    const char *ssid,
    const char *password
);

/**
 * @brief Enable or disable the Wi-Fi Station interface.
 *
 * Enabling Station starts a connection attempt using the configured
 * credentials. Disabling Station disconnects it without disabling
 * SoftAP.
 *
 * @param[in] enabled True to enable the Station interface.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not initialized, ESP_ERR_TIMEOUT if the service lock cannot be
 * acquired, otherwise an ESP-IDF error code.
 */
esp_err_t wifi_service_set_sta_enabled(
    bool enabled
);

/**
 * @brief Configure Wi-Fi Station credentials.
 *
 * This function may be called before wifi_service_init(). When Station
 * is running, the interface reconnects using the new credentials.
 *
 * An empty password configures an open network. A non-empty password
 * must contain from 8 to 63 bytes.
 *
 * @param[in] ssid Null-terminated Station SSID containing from 1 to
 * 32 bytes.
 * @param[in] password Null-terminated Station password.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if an argument or
 * password is invalid, ESP_ERR_INVALID_SIZE if a credential is too
 * long, ESP_ERR_NO_MEM if synchronization resources cannot be created,
 * ESP_ERR_TIMEOUT if the service lock cannot be acquired, otherwise an
 * ESP-IDF error code.
 */
esp_err_t wifi_service_set_sta_credentials(
    const char *ssid,
    const char *password
);

#ifdef __cplusplus
}
#endif
