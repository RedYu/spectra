/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the touch controller.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t touch_driver_init(void);

/**
 * @brief Get the initialized touch controller handle.
 *
 * @return Touch controller handle, or NULL if the driver has not
 * been initialized.
 */
esp_lcd_touch_handle_t touch_driver_get_handle(void);

/**
 * @brief Read the current touch state and coordinates.
 *
 * @param[out] x Horizontal touch coordinate.
 * @param[out] y Vertical touch coordinate.
 * @param[out] pressed true when the display is being touched;
 * otherwise false.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if any output
 * pointer is NULL, ESP_ERR_INVALID_STATE if the driver has not
 * been initialized, otherwise an ESP-IDF error code.
 */
esp_err_t touch_driver_read(
    uint16_t *x,
    uint16_t *y,
    bool *pressed
);

#ifdef __cplusplus
}
#endif
