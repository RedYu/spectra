/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file gui_styles.h
 * @brief Shared LVGL styles used by Spectra screens and widgets.
 *
 * Initialize the styles after gui_theme_init() and before creating
 * application screens.
 */

/**
 * @brief Initialize all shared GUI styles.
 *
 * The function must be called from the GUI task. Repeated calls are
 * accepted and do not recreate the styles.
 *
 * @return ESP_OK on success or ESP_ERR_INVALID_STATE if the shared
 * GUI theme is not initialized.
 */
esp_err_t gui_styles_init(void);

/**
 * @brief Check whether shared GUI styles are initialized.
 */
bool gui_styles_is_initialized(void);

/**
 * @brief Get shared GUI styles.
 *
 * Returned pointers remain valid for the application lifetime and must
 * not be modified, reset or released by callers.
 *
 * Each function returns the corresponding style after successful
 * gui_styles_init(), or NULL when styles are not initialized.
 */
const lv_style_t *gui_styles_screen(void);
const lv_style_t *gui_styles_card(void);

const lv_style_t *gui_styles_button_base(void);
const lv_style_t *gui_styles_button_primary(void);
const lv_style_t *gui_styles_button_primary_pressed(void);
const lv_style_t *gui_styles_button_secondary(void);
const lv_style_t *gui_styles_button_secondary_pressed(void);
const lv_style_t *gui_styles_button_danger(void);
const lv_style_t *gui_styles_button_disabled(void);

const lv_style_t *gui_styles_input(void);
const lv_style_t *gui_styles_input_focused(void);

const lv_style_t *gui_styles_text_body(void);
const lv_style_t *gui_styles_text_small(void);
const lv_style_t *gui_styles_text_muted(void);
const lv_style_t *gui_styles_text_heading(void);
const lv_style_t *gui_styles_text_title(void);

const lv_style_t *gui_styles_status_success(void);
const lv_style_t *gui_styles_status_warning(void);
const lv_style_t *gui_styles_status_danger(void);

#ifdef __cplusplus
}
#endif
