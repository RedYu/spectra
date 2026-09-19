/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file gui_feedback.h
 * @brief Audible feedback for LVGL controls.
 *
 * All functions must be called from the GUI task because they access
 * LVGL objects directly.
 */

/**
 * @brief Initialize global audible feedback for one input device.
 *
 * The input-device event receives every pressed control, including objects
 * created internally by compound widgets such as tab views and dialogs.
 * Call once from the GUI task after the LVGL input device is registered.
 *
 * @param[in] input Input device used by the GUI.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if input is NULL, or
 * ESP_ERR_INVALID_STATE if global feedback is already initialized.
 */
esp_err_t gui_feedback_init(
    lv_indev_t *input
);

/**
 * @brief Attach audible press feedback to an LVGL object.
 *
 * This compatibility helper is only needed when global input feedback has
 * not been initialized. Once gui_feedback_init() succeeds, it has no effect.
 *
 * @param[in] object LVGL object receiving the feedback callback.
 * NULL is accepted and has no effect.
 */
void gui_feedback_attach(
    lv_obj_t *object
);

#ifdef __cplusplus
}
#endif
