#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the USB RNDIS network interface.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t usb_network_service_init(void);

#ifdef __cplusplus
}
#endif
