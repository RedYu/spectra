#include "screens/splash_screen.h"

#include "assets/dev_logo.h"

static lv_obj_t *s_screen;
static lv_obj_t *s_progress_bar;
static lv_obj_t *s_status_label;

lv_obj_t *splash_screen_create(void)
{
    s_screen = lv_obj_create(NULL);

    lv_obj_remove_flag(
        s_screen,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_style_bg_color(
        s_screen,
        lv_color_hex(0xFFFFFF),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        s_screen,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_t *logo_image =
        lv_image_create(s_screen);

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
        lv_label_create(s_screen);

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
        lv_label_create(s_screen);

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
        lv_bar_create(s_screen);

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

    return s_screen;
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
