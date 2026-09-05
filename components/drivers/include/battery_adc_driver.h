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
 * @brief Initialize the battery-voltage ADC driver.
 *
 * Configures the ADC channel connected to the battery-voltage divider
 * and initializes the ADC calibration scheme.
 *
 * Repeated calls are accepted and do not recreate driver resources.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t battery_adc_driver_init(void);

/**
 * @brief Release battery ADC resources.
 *
 * Calling this function when the driver is not initialized has no
 * effect.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t battery_adc_driver_deinit(void);

/**
 * @brief Read the battery voltage in millivolts.
 *
 * Multiple ADC samples are averaged before calibration. The calibrated
 * ADC-pin voltage is converted using the resistor-divider ratio.
 *
 * @param[out] voltage_mv Measured battery voltage in millivolts.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if voltage_mv is NULL,
 * ESP_ERR_INVALID_STATE if the driver is not initialized, otherwise an
 * ESP-IDF error code.
 */
esp_err_t battery_adc_driver_get_voltage_mv(
    uint16_t *voltage_mv
);

/**
 * @brief Check whether the battery ADC driver is initialized.
 *
 * @return true when the ADC unit and calibration resources are ready.
 */
bool battery_adc_driver_is_initialized(void);

#ifdef __cplusplus
}
#endif
