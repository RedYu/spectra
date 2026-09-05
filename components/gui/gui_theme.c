/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "gui_theme.h"

#include <string.h>

static gui_theme_t s_theme;
static bool s_initialized = false;

static void gui_theme_set_light_palette(
    gui_theme_t *theme
)
{
    if (theme == NULL) {
        return;
    }

    theme->colors.background =
        lv_color_hex(0xFFFFFFU);

    theme->colors.surface =
        lv_color_hex(0xF5F7FAU);

    theme->colors.surface_hover =
        lv_color_hex(0xEEF2F7U);

    theme->colors.input =
        lv_color_hex(0xFFFFFFU);

    theme->colors.border =
        lv_color_hex(0xE2E8F0U);

    theme->colors.text =
        lv_color_hex(0x111827U);

    theme->colors.text_secondary =
        lv_color_hex(0x374151U);

    theme->colors.text_muted =
        lv_color_hex(0x6B7280U);

    theme->colors.text_disabled =
        lv_color_hex(0x9CA3AFU);

    theme->colors.text_on_primary =
        lv_color_hex(0xFFFFFFU);

    theme->colors.primary =
        lv_color_hex(0x2563EBU);

    theme->colors.primary_hover =
        lv_color_hex(0x1D4ED8U);

    theme->colors.control_accent =
        lv_color_hex(0x4B77D1U);

    theme->colors.success =
        lv_color_hex(0x39C56BU);

    theme->colors.warning =
        lv_color_hex(0xF4B400U);

    theme->colors.danger =
        lv_color_hex(0xE05252U);

    theme->colors.toolbar_background =
        lv_color_hex(0x20252AU);

    theme->colors.toolbar_control =
        lv_color_hex(0x343B42U);

    theme->colors.toolbar_control_pressed =
        lv_color_hex(0x4B77D1U);

    theme->colors.toolbar_foreground =
        lv_color_hex(0xFFFFFFU);

    theme->colors.toolbar_inactive =
        lv_color_hex(0x7B858FU);

    theme->modal.overlay =
        lv_color_hex(0x000000U);

    theme->modal.background =
        lv_color_hex(0xFFFFFFU);

    theme->modal.border =
        lv_color_hex(0xD8DEE5U);

    theme->modal.title =
        lv_color_hex(0x1F2933U);

    theme->modal.text =
        lv_color_hex(0x4B5563U);

    theme->modal.progress_text =
        lv_color_hex(0x6B7280U);

    theme->modal.primary =
        lv_color_hex(0x4B77D1U);

    theme->modal.primary_pressed =
        lv_color_hex(0x3F66B8U);

    theme->modal.secondary =
        lv_color_hex(0xE5E7EBU);

    theme->modal.secondary_pressed =
        lv_color_hex(0xD1D5DBU);

    theme->modal.disabled =
        lv_color_hex(0xBFC6CEU);

    theme->modal.progress_background =
        lv_color_hex(0xE5E7EBU);

    theme->modal.progress_indicator =
        lv_color_hex(0x4B77D1U);

    theme->settings.card_background =
        lv_color_hex(0xF7F8FAU);

    theme->settings.card_border =
        lv_color_hex(0xE1E5EAU);

    theme->settings.control_track =
        lv_color_hex(0xD9DEE5U);

    theme->splash.status_text =
        lv_color_hex(0xA0A8B0U);

    theme->splash.progress_background =
        lv_color_hex(0xE5E7EBU);

    theme->splash.progress_indicator =
        lv_color_hex(0x4B77D1U);
}

