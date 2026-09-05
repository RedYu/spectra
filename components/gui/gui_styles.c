/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "gui_styles.h"

#include "gui_theme.h"

static bool s_initialized = false;

static lv_style_t s_screen;
static lv_style_t s_card;

static lv_style_t s_button_base;
static lv_style_t s_button_primary;
static lv_style_t s_button_primary_pressed;
static lv_style_t s_button_secondary;
static lv_style_t s_button_secondary_pressed;
static lv_style_t s_button_danger;
static lv_style_t s_button_disabled;

static lv_style_t s_input;
static lv_style_t s_input_focused;

static lv_style_t s_text_body;
static lv_style_t s_text_small;
static lv_style_t s_text_muted;
static lv_style_t s_text_heading;
static lv_style_t s_text_title;

static lv_style_t s_status_success;
static lv_style_t s_status_warning;
static lv_style_t s_status_danger;

static void gui_styles_init_screen(
    const gui_theme_t *theme
)
{
    lv_style_init(
        &s_screen
    );

    lv_style_set_bg_color(
        &s_screen,
        theme->colors.background
    );

    lv_style_set_bg_opa(
        &s_screen,
        LV_OPA_COVER
    );

    lv_style_set_text_color(
        &s_screen,
        theme->colors.text
    );

    lv_style_set_text_font(
        &s_screen,
        theme->fonts.body
    );

    lv_style_set_border_width(
        &s_screen,
        0
    );

    lv_style_set_radius(
        &s_screen,
        0
    );

    lv_style_set_pad_all(
        &s_screen,
        0
    );
}

static void gui_styles_init_card(
    const gui_theme_t *theme
)
{
    lv_style_init(
        &s_card
    );

    lv_style_set_bg_color(
        &s_card,
        theme->colors.surface
    );

    lv_style_set_bg_opa(
        &s_card,
        LV_OPA_COVER
    );

    lv_style_set_border_color(
        &s_card,
        theme->colors.border
    );

    lv_style_set_border_width(
        &s_card,
        GUI_THEME_BORDER_WIDTH
    );

    lv_style_set_radius(
        &s_card,
        GUI_THEME_RADIUS_LG
    );

    lv_style_set_pad_all(
        &s_card,
        GUI_THEME_SPACE_LG
    );

    lv_style_set_text_color(
        &s_card,
        theme->colors.text
    );

    lv_style_set_text_font(
        &s_card,
        theme->fonts.body
    );
}

static void gui_styles_init_buttons(
    const gui_theme_t *theme
)
{
    lv_style_init(
        &s_button_base
    );

    lv_style_set_radius(
        &s_button_base,
        GUI_THEME_RADIUS_MD
    );

    lv_style_set_pad_hor(
        &s_button_base,
        GUI_THEME_SPACE_LG
    );

    lv_style_set_pad_ver(
        &s_button_base,
        GUI_THEME_SPACE_SM
    );

    lv_style_set_border_width(
        &s_button_base,
        0
    );

    lv_style_set_text_font(
        &s_button_base,
        theme->fonts.body
    );

    lv_style_set_text_align(
        &s_button_base,
        LV_TEXT_ALIGN_CENTER
    );

    lv_style_init(
        &s_button_primary
    );

    lv_style_set_bg_color(
        &s_button_primary,
        theme->colors.primary
    );

    lv_style_set_bg_opa(
        &s_button_primary,
        LV_OPA_COVER
    );

    lv_style_set_text_color(
        &s_button_primary,
        theme->colors.text_on_primary
    );

    lv_style_init(
        &s_button_primary_pressed
    );

    lv_style_set_bg_color(
        &s_button_primary_pressed,
        theme->colors.primary_hover
    );

    lv_style_set_bg_opa(
        &s_button_primary_pressed,
        LV_OPA_COVER
    );

    lv_style_init(
        &s_button_secondary
    );

    lv_style_set_bg_color(
        &s_button_secondary,
        theme->colors.input
    );

    lv_style_set_bg_opa(
        &s_button_secondary,
        LV_OPA_COVER
    );

    lv_style_set_border_color(
        &s_button_secondary,
        theme->colors.primary
    );

    lv_style_set_border_width(
        &s_button_secondary,
        GUI_THEME_BORDER_WIDTH
    );

    lv_style_set_text_color(
        &s_button_secondary,
        theme->colors.primary
    );

    lv_style_init(
        &s_button_secondary_pressed
    );

    lv_style_set_bg_color(
        &s_button_secondary_pressed,
        theme->colors.surface_hover
    );

    lv_style_set_bg_opa(
        &s_button_secondary_pressed,
        LV_OPA_COVER
    );

    lv_style_set_border_color(
        &s_button_secondary_pressed,
        theme->colors.primary_hover
    );

    lv_style_set_text_color(
        &s_button_secondary_pressed,
        theme->colors.primary_hover
    );

    lv_style_init(
        &s_button_danger
    );

    lv_style_set_bg_color(
        &s_button_danger,
        theme->colors.danger
    );

    lv_style_set_bg_opa(
        &s_button_danger,
        LV_OPA_20
    );

    lv_style_set_border_color(
        &s_button_danger,
        theme->colors.danger
    );

    lv_style_set_border_width(
        &s_button_danger,
        GUI_THEME_BORDER_WIDTH
    );

    lv_style_set_text_color(
        &s_button_danger,
        theme->colors.danger
    );

    lv_style_init(
        &s_button_disabled
    );

    lv_style_set_opa(
        &s_button_disabled,
        LV_OPA_50
    );
}

