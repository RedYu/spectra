#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MDNS_SERVICE_HOSTNAME_MAX_LENGTH  (32U)

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
 * @brief Copy the current mDNS hostname without the .local suffix.
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
