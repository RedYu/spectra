#pragma once

#include <stdint.h>

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

/**
 * @brief Schedule a device restart.
 *
 * The restart is performed asynchronously after the requested delay,
 * allowing the caller to finish operations such as sending an HTTP
 * response.
 *
 * Only one restart may be scheduled at a time. A zero delay requests
 * an immediate asynchronous restart. Callers that need to complete an
 * operation before restarting must provide a sufficient delay.
 *
 * The caller is responsible for saving settings, flushing persistent
 * data and completing any pending response before the restart occurs.
 *
 * @param[in] delay_ms Delay before restart in milliseconds. The
 * maximum supported delay is 60000 milliseconds.
 *
 * @return ESP_OK when the restart task is created,
 * ESP_ERR_INVALID_ARG if delay_ms exceeds the supported maximum,
 * ESP_ERR_INVALID_STATE if a restart is already scheduled,
 * ESP_ERR_NO_MEM if the restart task cannot be created, otherwise an
 * ESP-IDF error code.
 */
esp_err_t system_service_schedule_restart(
    uint32_t delay_ms
);

#ifdef __cplusplus
}
#endif
