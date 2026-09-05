/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Current battery information.
 */
typedef struct
{
    /**
     * True after at least one successful ADC measurement.
     */
    bool measurement_valid;

    /**
     * True when measured voltage indicates that a battery is present.
     */
    bool battery_present;

    /**
     * Filtered battery voltage in millivolts.
     */
    uint16_t voltage_mv;

    /**
     * Approximate battery charge level from 0 to 100 percent.
     */
    uint8_t level_percent;

    /**
     * Time of the latest successful measurement.
     */
    uint64_t last_update_ms;

} battery_service_info_t;

/**
 * @brief Start periodic battery monitoring.
 *
 * Initializes the battery ADC driver and starts the monitoring task.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already running,
 * ESP_ERR_NO_MEM if required resources cannot be created, otherwise an
 * ESP-IDF error code.
 */
esp_err_t battery_service_start(void);

/**
 * @brief Stop battery monitoring and release ADC resources.
 *
 * The function waits for the monitoring task to terminate before
 * releasing the battery ADC driver.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not running, ESP_ERR_TIMEOUT if the task does not terminate within
 * the shutdown timeout, otherwise an ESP-IDF error code.
 */
esp_err_t battery_service_stop(void);

/**
 * @brief Copy the current battery information.
 *
 * @param[out] info Destination information structure.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if info is NULL,
 * ESP_ERR_INVALID_STATE if the service is not running, or
 * ESP_ERR_TIMEOUT if the service lock cannot be acquired.
 */
esp_err_t battery_service_get_info(
    battery_service_info_t *info
);

/**
 * @brief Check whether battery monitoring is running.
 */
bool battery_service_is_running(void);

#ifdef __cplusplus
}
#endif
