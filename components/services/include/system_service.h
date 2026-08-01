#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the system monitoring service.
 *
 * The service periodically updates runtime information such as
 * uptime, heap usage and CPU usage in the system model.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service
 * is already running, ESP_ERR_NO_MEM if the service task cannot
 * be created, otherwise an ESP-IDF error code.
 */
esp_err_t system_service_start(void);

/**
 * @brief Request cooperative termination of the system service.
 *
 * The function returns immediately. The service task finishes its
 * current update, releases temporary resources and then terminates.
 * Calling this function when the service is not running has no effect.
 */
void system_service_stop(void);

#ifdef __cplusplus
}
#endif
