#include "screens/splash_screen.h"

#include <string.h>

#include "assets/gui_images.h"

#include "screen_manager.h"

static lv_obj_t *s_progress_bar;
static lv_obj_t *s_status_label;

typedef struct
{
    lv_obj_t *root;
    lv_timer_t *timer;

} splash_screen_context_t;

static splash_screen_context_t s_context = {0};

static void splash_screen_timer_cb(
    lv_timer_t *timer
)
{
    (void)timer;

    s_context.timer = NULL;

    screen_manager_clear_history();

    screen_manager_show(
        SCREEN_ID_MAIN,
        LV_SCR_LOAD_ANIM_FADE_IN,
        300
    );
}

lv_obj_t *splash_screen_create(void)
{
    lv_obj_t *screen =
            lv_obj_create(NULL);

    s_context.root =
        screen;

    lv_obj_remove_flag(
        screen,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_remove_flag(
        screen,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_style_bg_color(
        screen,
        lv_color_hex(0xFFFFFF),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        screen,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_t *logo_image =
        lv_image_create(screen);

    lv_image_set_src(
        logo_image,
        &dev_logo
    );

    lv_obj_align(
        logo_image,
        LV_ALIGN_CENTER,
        0,
        -45
    );

    lv_obj_t *title_label =
        lv_label_create(screen);

    lv_label_set_text(
        title_label,
        "SPECTRA"
    );

    lv_obj_set_style_text_font(
        title_label,
        &lv_font_montserrat_20,
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        title_label,
        lv_color_hex(0x808080),
        LV_PART_MAIN
    );

    lv_obj_align(
        title_label,
        LV_ALIGN_CENTER,
        0,
        55
    );

    s_status_label =
        lv_label_create(screen);

    lv_label_set_text(
        s_status_label,
        "Starting system..."
    );

    lv_obj_set_style_text_font(
        s_status_label,
        &lv_font_montserrat_16,
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        s_status_label,
        lv_color_hex(0xA0A8B0),
        LV_PART_MAIN
    );

    lv_obj_align(
        s_status_label,
        LV_ALIGN_BOTTOM_MID,
        0,
        -48
    );

    s_progress_bar =
        lv_bar_create(screen);

    lv_obj_set_size(
        s_progress_bar,
        280,
        8
    );

    lv_obj_align(
        s_progress_bar,
        LV_ALIGN_BOTTOM_MID,
        0,
        -25
    );

    lv_bar_set_range(
        s_progress_bar,
        0,
        100
    );

    lv_bar_set_value(
        s_progress_bar,
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
    if (s_progress_bar == NULL) {
        return;
    }

    if (progress > 100) {
        progress = 100;
    }

    lv_bar_set_value(
        s_progress_bar,
        progress,
        LV_ANIM_ON
    );

    if (status != NULL &&
        s_status_label != NULL) {

        lv_label_set_text(
            s_status_label,
            status
        );
    }
}

void splash_screen_on_show(
    lv_obj_t *screen
)
{
    (void)screen;

    if (s_context.timer == NULL) {
        s_context.timer =
            lv_timer_create(
                splash_screen_timer_cb,
                1500,
                NULL
            );

        lv_timer_set_repeat_count(
            s_context.timer,
            1
        );
    }
}

void splash_screen_on_hide(
    lv_obj_t *screen
)
{
    (void)screen;

    if (s_context.timer != NULL) {
        lv_timer_delete(
            s_context.timer
        );

        s_context.timer =
            NULL;
    }
}

void splash_screen_destroy(
    lv_obj_t *screen
)
{
    splash_screen_on_hide(
        screen
    );

    lv_obj_delete(
        screen
    );

    memset(
        &s_context,
        0,
        sizeof(s_context)
    );
}
