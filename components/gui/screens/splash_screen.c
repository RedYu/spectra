/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "screens/splash_screen.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"

#include "assets/gui_images.h"
#include "gui_config.h"
#include "gui_styles.h"
#include "gui_theme.h"
#include "screen_manager.h"

#define SPLASH_PROGRESS_MAX          (100U)
#define SPLASH_TRANSITION_TIME_MS    (300U)

#define SPLASH_LOGO_OFFSET_Y         (-45)
#define SPLASH_STATUS_OFFSET_Y       (-48)
#define SPLASH_PROGRESS_OFFSET_Y     (-25)

#define SPLASH_PROGRESS_WIDTH        (280)
#define SPLASH_PROGRESS_HEIGHT       (8)

#define SPLASH_COMPLETION_HOLD_TIME_MS  (500U)

static const char *TAG = "splash_screen";

typedef struct
{
    lv_obj_t *root;
    lv_timer_t *timer;
    lv_obj_t *progress_bar;
    lv_obj_t *status_label;

    uint8_t progress;
    bool active;

} splash_screen_context_t;

static splash_screen_context_t s_context = {0};

static void splash_screen_delete_event_cb(
    lv_event_t *event
)
{
    (void)event;

    if (s_context.timer != NULL) {
        lv_timer_delete(
            s_context.timer
        );

        s_context.timer = NULL;
    }

    memset(
        &s_context,
        0,
        sizeof(s_context)
    );
}

static void splash_screen_timer_cb(
    lv_timer_t *timer
)
{
    (void)timer;

    s_context.timer = NULL;

    if (!s_context.active ||
        (s_context.progress < SPLASH_PROGRESS_MAX)) {

        return;
    }

    const bool animations_enabled =
        gui_config_get_animations_enabled();

    const lv_screen_load_anim_t animation =
        animations_enabled
            ? LV_SCR_LOAD_ANIM_FADE_IN
            : LV_SCR_LOAD_ANIM_NONE;

    const uint32_t animation_time_ms =
        animations_enabled
            ? SPLASH_TRANSITION_TIME_MS
            : 0U;

    const esp_err_t result =
        screen_manager_show(
            SCREEN_ID_MAIN,
            animation,
            animation_time_ms
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to show main screen: %s",
            esp_err_to_name(result)
        );

        return;
    }

    /*
     * The splash screen must not be reachable through Back navigation.
     */
    screen_manager_clear_history();
}

static void splash_screen_start_transition_timer(void)
{
    if (!s_context.active ||
        (s_context.progress < SPLASH_PROGRESS_MAX) ||
        (s_context.timer != NULL)) {

        return;
    }

    /*
     * Loading is complete. Leave only the logo visible while
     * waiting before transitioning to the main screen.
     */
    if (s_context.status_label != NULL) {
        lv_obj_add_flag(
            s_context.status_label,
            LV_OBJ_FLAG_HIDDEN
        );
    }

    if (s_context.progress_bar != NULL) {
        lv_obj_add_flag(
            s_context.progress_bar,
            LV_OBJ_FLAG_HIDDEN
        );
    }

    s_context.timer =
        lv_timer_create(
            splash_screen_timer_cb,
            SPLASH_COMPLETION_HOLD_TIME_MS,
            NULL
        );

    if (s_context.timer == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create splash-screen timer"
        );

        return;
    }

    lv_timer_set_repeat_count(
        s_context.timer,
        1
    );
}

