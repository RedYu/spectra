#include "screens/settings_screen.h"

#include "screen_manager.h"
#include "widgets/toolbar.h"

#include "assets/gui_images.h"

#define TOOLBAR_HEIGHT 56

static void settings_screen_back_action(void)
{
    screen_manager_back(
        LV_SCR_LOAD_ANIM_MOVE_RIGHT,
        200
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

        .left_icon = &icons8_back_32,
        .left_action = settings_screen_back_action,

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


    lv_obj_t *img =
        lv_image_create(screen);

    lv_image_set_src(
        img,
        &icons8_micro_sd_32
    );

    lv_obj_center(img);

    lv_obj_set_style_image_recolor(
            img,
            lv_color_hex(0x4B77D1),
            LV_PART_MAIN
        );

        lv_obj_set_style_image_recolor_opa(
            img,
            LV_OPA_COVER,
            LV_PART_MAIN
        );

    return screen;
}

void settings_screen_on_show(
    lv_obj_t *screen
)
{
    (void)screen;

    /*
     * Refresh settings values here.
     */
}

void settings_screen_on_hide(
    lv_obj_t *screen
)
{
    (void)screen;
}

void settings_screen_destroy(
    lv_obj_t *screen
)
{
    lv_obj_delete(
        screen
    );
}

