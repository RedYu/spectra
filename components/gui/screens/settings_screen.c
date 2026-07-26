#include "screens/settings_screen.h"

#include "screen_manager.h"
#include "widgets/toolbar.h"

#include "assets/gui_images.h"

#include "settings_model.h"
#include "settings_service.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"

#include "gui_config.h"

#define TOOLBAR_HEIGHT 56

static const char *TAG = "settings_screen";

static lv_obj_t *s_brightness_slider = NULL;
static lv_obj_t *s_brightness_value_label = NULL;

static lv_obj_t *s_sd_logging_switch;
static lv_obj_t *s_animations_switch;

static void settings_screen_back_action(void)
{
    esp_err_t result =
        settings_service_save();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to save settings: %s",
            esp_err_to_name(result)
        );

        /*
         * Do not leave the screen if saving failed.
         */
        return;
    }

    result = screen_manager_back(
        gui_config_are_animations_enabled() ? LV_SCR_LOAD_ANIM_MOVE_RIGHT : LV_SCR_LOAD_ANIM_NONE,
        200
    );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to return to previous screen: %s",
            esp_err_to_name(result)
        );
    }
}

static void brightness_slider_event_cb(
    lv_event_t *event
)
{
    lv_obj_t *slider =
        lv_event_get_target_obj(event);

    const uint8_t brightness =
        (uint8_t)lv_slider_get_value(slider);

    esp_err_t result =
        settings_service_set_brightness(
            brightness
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to apply brightness: %s",
            esp_err_to_name(result)
        );

        return;
    }

    const app_settings_t *settings =
        settings_model_get();

    if (settings != NULL &&
        s_brightness_value_label != NULL) {

        lv_label_set_text_fmt(
            s_brightness_value_label,
            "%u%%",
            settings->display.brightness
        );
    }
}

static void sd_logging_switch_event_cb(
    lv_event_t *event
)
{
    lv_obj_t *switch_obj =
        lv_event_get_target_obj(event);

    const bool enabled =
        lv_obj_has_state(
            switch_obj,
            LV_STATE_CHECKED
        );

    esp_err_t result =
        settings_service_set_sd_logging_enabled(
            enabled
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to apply sd logging: %s",
            esp_err_to_name(result)
        );

        return;
    }
}

static void animations_switch_event_cb(
    lv_event_t *event
)
{
    lv_obj_t *switch_obj =
        lv_event_get_target_obj(event);

    const bool enabled =
        lv_obj_has_state(
            switch_obj,
            LV_STATE_CHECKED
        );

    esp_err_t result =
        settings_service_set_animations_enabled(
            enabled
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to apply animation enabled: %s",
            esp_err_to_name(result)
        );

        return;
    }
}

