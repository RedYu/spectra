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
 * @file gui_theme.h
 * @brief Shared visual tokens used by Spectra GUI components.
 *
 * Initialize the theme after LVGL initialization and before creating
 * application screens or widgets.
 */

#define GUI_THEME_SPACE_XS               (4)
#define GUI_THEME_SPACE_SM               (8)
#define GUI_THEME_SPACE_MD               (12)
#define GUI_THEME_SPACE_LG               (16)
#define GUI_THEME_SPACE_XL               (24)

#define GUI_THEME_RADIUS_SM              (6)
#define GUI_THEME_RADIUS_MD              (10)
#define GUI_THEME_RADIUS_LG              (14)

#define GUI_THEME_BORDER_WIDTH           (1)
#define GUI_THEME_CONTROL_HEIGHT         (42)
#define GUI_THEME_TOOLBAR_HEIGHT         (56)
#define GUI_THEME_TOOLBAR_BUTTON_SIZE    (44)

typedef enum
{
    GUI_THEME_MODE_LIGHT = 0,
    GUI_THEME_MODE_DARK,

} gui_theme_mode_t;

/**
 * @brief Shared GUI color palette.
 */
typedef struct
{
    lv_color_t background;
    lv_color_t surface;
    lv_color_t surface_hover;
    lv_color_t input;
    lv_color_t border;

    lv_color_t text;
    lv_color_t text_secondary;
    lv_color_t text_muted;
    lv_color_t text_disabled;
    lv_color_t text_on_primary;

    lv_color_t primary;
    lv_color_t primary_hover;
    lv_color_t control_accent;

    lv_color_t success;
    lv_color_t warning;
    lv_color_t danger;

    /*
     * Toolbar uses its own dark palette in both GUI modes.
     */
    lv_color_t toolbar_background;
    lv_color_t toolbar_control;
    lv_color_t toolbar_control_pressed;
    lv_color_t toolbar_foreground;
    lv_color_t toolbar_inactive;

} gui_theme_colors_t;

/**
 * @brief Shared GUI font selection.
 */
typedef struct
{
    const lv_font_t *body;
    const lv_font_t *small;
    const lv_font_t *heading;
    const lv_font_t *title;

} gui_theme_fonts_t;

typedef struct
{
    lv_color_t overlay;

    lv_color_t background;
    lv_color_t border;

    lv_color_t title;
    lv_color_t text;
    lv_color_t progress_text;

    lv_color_t primary;
    lv_color_t primary_pressed;

    lv_color_t secondary;
    lv_color_t secondary_pressed;

    lv_color_t disabled;

    lv_color_t progress_background;
    lv_color_t progress_indicator;

} gui_theme_modal_colors_t;

typedef struct
{
    lv_color_t card_background;
    lv_color_t card_border;
    lv_color_t control_track;

} gui_theme_settings_colors_t;

typedef struct
{
    lv_color_t status_text;
    lv_color_t progress_background;
    lv_color_t progress_indicator;

} gui_theme_splash_colors_t;

/**
 * @brief Complete shared GUI theme.
 */
typedef struct
{
    gui_theme_mode_t mode;

    gui_theme_colors_t colors;
    gui_theme_fonts_t fonts;
    gui_theme_modal_colors_t modal;
    gui_theme_settings_colors_t settings;
    gui_theme_splash_colors_t splash;

} gui_theme_t;

/**
 * @brief Initialize the shared GUI theme.
 *
 * The function must be called from the GUI task after LVGL has been
 * initialized and before shared styles or GUI objects are created.
 *
 * Repeated initialization with the same mode is accepted. Changing the
 * active mode after initialization is not supported; the application
 * must restart to apply another theme.
 *
 * @param[in] mode Theme mode to initialize.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if mode is invalid,
 * or ESP_ERR_INVALID_STATE if another mode is already active.
 */
esp_err_t gui_theme_init(
    gui_theme_mode_t mode
);

/**
 * @brief Get the initialized shared GUI theme.
 *
 * The returned pointer remains valid for the application lifetime and
 * must not be modified or released.
 *
 * @return Theme pointer when initialized; otherwise NULL.
 */
const gui_theme_t *gui_theme_get(void);

/**
 * @brief Check whether the shared GUI theme is initialized.
 */
bool gui_theme_is_initialized(void);

#ifdef __cplusplus
}
#endif
