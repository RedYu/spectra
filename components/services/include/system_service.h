/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

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
 * @brief Stop the system monitoring service.
 *
 * Requests cooperative termination and waits until the monitoring task
 * releases the temperature sensor and stops accessing shared service
 * resources.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not running, or ESP_ERR_TIMEOUT if the task does not stop in time.
 */
esp_err_t system_service_stop(void);

/**
 * @brief Schedule a graceful device restart.
 *
 * The restart sequence runs asynchronously after the requested delay,
 * allowing callers such as HTTP handlers to finish sending their
 * responses.
 *
 * Before restarting, the shutdown service saves settings, stops
 * network and CAN services, stops background monitoring, closes file
 * logging and safely unmounts the SD card.
 *
 * Only one restart may be scheduled at a time. A zero delay starts the
 * graceful shutdown sequence immediately.
 *
 * @param[in] delay_ms Delay before starting graceful shutdown, in
 * milliseconds. The maximum supported delay is 60000 milliseconds.
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
