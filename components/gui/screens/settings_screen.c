#include "screens/settings_screen.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"

#include "assets/gui_images.h"
#include "board_config.h"
#include "gui_config.h"
#include "screen_manager.h"
#include "settings_model.h"
#include "settings_service.h"
#include "widgets/toolbar.h"

#define TOOLBAR_HEIGHT                  (56U)
#define SETTINGS_TRANSITION_TIME_MS     (200U)

#define CONTENT_PADDING                 (20)
#define CONTENT_ROW_PADDING             (20)

#define BRIGHTNESS_CARD_HEIGHT          (110)
#define GENERAL_CARD_HEIGHT             (130)

#define CARD_PADDING                    (14)
#define CARD_RADIUS                     (12)

#define SETTINGS_SWITCH_WIDTH           (48)
#define SETTINGS_SWITCH_HEIGHT          (26)

static const char *TAG = "settings_screen";

static lv_obj_t *s_root = NULL;
static lv_obj_t *s_brightness_slider = NULL;
static lv_obj_t *s_brightness_value_label = NULL;
static lv_obj_t *s_sd_logging_switch = NULL;
static lv_obj_t *s_animations_switch = NULL;

static bool s_updating_controls = false;

static void settings_screen_delete_event_cb(
    lv_event_t *event
)
{
    (void)event;

    s_root = NULL;
    s_brightness_slider = NULL;
    s_brightness_value_label = NULL;
    s_sd_logging_switch = NULL;
    s_animations_switch = NULL;
    s_updating_controls = false;
}

static void settings_screen_set_switch_state(
    lv_obj_t *switch_obj,
    bool enabled
)
{
    if (switch_obj == NULL) {
        return;
    }

    if (enabled) {
        lv_obj_add_state(
            switch_obj,
            LV_STATE_CHECKED
        );
    } else {
        lv_obj_remove_state(
            switch_obj,
            LV_STATE_CHECKED
        );
    }
}

static esp_err_t settings_screen_refresh(void)
{
    app_settings_t settings;

    const esp_err_t result =
        settings_model_get(
            &settings
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to get settings: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    s_updating_controls = true;

    if (s_brightness_slider != NULL) {
        lv_slider_set_value(
            s_brightness_slider,
            (int32_t)settings.display.brightness,
            LV_ANIM_OFF
        );
    }

    if (s_brightness_value_label != NULL) {
        lv_label_set_text_fmt(
            s_brightness_value_label,
            "%u%%",
            (unsigned int)settings.display.brightness
        );
    }

    settings_screen_set_switch_state(
        s_sd_logging_switch,
        settings.logging.sd_enabled
    );

    settings_screen_set_switch_state(
        s_animations_switch,
        settings.ui.animations_enabled
    );

    s_updating_controls = false;

    return ESP_OK;
}

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

        return;
    }

    const bool animations_enabled =
        gui_config_get_animations_enabled();

    const lv_screen_load_anim_t animation =
        animations_enabled
            ? LV_SCR_LOAD_ANIM_MOVE_RIGHT
            : LV_SCR_LOAD_ANIM_NONE;

    const uint32_t animation_time_ms =
        animations_enabled
            ? SETTINGS_TRANSITION_TIME_MS
            : 0U;

    result = screen_manager_back(
        animation,
        animation_time_ms
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
    if (s_updating_controls) {
        return;
    }

    lv_obj_t *slider =
        lv_event_get_target_obj(event);

    if (slider == NULL) {
        return;
    }

    const int32_t slider_value =
        lv_slider_get_value(slider);

    const esp_err_t result =
        settings_service_set_brightness(
            (uint8_t)slider_value
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to apply brightness: %s",
            esp_err_to_name(result)
        );

        (void)settings_screen_refresh();

        return;
    }

    /*
     * Refresh the displayed value because the model performs the
     * final validation and range limiting.
     */
    (void)settings_screen_refresh();
}

