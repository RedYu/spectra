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
 * @brief Current Wi-Fi SoftAP service information.
 */
typedef struct
{
    bool initialized;
    bool started;

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

} wifi_service_info_t;

/**
 * @brief Initialize the Wi-Fi SoftAP service.
 *
 * The global ESP-NETIF stack and default event loop must already be
 * initialized. This function creates the service synchronization
 * resources, network interface and Wi-Fi driver, but does not start
 * the SoftAP.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * already initialized or a required network service is unavailable,
 * ESP_ERR_NO_MEM if required resources cannot be allocated,
 * ESP_ERR_TIMEOUT if the service lock cannot be acquired, otherwise
 * an ESP-IDF error code.
 */
esp_err_t wifi_service_init(void);

/**
 * @brief Start the Wi-Fi SoftAP.
 *
 * The service must be initialized first. Resources created during
 * initialization remain available after wifi_service_stop(), allowing
 * the SoftAP to be started again without reinitialization.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not initialized or is already running, ESP_ERR_TIMEOUT if the
 * service lock cannot be acquired or SoftAP startup times out,
 * otherwise an ESP-IDF error code.
 */
esp_err_t wifi_service_start(void);

/**
 * @brief Stop the Wi-Fi SoftAP.
 *
 * This function stops the wireless interface but preserves the Wi-Fi
 * driver, network interface and synchronization resources. Calling
 * this function when the service is already stopped returns ESP_OK.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not initialized, ESP_ERR_TIMEOUT if the service lock cannot be
 * acquired, otherwise an ESP-IDF error code.
 */
esp_err_t wifi_service_stop(void);

/**
 * @brief Deinitialize the Wi-Fi SoftAP service.
 *
 * If the SoftAP is running, it is stopped first. The Wi-Fi driver,
 * network interface and service resources are then released.
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
 * @brief Get the Wi-Fi SoftAP running state.
 *
 * @param[out] started Set to true when the SoftAP is running.
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
 * @brief Get the current Wi-Fi SoftAP information.
 *
 * The connected-client list is queried directly from the Wi-Fi driver.
 * It currently contains the MAC address and RSSI of each station.
 * Client IPv4 addresses remain empty until DHCP lease lookup support
 * is implemented.
 *
 * The returned structure does not contain the SoftAP password.
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

#ifdef __cplusplus
}
#endif