static void gui_theme_set_dark_palette(
    gui_theme_t *theme
)
{
    if (theme == NULL) {
        return;
    }

    /*
     * Base surfaces.
     */
    theme->colors.background =
        lv_color_hex(0x0B0F14U);

    theme->colors.surface =
        lv_color_hex(0x111821U);

    theme->colors.surface_hover =
        lv_color_hex(0x18222DU);

    theme->colors.input =
        lv_color_hex(0x0F151DU);

    theme->colors.border =
        lv_color_hex(0x27313DU);

    /*
     * Text colors.
     */
    theme->colors.text =
        lv_color_hex(0xE7EDF4U);

    theme->colors.text_muted =
        lv_color_hex(0x91A0AFU);

    theme->colors.text_disabled =
        lv_color_hex(0x5F6C79U);

    theme->colors.text_on_primary =
        lv_color_hex(0xFFFFFFU);

    theme->colors.text_secondary =
        lv_color_hex(0xD1D5DBU);

    theme->colors.control_accent =
        lv_color_hex(0x60A5FAU);

    /*
     * Semantic colors.
     */
    theme->colors.primary =
        lv_color_hex(0x3B82F6U);

    theme->colors.primary_hover =
        lv_color_hex(0x2563EBU);

    theme->colors.success =
        lv_color_hex(0x22C55EU);

    theme->colors.warning =
        lv_color_hex(0xF59E0BU);

    theme->colors.danger =
        lv_color_hex(0xF05252U);

    theme->colors.toolbar_background =
        lv_color_hex(0x20252AU);

    theme->colors.toolbar_control =
        lv_color_hex(0x343B42U);

    theme->colors.toolbar_control_pressed =
        lv_color_hex(0x4B77D1U);

    theme->colors.toolbar_foreground =
        lv_color_hex(0xFFFFFFU);

    theme->colors.toolbar_inactive =
        lv_color_hex(0x7B858FU);

    theme->modal.overlay =
        lv_color_hex(0x000000U);

    theme->modal.background =
        lv_color_hex(0x111821U);

    theme->modal.border =
        lv_color_hex(0x27313DU);

    theme->modal.title =
        lv_color_hex(0xE7EDF4U);

    theme->modal.text =
        lv_color_hex(0xD1D5DBU);

    theme->modal.progress_text =
        lv_color_hex(0x91A0AFU);

    theme->modal.primary =
        lv_color_hex(0x60A5FAU);

    theme->modal.primary_pressed =
        lv_color_hex(0x3B82F6U);

    theme->modal.secondary =
        lv_color_hex(0x27313DU);

    theme->modal.secondary_pressed =
        lv_color_hex(0x344150U);

    theme->modal.disabled =
        lv_color_hex(0x4B5563U);

    theme->modal.progress_background =
        lv_color_hex(0x27313DU);

    theme->modal.progress_indicator =
        lv_color_hex(0x60A5FAU);

    theme->settings.card_background =
        lv_color_hex(0x111821U);

    theme->settings.card_border =
        lv_color_hex(0x27313DU);

    theme->settings.control_track =
        lv_color_hex(0x27313DU);

    theme->splash.status_text =
        lv_color_hex(0x91A0AFU);

    theme->splash.progress_background =
        lv_color_hex(0x27313DU);

    theme->splash.progress_indicator =
        lv_color_hex(0x60A5FAU);
}

esp_err_t gui_theme_init(
    gui_theme_mode_t mode
)
{
    if ((mode != GUI_THEME_MODE_LIGHT) &&
        (mode != GUI_THEME_MODE_DARK)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (s_initialized) {
        return s_theme.mode == mode
            ? ESP_OK
            : ESP_ERR_INVALID_STATE;
    }

    memset(
        &s_theme,
        0,
        sizeof(s_theme)
    );

    s_theme.mode = mode;

    if (mode == GUI_THEME_MODE_DARK) {
        gui_theme_set_dark_palette(
            &s_theme
        );
    } else {
        gui_theme_set_light_palette(
            &s_theme
        );
    }

    /*
     * Font initialization remains shared by both themes.
     */
    s_theme.fonts.small =
        &lv_font_montserrat_14;

    s_theme.fonts.body =
        &lv_font_montserrat_16;

    s_theme.fonts.heading =
        &lv_font_montserrat_18;

    s_theme.fonts.title =
        &lv_font_montserrat_20;

    s_initialized = true;

    return ESP_OK;
}

const gui_theme_t *gui_theme_get(void)
{
    if (!s_initialized) {
        return NULL;
    }

    return &s_theme;
}

bool gui_theme_is_initialized(void)
{
    return s_initialized;
}
