/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#define SPECTRA_APP_NAME   "Modern Automotive CAN Analyzer"
#define SPECTRA_APP_TARGET "spectra"

/*
 * LVGL renders the screen using partial updates.
 * RGB565 requires 2 bytes per pixel.
 */
#define LVGL_DRAW_BUFFER_LINES   (40U)

#define LVGL_TICK_PERIOD_MS      (2U)
#define LVGL_HANDLER_MIN_MS      (2U)
#define LVGL_HANDLER_MAX_MS      (20U)

#define LVGL_DRAW_BUFFER_PIXELS  (LCD_H_RES * LVGL_DRAW_BUFFER_LINES)
