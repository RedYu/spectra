#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start periodic Spectra backend availability checks.
 *
 * The service periodically sends an HTTPS request to the configured
 * Spectra backend. The result is stored in the system model as the
 * current Internet availability state.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * already running, ESP_ERR_NO_MEM if the service task cannot be
 * created, otherwise an ESP-IDF error code.
 */
esp_err_t internet_service_start(void);

/**
 * @brief Request cooperative termination of the Internet service.
 *
 * The function returns immediately. The service task is notified and
 * terminates after the current HTTPS request has completed. Calling
 * this function when the service is not running has no effect.
 */
void internet_service_stop(void);

#ifdef __cplusplus
}
#endif
