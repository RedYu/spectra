#include "screens/settings_screen.h"

#include "screen_manager.h"
#include "widgets/toolbar.h"

#include "assets/gui_images.h"

#include "settings_model.h"
#include "settings_service.h"

#define TOOLBAR_HEIGHT 56

static lv_obj_t *s_brightness_value_label = NULL;

static void settings_screen_back_action(void)
{
    screen_manager_back(
        LV_SCR_LOAD_ANIM_MOVE_RIGHT,
        200
    );
}

static void brightness_slider_event_cb(lv_event_t *event)
{
    lv_obj_t *slider =
        lv_event_get_target_obj(event);

    const uint8_t brightness =
        (uint8_t)lv_slider_get_value(slider);

    settings_service_set_brightness(
        brightness
    );

    if (s_brightness_value_label != NULL) {
        const app_settings_t *settings =
            settings_model_get();

        lv_label_set_text_fmt(
            s_brightness_value_label,
            "%u%%",
            settings->display.brightness
        );
    }
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

    lv_obj_set_style_pad_all(
        content,
        20,
        LV_PART_MAIN
    );

    lv_obj_set_flex_flow(
        content,
        LV_FLEX_FLOW_COLUMN
    );

    lv_obj_set_flex_align(
        content,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START
    );

    lv_obj_set_style_pad_row(
        content,
        20,
        LV_PART_MAIN
    );

    /*
     * Screen title.
     */
    lv_obj_t *title =
        lv_label_create(content);

    lv_label_set_text(
        title,
        "Device settings"
    );

    lv_obj_set_style_text_font(
        title,
        &lv_font_montserrat_20,
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        title,
        lv_color_hex(0x20252A),
        LV_PART_MAIN
    );

    /*
     * Brightness settings card.
     */
    lv_obj_t *brightness_card =
        lv_obj_create(content);

    lv_obj_set_size(
        brightness_card,
        LV_PCT(100),
        110
    );

    lv_obj_remove_flag(
        brightness_card,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_style_border_width(
        brightness_card,
        1,
        LV_PART_MAIN
    );

    lv_obj_set_style_border_color(
        brightness_card,
        lv_color_hex(0xE1E5EA),
        LV_PART_MAIN
    );

    lv_obj_set_style_radius(
        brightness_card,
        12,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        brightness_card,
        lv_color_hex(0xF7F8FA),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        brightness_card,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        brightness_card,
        14,
        LV_PART_MAIN
    );

    /*
     * Brightness label.
     */
    lv_obj_t *brightness_label =
        lv_label_create(brightness_card);

    lv_label_set_text(
        brightness_label,
        "Display brightness"
    );

    lv_obj_set_style_text_font(
        brightness_label,
        &lv_font_montserrat_16,
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        brightness_label,
        lv_color_hex(0x20252A),
        LV_PART_MAIN
    );

    lv_obj_align(
        brightness_label,
        LV_ALIGN_TOP_LEFT,
        0,
        0
    );

    /*
     * Current brightness value.
     */
    s_brightness_value_label =
        lv_label_create(brightness_card);

    const app_settings_t *settings =
        settings_model_get();

    lv_label_set_text_fmt(
        s_brightness_value_label,
        "%u%%",
        settings->display.brightness
    );

    lv_obj_set_style_text_font(
        s_brightness_value_label,
        &lv_font_montserrat_16,
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        s_brightness_value_label,
        lv_color_hex(0x4B77D1),
        LV_PART_MAIN
    );

    lv_obj_align(
        s_brightness_value_label,
        LV_ALIGN_TOP_RIGHT,
        0,
        0
    );

    /*
     * Brightness slider.
     */
    lv_obj_t *brightness_slider =
        lv_slider_create(brightness_card);

    lv_obj_set_size(
        brightness_slider,
        LV_PCT(100),
        12
    );

    lv_obj_align(
        brightness_slider,
        LV_ALIGN_BOTTOM_MID,
        0,
        -6
    );

    lv_slider_set_range(
        brightness_slider,
        SETTINGS_DISPLAY_BRIGHTNESS_MIN,
        SETTINGS_DISPLAY_BRIGHTNESS_MAX
    );

    lv_slider_set_value(
        brightness_slider,
        settings->display.brightness,
        LV_ANIM_OFF
    );

    lv_obj_set_style_bg_color(
        brightness_slider,
        lv_color_hex(0xD9DEE5),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        brightness_slider,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        brightness_slider,
        lv_color_hex(0x4B77D1),
        LV_PART_INDICATOR
    );

    lv_obj_set_style_bg_opa(
        brightness_slider,
        LV_OPA_COVER,
        LV_PART_INDICATOR
    );

    lv_obj_set_style_bg_color(
        brightness_slider,
        lv_color_hex(0x4B77D1),
        LV_PART_KNOB
    );

    lv_obj_set_style_bg_opa(
        brightness_slider,
        LV_OPA_COVER,
        LV_PART_KNOB
    );

    lv_obj_set_style_pad_all(
        brightness_slider,
        5,
        LV_PART_KNOB
    );

    lv_obj_add_event_cb(
        brightness_slider,
        brightness_slider_event_cb,
        LV_EVENT_VALUE_CHANGED,
        NULL
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

