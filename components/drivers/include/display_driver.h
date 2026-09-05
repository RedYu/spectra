/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the display panel and its communication interface.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t display_driver_init(void);

/**
 * @brief Get the initialized display panel handle.
 *
 * @return Display panel handle, or NULL if the display driver
 * has not been initialized.
 */
esp_lcd_panel_handle_t display_driver_get_panel(void);

/**
 * @brief Get the initialized display panel I/O handle.
 *
 * @return Display panel I/O handle, or NULL if the display driver
 * has not been initialized.
 */
esp_lcd_panel_io_handle_t display_driver_get_panel_io(void);

#ifdef __cplusplus
}
#endif
