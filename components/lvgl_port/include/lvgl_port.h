/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize LVGL and register the display and touch input.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t lvgl_port_init(void);

/**
 * @brief Run the LVGL timer handler.
 *
 * This function must only be called from the GUI task.
 *
 * @return Recommended delay in milliseconds before the next call.
 */
uint32_t lvgl_port_handler(void);

/**
 * @brief Get the registered LVGL display.
 *
 * @return LVGL display instance, or NULL if the LVGL port has not
 * been initialized.
 */
lv_display_t *lvgl_port_get_display(void);

/**
 * @brief Get the registered LVGL touch input device.
 *
 * @return LVGL input device instance, or NULL if the LVGL port has
 * not been initialized.
 */
lv_indev_t *lvgl_port_get_touch(void);

#ifdef __cplusplus
}
#endif
