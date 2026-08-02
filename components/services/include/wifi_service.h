#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SERVICE_AP_IP_ADDRESS_MAX_LENGTH  (16U)
#define WIFI_SERVICE_AP_SSID_MAX_LENGTH        (33U)

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
 * driver, network interface and synchronization resources. The SoftAP
 * can be started again by calling wifi_service_start().
 *
 * Calling this function when the service is initialized but already
 * stopped has no effect and returns ESP_OK.
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
 * network interface and service resources are then released. After
 * this function succeeds, wifi_service_init() must be called before
 * the service can be started again.
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
 * @param[out] started Set to true when the SoftAP is running; otherwise
 * set to false.
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

#ifdef __cplusplus
}
#endif
