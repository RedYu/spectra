#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Schedule a graceful device restart.
 *
 * The shutdown task waits for the requested delay, stops network
 * services, closes logging, unmounts the SD card and restarts the
 * device.
 *
 * Only one restart can be scheduled during the current boot.
 *
 * @param[in] delay_ms Delay before shutdown starts, in milliseconds.
 *
 * @return ESP_OK when scheduled, ESP_ERR_INVALID_STATE if a restart
 * is already scheduled, or ESP_ERR_NO_MEM if the shutdown task cannot
 * be created.
 */
esp_err_t shutdown_service_schedule_restart(
    uint32_t delay_ms
);

/**
 * @brief Check whether a graceful restart has been scheduled.
 */
bool shutdown_service_is_restart_scheduled(void);

#ifdef __cplusplus
}
#endif
