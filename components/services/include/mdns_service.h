#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MDNS_SERVICE_HOSTNAME_MAX_LENGTH       (32U)
#define MDNS_SERVICE_INSTANCE_NAME_MAX_LENGTH  (64U)
#define MDNS_SERVICE_TYPE_MAX_LENGTH           (32U)

/**
 * @brief Current mDNS service information.
 *
 * String-array sizes include space for the terminating null character.
 * The hostname does not include the ".local" suffix.
 */
typedef struct
{
    bool started;

    char hostname[
        MDNS_SERVICE_HOSTNAME_MAX_LENGTH
    ];

    char instance_name[
        MDNS_SERVICE_INSTANCE_NAME_MAX_LENGTH
    ];

    char service[
        MDNS_SERVICE_TYPE_MAX_LENGTH
    ];

    uint16_t port;

} mdns_service_info_t;

/**
 * @brief Start local mDNS discovery.
 *
 * The service publishes the device as:
 *
 *     spectra-XXXXXX.local
 *
 * where XXXXXX contains the final six hexadecimal characters of the
 * Wi-Fi Station MAC address.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * already started, otherwise an ESP-IDF error code.
 */
esp_err_t mdns_service_start(void);

/**
 * @brief Stop mDNS discovery and release its resources.
 *
 * Calling this function when the service is not running has no effect.
 */
void mdns_service_stop(void);

/**
 * @brief Restart the mDNS responder.
 *
 * Use this function after a Wi-Fi mode or network-interface change.
 * The function stops the current responder and registers the hostname
 * and HTTP service again.
 *
 * This function must not be called from the ESP event-loop task.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t mdns_service_refresh(void);

/**
 * @brief Get the current mDNS service information.
 *
 * This function also succeeds when mDNS is stopped. In that case,
 * started is false and hostname is empty. Static service information,
 * including the instance name, service type and port, is still
 * returned.
 *
 * @param[out] info Destination information structure.
 *
 * @return ESP_OK on success or ESP_ERR_INVALID_ARG if info is NULL.
 */
esp_err_t mdns_service_get_info(
    mdns_service_info_t *info
);

/**
 * @brief Copy the current mDNS hostname without the .local suffix.
 *
 * Unlike mdns_service_get_info(), this function requires the service
 * to be running.
 *
 * @param[out] hostname Destination buffer.
 * @param[in] hostname_size Destination buffer size.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid arguments,
 * ESP_ERR_INVALID_STATE if the service is not started, or
 * ESP_ERR_INVALID_SIZE if the destination buffer is too small.
 */
esp_err_t mdns_service_get_hostname(
    char *hostname,
    size_t hostname_size
);

#ifdef __cplusplus
}
#endif