static void gui_styles_init_inputs(
    const gui_theme_t *theme
)
{
    lv_style_init(
        &s_input
    );

    lv_style_set_bg_color(
        &s_input,
        theme->colors.input
    );

    lv_style_set_bg_opa(
        &s_input,
        LV_OPA_COVER
    );

    lv_style_set_border_color(
        &s_input,
        theme->colors.border
    );

    lv_style_set_border_width(
        &s_input,
        GUI_THEME_BORDER_WIDTH
    );

    lv_style_set_radius(
        &s_input,
        GUI_THEME_RADIUS_MD
    );

    lv_style_set_pad_hor(
        &s_input,
        GUI_THEME_SPACE_MD
    );

    lv_style_set_pad_ver(
        &s_input,
        GUI_THEME_SPACE_SM
    );

    lv_style_set_text_color(
        &s_input,
        theme->colors.text_secondary
    );

    lv_style_set_text_font(
        &s_input,
        theme->fonts.body
    );

    lv_style_init(
        &s_input_focused
    );

    lv_style_set_border_color(
        &s_input_focused,
        theme->colors.control_accent
    );

    lv_style_set_outline_color(
        &s_input_focused,
        theme->colors.control_accent
    );

    lv_style_set_outline_width(
        &s_input_focused,
        2
    );

    lv_style_set_outline_opa(
        &s_input_focused,
        LV_OPA_20
    );

    lv_style_set_outline_pad(
        &s_input_focused,
        1
    );
}

static void gui_styles_init_text(
    const gui_theme_t *theme
)
{
    lv_style_init(
        &s_text_body
    );

    lv_style_set_text_color(
        &s_text_body,
        theme->colors.text_secondary
    );

    lv_style_set_text_font(
        &s_text_body,
        theme->fonts.body
    );

    lv_style_init(
        &s_text_small
    );

    lv_style_set_text_color(
        &s_text_small,
        theme->colors.text_secondary
    );

    lv_style_set_text_font(
        &s_text_small,
        theme->fonts.small
    );

    lv_style_init(
        &s_text_muted
    );

    lv_style_set_text_color(
        &s_text_muted,
        theme->colors.text_muted
    );

    lv_style_set_text_font(
        &s_text_muted,
        theme->fonts.small
    );

    lv_style_init(
        &s_text_heading
    );

    lv_style_set_text_color(
        &s_text_heading,
        theme->colors.text
    );

    lv_style_set_text_font(
        &s_text_heading,
        theme->fonts.heading
    );

    lv_style_init(
        &s_text_title
    );

    lv_style_set_text_color(
        &s_text_title,
        theme->colors.text
    );

    lv_style_set_text_font(
        &s_text_title,
        theme->fonts.title
    );
}

static void gui_styles_init_status(
    const gui_theme_t *theme
)
{
    lv_style_init(
        &s_status_success
    );

    lv_style_set_text_color(
        &s_status_success,
        theme->colors.success
    );

    lv_style_init(
        &s_status_warning
    );

    lv_style_set_text_color(
        &s_status_warning,
        theme->colors.warning
    );

    lv_style_init(
        &s_status_danger
    );

    lv_style_set_text_color(
        &s_status_danger,
        theme->colors.danger
    );
}

esp_err_t gui_styles_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    const gui_theme_t *theme =
        gui_theme_get();

    if (theme == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    gui_styles_init_screen(
        theme
    );

    gui_styles_init_card(
        theme
    );

    gui_styles_init_buttons(
        theme
    );

    gui_styles_init_inputs(
        theme
    );

    gui_styles_init_text(
        theme
    );

    gui_styles_init_status(
        theme
    );

    s_initialized = true;

    return ESP_OK;
}

bool gui_styles_is_initialized(void)
{
    return s_initialized;
}

#define GUI_STYLE_GETTER(function_name, style_name) \
    const lv_style_t *function_name(void)           \
    {                                               \
        return s_initialized                        \
            ? &(style_name)                         \
            : NULL;                                 \
    }

GUI_STYLE_GETTER(
    gui_styles_screen,
    s_screen
)

GUI_STYLE_GETTER(
    gui_styles_card,
    s_card
)

GUI_STYLE_GETTER(
    gui_styles_button_base,
    s_button_base
)

GUI_STYLE_GETTER(
    gui_styles_button_primary,
    s_button_primary
)

GUI_STYLE_GETTER(
    gui_styles_button_primary_pressed,
    s_button_primary_pressed
)

GUI_STYLE_GETTER(
    gui_styles_button_secondary,
    s_button_secondary
)

GUI_STYLE_GETTER(
    gui_styles_button_secondary_pressed,
    s_button_secondary_pressed
)

GUI_STYLE_GETTER(
    gui_styles_button_danger,
    s_button_danger
)

GUI_STYLE_GETTER(
    gui_styles_button_disabled,
    s_button_disabled
)

GUI_STYLE_GETTER(
    gui_styles_input,
    s_input
)

GUI_STYLE_GETTER(
    gui_styles_input_focused,
    s_input_focused
)

GUI_STYLE_GETTER(
    gui_styles_text_body,
    s_text_body
)

GUI_STYLE_GETTER(
    gui_styles_text_small,
    s_text_small
)

GUI_STYLE_GETTER(
    gui_styles_text_muted,
    s_text_muted
)

GUI_STYLE_GETTER(
    gui_styles_text_heading,
    s_text_heading
)

GUI_STYLE_GETTER(
    gui_styles_text_title,
    s_text_title
)

GUI_STYLE_GETTER(
    gui_styles_status_success,
    s_status_success
)

GUI_STYLE_GETTER(
    gui_styles_status_warning,
    s_status_warning
)

GUI_STYLE_GETTER(
    gui_styles_status_danger,
    s_status_danger
)

#undef GUI_STYLE_GETTER