lv_obj_t *settings_screen_create(void)
{
    lv_obj_t *screen =
        lv_obj_create(NULL);

    const app_settings_t *settings =
        settings_model_get();

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

    lv_obj_add_flag(
        content,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_scroll_dir(
        content,
        LV_DIR_VER
    );

    lv_obj_set_scrollbar_mode(
        content,
        LV_SCROLLBAR_MODE_AUTO
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

    lv_obj_set_width(
        brightness_card,
        LV_PCT(100)
    );

    lv_obj_set_height(
        brightness_card,
        110
    );

    lv_obj_set_flex_grow(
        brightness_card,
        0
    );

    /*
     * General settings card.
     */
    lv_obj_t *general_card =
        lv_obj_create(content);

    lv_obj_set_width(
        general_card,
        LV_PCT(100)
    );

    lv_obj_set_height(
        general_card,
        130
    );

    lv_obj_remove_flag(
        general_card,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_style_border_width(
        general_card,
        1,
        LV_PART_MAIN
    );

    lv_obj_set_style_border_color(
        general_card,
        lv_color_hex(0xE1E5EA),
        LV_PART_MAIN
    );

    lv_obj_set_style_radius(
        general_card,
        12,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        general_card,
        lv_color_hex(0xF7F8FA),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        general_card,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        general_card,
        14,
        LV_PART_MAIN
    );

    /*
     * General card title.
     */
    lv_obj_t *general_title =
        lv_label_create(general_card);

    lv_label_set_text(
        general_title,
        "General"
    );

    lv_obj_set_style_text_font(
        general_title,
        &lv_font_montserrat_16,
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        general_title,
        lv_color_hex(0x20252A),
        LV_PART_MAIN
    );

    lv_obj_align(
        general_title,
        LV_ALIGN_TOP_LEFT,
        0,
        0
    );

    /*
     * SD logging row.
     */
    lv_obj_t *sd_logging_label =
        lv_label_create(general_card);

    lv_label_set_text(
        sd_logging_label,
        "Log to SD card"
    );

    lv_obj_set_style_text_font(
        sd_logging_label,
        &lv_font_montserrat_14,
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        sd_logging_label,
        lv_color_hex(0x4B5563),
        LV_PART_MAIN
    );

    lv_obj_align(
        sd_logging_label,
        LV_ALIGN_TOP_LEFT,
        0,
        36
    );

    s_sd_logging_switch =
        lv_switch_create(general_card);

    lv_obj_set_size(
        s_sd_logging_switch,
        48,
        26
    );

    lv_obj_align(
        s_sd_logging_switch,
        LV_ALIGN_TOP_RIGHT,
        0,
        30
    );

    lv_obj_set_style_bg_color(
        s_sd_logging_switch,
        lv_color_hex(0xD9DEE5),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        s_sd_logging_switch,
        lv_color_hex(0x4B77D1),
        LV_PART_INDICATOR | LV_STATE_CHECKED
    );

    lv_obj_set_style_bg_color(
        s_sd_logging_switch,
        lv_color_hex(0xFFFFFF),
        LV_PART_KNOB
    );

    if (settings->logging.sd_enabled) {
        lv_obj_add_state(
            s_sd_logging_switch,
            LV_STATE_CHECKED
        );
    }

    lv_obj_add_event_cb(
        s_sd_logging_switch,
        sd_logging_switch_event_cb,
        LV_EVENT_VALUE_CHANGED,
        NULL
    );

    /*
     * Separator.
     */
    lv_obj_t *separator =
        lv_obj_create(general_card);

    lv_obj_set_size(
        separator,
        LV_PCT(100),
        1
    );

    lv_obj_align(
        separator,
        LV_ALIGN_TOP_MID,
        0,
        70
    );

    lv_obj_set_style_border_width(
        separator,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_radius(
        separator,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        separator,
        lv_color_hex(0xE1E5EA),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        separator,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    /*
     * Animations row.
     */
    lv_obj_t *animations_label =
        lv_label_create(general_card);

    lv_label_set_text(
        animations_label,
        "Enable animations"
    );

    lv_obj_set_style_text_font(
        animations_label,
        &lv_font_montserrat_14,
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        animations_label,
        lv_color_hex(0x4B5563),
        LV_PART_MAIN
    );

    lv_obj_align(
        animations_label,
        LV_ALIGN_BOTTOM_LEFT,
        0,
        -4
    );

    s_animations_switch =
        lv_switch_create(general_card);

    lv_obj_set_size(
        s_animations_switch,
        48,
        26
    );

    lv_obj_align(
        s_animations_switch,
        LV_ALIGN_BOTTOM_RIGHT,
        0,
        0
    );

    lv_obj_set_style_bg_color(
        s_animations_switch,
        lv_color_hex(0xD9DEE5),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        s_animations_switch,
        lv_color_hex(0x4B77D1),
        LV_PART_INDICATOR | LV_STATE_CHECKED
    );

    lv_obj_set_style_bg_color(
        s_animations_switch,
        lv_color_hex(0xFFFFFF),
        LV_PART_KNOB
    );

    if (settings->ui.animations_enabled) {
        lv_obj_add_state(
            s_animations_switch,
            LV_STATE_CHECKED
        );
    }

    lv_obj_add_event_cb(
        s_animations_switch,
        animations_switch_event_cb,
        LV_EVENT_VALUE_CHANGED,
        NULL
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
    s_brightness_slider =
        lv_slider_create(brightness_card);

    lv_obj_set_size(
        s_brightness_slider,
        LV_PCT(100),
        12
    );

    lv_obj_align(
        s_brightness_slider,
        LV_ALIGN_BOTTOM_MID,
        0,
        -6
    );

    lv_slider_set_range(
        s_brightness_slider,
        SETTINGS_DISPLAY_BRIGHTNESS_MIN,
        SETTINGS_DISPLAY_BRIGHTNESS_MAX
    );

    lv_slider_set_value(
        s_brightness_slider,
        settings->display.brightness,
        LV_ANIM_OFF
    );

    lv_obj_set_style_bg_color(
        s_brightness_slider,
        lv_color_hex(0xD9DEE5),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        s_brightness_slider,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        s_brightness_slider,
        lv_color_hex(0x4B77D1),
        LV_PART_INDICATOR
    );

    lv_obj_set_style_bg_opa(
        s_brightness_slider,
        LV_OPA_COVER,
        LV_PART_INDICATOR
    );

    lv_obj_set_style_bg_color(
        s_brightness_slider,
        lv_color_hex(0x4B77D1),
        LV_PART_KNOB
    );

    lv_obj_set_style_bg_opa(
        s_brightness_slider,
        LV_OPA_COVER,
        LV_PART_KNOB
    );

    lv_obj_set_style_pad_all(
        s_brightness_slider,
        5,
        LV_PART_KNOB
    );

    lv_obj_add_event_cb(
        s_brightness_slider,
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

    const app_settings_t *settings =
        settings_model_get();

    if (settings == NULL) {
        return;
    }

    if (s_brightness_slider != NULL) {
        lv_slider_set_value(
            s_brightness_slider,
            settings->display.brightness,
            LV_ANIM_OFF
        );
    }

    if (s_brightness_value_label != NULL) {
        lv_label_set_text_fmt(
            s_brightness_value_label,
            "%u%%",
            settings->display.brightness
        );
    }
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