lv_obj_t *splash_screen_create(void)
{
    if (!gui_theme_is_initialized() ||
        !gui_styles_is_initialized()) {

        ESP_LOGE(
            TAG,
            "GUI theme or styles are not initialized"
        );

        return NULL;
    }

    const gui_theme_t *theme =
        gui_theme_get();

    if (theme == NULL) {
        return NULL;
    }

    lv_obj_t *screen =
        lv_obj_create(NULL);

    if (screen == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create splash screen"
        );

        return NULL;
    }

    memset(
        &s_context,
        0,
        sizeof(s_context)
    );

    s_context.root = screen;

    lv_obj_add_event_cb(
        screen,
        splash_screen_delete_event_cb,
        LV_EVENT_DELETE,
        NULL
    );

    lv_obj_remove_flag(
        screen,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_add_style(
        screen,
        gui_styles_screen(),
        LV_PART_MAIN
    );

    lv_obj_t *logo_image =
        lv_image_create(screen);

    if (logo_image == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create splash-screen logo"
        );

        lv_obj_delete(screen);
        return NULL;
    }

    lv_image_set_src(
        logo_image,
        &dev_logo
    );

    lv_obj_align(
        logo_image,
        LV_ALIGN_CENTER,
        0,
        SPLASH_LOGO_OFFSET_Y
    );

    if ((theme != NULL) &&
        (theme->mode == GUI_THEME_MODE_DARK)) {

        lv_obj_set_style_image_recolor(
            logo_image,
            lv_color_white(),
            LV_PART_MAIN
        );

        lv_obj_set_style_image_recolor_opa(
            logo_image,
            LV_OPA_COVER,
            LV_PART_MAIN
        );
    }

    s_context.status_label =
        lv_label_create(screen);

    if (s_context.status_label == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create splash-screen status label"
        );

        lv_obj_delete(
            screen
        );

        return NULL;
    }

    lv_label_set_text(
        s_context.status_label,
        "Initializing"
    );

    lv_obj_add_style(
        s_context.status_label,
        gui_styles_text_body(),
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        s_context.status_label,
        theme->splash.status_text,
        LV_PART_MAIN
    );

    lv_obj_align(
        s_context.status_label,
        LV_ALIGN_BOTTOM_MID,
        0,
        SPLASH_STATUS_OFFSET_Y
    );

    s_context.progress_bar =
        lv_bar_create(screen);

    if (s_context.progress_bar == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create splash-screen progress bar"
        );

        lv_obj_delete(
            screen
        );

        return NULL;
    }

    lv_obj_set_size(
        s_context.progress_bar,
        SPLASH_PROGRESS_WIDTH,
        SPLASH_PROGRESS_HEIGHT
    );

    lv_obj_set_style_radius(
        s_context.progress_bar,
        LV_RADIUS_CIRCLE,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        s_context.progress_bar,
        theme->splash.progress_background,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        s_context.progress_bar,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_set_style_border_width(
        s_context.progress_bar,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_radius(
        s_context.progress_bar,
        LV_RADIUS_CIRCLE,
        LV_PART_INDICATOR
    );

    lv_obj_set_style_bg_color(
        s_context.progress_bar,
        theme->splash.progress_indicator,
        LV_PART_INDICATOR
    );

    lv_obj_set_style_bg_opa(
        s_context.progress_bar,
        LV_OPA_COVER,
        LV_PART_INDICATOR
    );

    lv_obj_align(
        s_context.progress_bar,
        LV_ALIGN_BOTTOM_MID,
        0,
        SPLASH_PROGRESS_OFFSET_Y
    );

    lv_bar_set_range(
        s_context.progress_bar,
        0,
        (int32_t)SPLASH_PROGRESS_MAX
    );

    lv_bar_set_value(
        s_context.progress_bar,
        0,
        LV_ANIM_OFF
    );

    return screen;
}

void splash_screen_set_progress(
    uint8_t progress,
    const char *status
)
{
    if (progress > SPLASH_PROGRESS_MAX) {
        progress = SPLASH_PROGRESS_MAX;
    }

    s_context.progress = progress;

    if (s_context.progress_bar != NULL) {
        const lv_anim_enable_t animation =
            gui_config_get_animations_enabled()
                ? LV_ANIM_ON
                : LV_ANIM_OFF;

        lv_bar_set_value(
            s_context.progress_bar,
            (int32_t)progress,
            animation
        );
    }

    if ((status != NULL) &&
        (s_context.status_label != NULL)) {

        lv_label_set_text(
            s_context.status_label,
            status
        );
    }

    splash_screen_start_transition_timer();
}

void splash_screen_on_show(
    lv_obj_t *screen
)
{
    if ((screen == NULL) ||
        (screen != s_context.root)) {

        return;
    }

    s_context.active = true;

    /*
     * Progress may have reached 100 before the screen became active.
     */
    splash_screen_start_transition_timer();
}

void splash_screen_on_hide(
    lv_obj_t *screen
)
{
    if ((screen == NULL) ||
        (screen != s_context.root)) {

        return;
    }

    s_context.active = false;

    if (s_context.timer != NULL) {
        lv_timer_delete(
            s_context.timer
        );

        s_context.timer = NULL;
    }
}

void splash_screen_destroy(
    lv_obj_t *screen
)
{
    if ((screen == NULL) ||
        (screen != s_context.root)) {

        return;
    }

    splash_screen_on_hide(
        screen
    );

    /*
     * The delete callback performs the final context cleanup.
     */
    lv_obj_delete(
        screen
    );
}