static void sd_logging_switch_event_cb(
    lv_event_t *event
)
{
    if (s_updating_controls) {
        return;
    }

    lv_obj_t *switch_obj =
        lv_event_get_target_obj(event);

    if (switch_obj == NULL) {
        return;
    }

    const bool enabled =
        lv_obj_has_state(
            switch_obj,
            LV_STATE_CHECKED
        );

    const esp_err_t result =
        settings_service_set_sd_logging_enabled(
            enabled
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to apply SD logging: %s",
            esp_err_to_name(result)
        );

        (void)settings_screen_refresh();
    }
}

static void animations_switch_event_cb(
    lv_event_t *event
)
{
    if (s_updating_controls) {
        return;
    }

    lv_obj_t *switch_obj =
        lv_event_get_target_obj(event);

    if (switch_obj == NULL) {
        return;
    }

    const bool enabled =
        lv_obj_has_state(
            switch_obj,
            LV_STATE_CHECKED
        );

    const esp_err_t result =
        settings_service_set_animations_enabled(
            enabled
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to apply animations setting: %s",
            esp_err_to_name(result)
        );

        (void)settings_screen_refresh();
    }
}

static void settings_screen_style_card(
    lv_obj_t *card
)
{
    lv_obj_remove_flag(
        card,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_style_border_width(
        card,
        1,
        LV_PART_MAIN
    );

    lv_obj_set_style_border_color(
        card,
        lv_color_hex(0xE1E5EAU),
        LV_PART_MAIN
    );

    lv_obj_set_style_radius(
        card,
        CARD_RADIUS,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        card,
        lv_color_hex(0xF7F8FAU),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        card,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        card,
        CARD_PADDING,
        LV_PART_MAIN
    );
}

static void settings_screen_style_switch(
    lv_obj_t *switch_obj
)
{
    lv_obj_set_size(
        switch_obj,
        SETTINGS_SWITCH_WIDTH,
        SETTINGS_SWITCH_HEIGHT
    );

    lv_obj_set_style_bg_color(
        switch_obj,
        lv_color_hex(0xD9DEE5U),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        switch_obj,
        lv_color_hex(0x4B77D1U),
        LV_PART_INDICATOR | LV_STATE_CHECKED
    );

    lv_obj_set_style_bg_color(
        switch_obj,
        lv_color_hex(0xFFFFFFU),
        LV_PART_KNOB
    );
}

lv_obj_t *settings_screen_create(void)
{
    lv_obj_t *screen =
        lv_obj_create(NULL);

    if (screen == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create settings screen"
        );

        return NULL;
    }

    s_root = screen;
    s_brightness_slider = NULL;
    s_brightness_value_label = NULL;
    s_sd_logging_switch = NULL;
    s_animations_switch = NULL;
    s_updating_controls = false;

    lv_obj_add_event_cb(
        screen,
        settings_screen_delete_event_cb,
        LV_EVENT_DELETE,
        NULL
    );

    lv_obj_remove_flag(
        screen,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_style_bg_color(
        screen,
        lv_color_hex(0xFFFFFFU),
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

    const toolbar_t toolbar =
        toolbar_create(
            screen,
            &toolbar_config
        );

    if (toolbar.root == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create settings-screen toolbar"
        );

        settings_screen_destroy(
            screen
        );

        return NULL;
    }

    lv_obj_t *content =
        lv_obj_create(screen);

    if (content == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create settings-screen content"
        );

        settings_screen_destroy(
            screen
        );

        return NULL;
    }

    lv_obj_set_size(
        content,
        LV_PCT(100),
        LCD_V_RES - TOOLBAR_HEIGHT
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
        lv_color_hex(0xFFFFFFU),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        content,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        content,
        CONTENT_PADDING,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_row(
        content,
        CONTENT_ROW_PADDING,
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
        lv_color_hex(0x20252AU),
        LV_PART_MAIN
    );

    /*
     * Brightness card.
     */
    lv_obj_t *brightness_card =
        lv_obj_create(content);

    lv_obj_set_size(
        brightness_card,
        LV_PCT(100),
        BRIGHTNESS_CARD_HEIGHT
    );

    settings_screen_style_card(
        brightness_card
    );

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
        lv_color_hex(0x20252AU),
        LV_PART_MAIN
    );

    lv_obj_align(
        brightness_label,
        LV_ALIGN_TOP_LEFT,
        0,
        0
    );

    s_brightness_value_label =
        lv_label_create(brightness_card);

    lv_label_set_text(
        s_brightness_value_label,
        "0%"
    );

    lv_obj_set_style_text_font(
        s_brightness_value_label,
        &lv_font_montserrat_16,
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        s_brightness_value_label,
        lv_color_hex(0x4B77D1U),
        LV_PART_MAIN
    );

    lv_obj_align(
        s_brightness_value_label,
        LV_ALIGN_TOP_RIGHT,
        0,
        0
    );

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
        (int32_t)SETTINGS_DISPLAY_BRIGHTNESS_MIN,
        (int32_t)SETTINGS_DISPLAY_BRIGHTNESS_MAX
    );

    lv_slider_set_value(
        s_brightness_slider,
        (int32_t)SETTINGS_DISPLAY_BRIGHTNESS_DEFAULT,
        LV_ANIM_OFF
    );

    lv_obj_set_style_bg_color(
        s_brightness_slider,
        lv_color_hex(0xD9DEE5U),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        s_brightness_slider,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        s_brightness_slider,
        lv_color_hex(0x4B77D1U),
        LV_PART_INDICATOR
    );

    lv_obj_set_style_bg_opa(
        s_brightness_slider,
        LV_OPA_COVER,
        LV_PART_INDICATOR
    );

    lv_obj_set_style_bg_color(
        s_brightness_slider,
        lv_color_hex(0x4B77D1U),
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

    /*
     * General settings card.
     */
    lv_obj_t *general_card =
        lv_obj_create(content);

    lv_obj_set_size(
        general_card,
        LV_PCT(100),
        GENERAL_CARD_HEIGHT
    );

    settings_screen_style_card(
        general_card
    );

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
        lv_color_hex(0x20252AU),
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
        lv_color_hex(0x4B5563U),
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

    settings_screen_style_switch(
        s_sd_logging_switch
    );

    lv_obj_align(
        s_sd_logging_switch,
        LV_ALIGN_TOP_RIGHT,
        0,
        30
    );

    lv_obj_add_event_cb(
        s_sd_logging_switch,
        sd_logging_switch_event_cb,
        LV_EVENT_VALUE_CHANGED,
        NULL
    );

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

    lv_obj_remove_flag(
        separator,
        LV_OBJ_FLAG_SCROLLABLE
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
        lv_color_hex(0xE1E5EAU),
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
        lv_color_hex(0x4B5563U),
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

    settings_screen_style_switch(
        s_animations_switch
    );

    lv_obj_align(
        s_animations_switch,
        LV_ALIGN_BOTTOM_RIGHT,
        0,
        0
    );

    lv_obj_add_event_cb(
        s_animations_switch,
        animations_switch_event_cb,
        LV_EVENT_VALUE_CHANGED,
        NULL
    );

    const esp_err_t result =
        settings_screen_refresh();

    if (result != ESP_OK) {
        settings_screen_destroy(
            screen
        );

        return NULL;
    }

    return screen;
}

void settings_screen_on_show(
    lv_obj_t *screen
)
{
    if ((screen == NULL) ||
        (screen != s_root)) {

        return;
    }

    const esp_err_t result =
        settings_screen_refresh();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to refresh settings screen: %s",
            esp_err_to_name(result)
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
    if ((screen == NULL) ||
        (screen != s_root)) {

        return;
    }

    s_root = NULL;
    s_brightness_slider = NULL;
    s_brightness_value_label = NULL;
    s_sd_logging_switch = NULL;
    s_animations_switch = NULL;
    s_updating_controls = false;

    lv_obj_delete(
        screen
    );
}
