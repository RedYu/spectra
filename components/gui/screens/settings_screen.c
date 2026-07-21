#include "screens/settings_screen.h"

#include "screens/main_screen.h"
#include "widgets/toolbar.h"

#define TOOLBAR_HEIGHT 56

static void return_to_main_screen(void)
{
    lv_obj_t *main_screen =
        main_screen_create();

    if (main_screen == NULL) {
        return;
    }

    lv_screen_load_anim(
        main_screen,
        LV_SCR_LOAD_ANIM_NONE,
        0,
        0,
        true
    );
}

lv_obj_t *settings_screen_create(void)
{
    lv_obj_t *screen =
        lv_obj_create(NULL);

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

    const toolbar_config_t toolbar_config = {
        .title = "Settings",

        .left_icon = LV_SYMBOL_LEFT,
        .left_action = return_to_main_screen,

        .right_icon = NULL,
        .right_action = NULL,
    };

    toolbar_create(
        screen,
        &toolbar_config
    );

    lv_obj_t *content =
        lv_obj_create(screen);

    lv_obj_set_size(
        content,
        LV_PCT(100),
        320 - TOOLBAR_HEIGHT
    );

    lv_obj_align(
        content,
        LV_ALIGN_BOTTOM_MID,
        0,
        0
    );

    lv_obj_remove_flag(
        content,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_style_border_width(
        content,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_radius(
        content,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        content,
        lv_color_hex(0xFFFFFF),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        content,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_t *label =
        lv_label_create(content);

    lv_label_set_text(
        label,
        "Device settings"
    );

    lv_obj_set_style_text_font(
        label,
        &lv_font_montserrat_20,
        LV_PART_MAIN
    );

    lv_obj_center(label);

    return screen;
}

