#include "screens/settings_screen.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"

#include "assets/gui_images.h"
#include "board_config.h"
#include "gui_config.h"
#include "screen_manager.h"
#include "settings_model.h"
#include "settings_service.h"
#include "wifi_service.h"
#include "usb_network_service.h"

#include "widgets/toolbar.h"

#define TOOLBAR_HEIGHT                  (56U)
#define SETTINGS_TRANSITION_TIME_MS     (200U)

#define SETTINGS_TAB_BAR_WIDTH          (105)

#define CONTENT_PADDING                 (12)
#define CONTENT_ROW_PADDING             (12)

#define BRIGHTNESS_CARD_HEIGHT          (110)
#define SETTINGS_ROW_CARD_HEIGHT        (70)

#define CARD_PADDING                    (14)
#define CARD_RADIUS                     (12)

#define SETTINGS_SWITCH_WIDTH           (48)
#define SETTINGS_SWITCH_HEIGHT          (26)

#define WIFI_INFO_REFRESH_PERIOD_MS  (1000U)

static const char *TAG = "settings_screen";

static lv_timer_t *s_wifi_refresh_timer = NULL;

static lv_obj_t *s_root = NULL;
static lv_obj_t *s_tabview = NULL;

static lv_obj_t *s_brightness_slider = NULL;
static lv_obj_t *s_brightness_value_label = NULL;
static lv_obj_t *s_sd_logging_switch = NULL;
static lv_obj_t *s_animations_switch = NULL;

static lv_obj_t *s_wifi_enabled_switch = NULL;
static lv_obj_t *s_wifi_info_label = NULL;

static lv_obj_t *s_usb_rndis_enabled_switch = NULL;
static lv_obj_t *s_usb_rndis_info_label = NULL;

static bool s_updating_controls = false;

static void settings_screen_refresh_usb_rndis_info(
    const app_settings_t *settings
);

static void settings_screen_refresh_wifi_info(
    const app_settings_t *settings
);

static void settings_screen_stop_wifi_refresh(void);

static void settings_screen_network_refresh_timer_cb(
    lv_timer_t *timer
)
{
    (void)timer;

    if (s_root == NULL) {
        return;
    }

    app_settings_t settings;

    const esp_err_t result =
        settings_model_get(
            &settings
        );

    if (result != ESP_OK) {
        return;
    }

    s_updating_controls = true;

    settings_screen_refresh_wifi_info(
        &settings
    );

    settings_screen_refresh_usb_rndis_info(
        &settings
    );

    s_updating_controls = false;
}

static void settings_screen_start_wifi_refresh(void)
{
    if (s_wifi_refresh_timer != NULL) {
        return;
    }

    s_wifi_refresh_timer =
        lv_timer_create(
            settings_screen_network_refresh_timer_cb,
            WIFI_INFO_REFRESH_PERIOD_MS,
            NULL
        );

    if (s_wifi_refresh_timer == NULL) {
        ESP_LOGW(
            TAG,
            "Failed to create Wi-Fi refresh timer"
        );
    }
}

static void settings_screen_stop_wifi_refresh(void)
{
    if (s_wifi_refresh_timer == NULL) {
        return;
    }

    lv_timer_delete(
        s_wifi_refresh_timer
    );

    s_wifi_refresh_timer = NULL;
}

static void settings_screen_delete_event_cb(
    lv_event_t *event
)
{
    (void)event;

    settings_screen_stop_wifi_refresh();

    s_root = NULL;
    s_tabview = NULL;
    s_brightness_slider = NULL;
    s_brightness_value_label = NULL;
    s_sd_logging_switch = NULL;
    s_animations_switch = NULL;
    s_wifi_enabled_switch = NULL;
    s_wifi_info_label = NULL;
    s_usb_rndis_enabled_switch = NULL;
    s_usb_rndis_info_label = NULL;
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

static void settings_screen_refresh_wifi_info(
    const app_settings_t *settings
)
{
    if (settings == NULL) {
        return;
    }

    settings_screen_set_switch_state(
        s_wifi_enabled_switch,
        settings->wifi_ap.enabled
    );

    if (s_wifi_info_label == NULL) {
        return;
    }

    wifi_service_info_t info = {0};

    const esp_err_t result =
        wifi_service_get_info(
            &info
        );

    if (result != ESP_OK) {
        lv_label_set_text_fmt(
            s_wifi_info_label,
            "State: Unavailable\n"
            "Configured SSID: %s\n"
            "IP address: N/A\n"
            "DHCP: N/A\n"
            "DNS: N/A",
            settings->wifi_ap.ssid
        );

        return;
    }

    const char *state =
        info.started
            ? "Running"
            : "Stopped";

    const char *ssid =
        info.ssid[0] != '\0'
            ? info.ssid
            : settings->wifi_ap.ssid;

    const char *ip_address =
        info.started &&
        info.ip_address[0] != '\0'
            ? info.ip_address
            : "N/A";

    const char *dhcp_start =
        info.started &&
        info.dhcp_start[0] != '\0'
            ? info.dhcp_start
            : "N/A";

    const char *dhcp_end =
        info.started &&
        info.dhcp_end[0] != '\0'
            ? info.dhcp_end
            : "N/A";

    const char *dns_address =
        info.started &&
        info.dns_address[0] != '\0'
            ? info.dns_address
            : "N/A";

    if (info.started) {
        lv_label_set_text_fmt(
            s_wifi_info_label,
            "State: %s\n"
            "SSID: %s\n"
            "IP address: %s\n"
            "DHCP: %s - %s\n"
            "DNS: %s\n"
            "Clients: %u",
            state,
            ssid,
            ip_address,
            dhcp_start,
            dhcp_end,
            dns_address,
            (unsigned int)info.client_count
        );
    } else {
        lv_label_set_text_fmt(
            s_wifi_info_label,
            "State: %s\n"
            "SSID: %s\n"
            "IP address: Not active\n"
            "DHCP: Not active\n"
            "DNS: Not active\n"
            "Clients: %u",
            state,
            ssid,
            (unsigned int)info.client_count
        );
    }
}

static void settings_screen_refresh_usb_rndis_info(
    const app_settings_t *settings
)
{
    if (settings == NULL) {
        return;
    }

    settings_screen_set_switch_state(
        s_usb_rndis_enabled_switch,
        settings->usb_rndis.enabled
    );

    if (s_usb_rndis_info_label == NULL) {
        return;
    }

    usb_network_service_info_t info = {0};

    const esp_err_t result =
        usb_network_service_get_info(
            &info
        );

    if (result != ESP_OK) {
        lv_label_set_text_fmt(
            s_usb_rndis_info_label,
            "Configured: %s\n"
            "State: Unavailable\n"
            "Host: Not connected\n"
            "IP address: N/A\n"
            "DHCP: N/A\n"
            "DNS: N/A\n"
            "Changes apply after restart",
            settings->usb_rndis.enabled
                ? "Enabled"
                : "Disabled"
        );

        return;
    }

    char mac_address[18];

    (void)snprintf(
        mac_address,
        sizeof(mac_address),
        "%02X:%02X:%02X:%02X:%02X:%02X",
        info.mac[0],
        info.mac[1],
        info.mac[2],
        info.mac[3],
        info.mac[4],
        info.mac[5]
    );

    if (info.started) {
        lv_label_set_text_fmt(
            s_usb_rndis_info_label,
            "Configured: %s\n"
            "State: Running\n"
            "Host: %s\n"
            "MAC: %s\n"
            "IP address: %s\n"
            "DHCP: %s - %s\n"
            "DNS: %s",
            settings->usb_rndis.enabled
                ? "Enabled"
                : "Disabled",
            info.host_connected
                ? "Connected"
                : "Not connected",
            mac_address,
            info.ip_address[0] != '\0'
                ? info.ip_address
                : "N/A",
            info.dhcp_start[0] != '\0'
                ? info.dhcp_start
                : "N/A",
            info.dhcp_end[0] != '\0'
                ? info.dhcp_end
                : "N/A",
            info.dns_address[0] != '\0'
                ? info.dns_address
                : "N/A"
        );
    } else {
        lv_label_set_text_fmt(
            s_usb_rndis_info_label,
            "Configured: %s\n"
            "State: Stopped\n"
            "Host: Not connected\n"
            "IP address: Not active\n"
            "DHCP: Not active\n"
            "DNS: Not active\n"
            "Changes apply after restart",
            settings->usb_rndis.enabled
                ? "Enabled"
                : "Disabled"
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

    settings_screen_refresh_wifi_info(
        &settings
    );

    settings_screen_refresh_usb_rndis_info(
        &settings
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

static void wifi_enabled_switch_event_cb(
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
        settings_service_set_wifi_ap_enabled(
            enabled
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to change Wi-Fi SoftAP state: %s",
            esp_err_to_name(result)
        );

        /*
         * Restore the control from the actual settings model.
         */
        (void)settings_screen_refresh();
        return;
    }

    (void)settings_screen_refresh();
}

static void usb_rndis_enabled_switch_event_cb(
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
        settings_service_set_usb_rndis_enabled(
            enabled
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to change USB RNDIS setting: %s",
            esp_err_to_name(result)
        );

        (void)settings_screen_refresh();
        return;
    }

    /*
     * The new USB configuration is applied after device restart.
     */
    (void)settings_screen_refresh();
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

static void settings_screen_style_tab(
    lv_obj_t *tab
)
{
    if (tab == NULL) {
        return;
    }

    lv_obj_set_style_border_width(
        tab,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_radius(
        tab,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        tab,
        lv_color_hex(0xFFFFFFU),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        tab,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        tab,
        CONTENT_PADDING,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_row(
        tab,
        CONTENT_ROW_PADDING,
        LV_PART_MAIN
    );

    lv_obj_set_flex_flow(
        tab,
        LV_FLEX_FLOW_COLUMN
    );

    lv_obj_set_flex_align(
        tab,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START
    );

    lv_obj_set_scroll_dir(
        tab,
        LV_DIR_VER
    );

    lv_obj_set_scrollbar_mode(
        tab,
        LV_SCROLLBAR_MODE_AUTO
    );
}

static lv_obj_t *settings_screen_create_switch_card(
    lv_obj_t *parent,
    const char *text,
    lv_event_cb_t event_cb
)
{
    if ((parent == NULL) ||
        (text == NULL) ||
        (event_cb == NULL)) {

        return NULL;
    }

    lv_obj_t *card =
        lv_obj_create(parent);

    if (card == NULL) {
        return NULL;
    }

    lv_obj_set_size(
        card,
        LV_PCT(100),
        SETTINGS_ROW_CARD_HEIGHT
    );

    settings_screen_style_card(
        card
    );

    lv_obj_t *label =
        lv_label_create(card);

    if (label == NULL) {
        lv_obj_delete(card);
        return NULL;
    }

    lv_label_set_text(
        label,
        text
    );

    lv_obj_set_style_text_font(
        label,
        &lv_font_montserrat_14,
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        label,
        lv_color_hex(0x4B5563U),
        LV_PART_MAIN
    );

    lv_obj_align(
        label,
        LV_ALIGN_LEFT_MID,
        0,
        0
    );

    lv_obj_t *switch_obj =
        lv_switch_create(card);

    if (switch_obj == NULL) {
        lv_obj_delete(card);
        return NULL;
    }

    settings_screen_style_switch(
        switch_obj
    );

    lv_obj_align(
        switch_obj,
        LV_ALIGN_RIGHT_MID,
        0,
        0
    );

    lv_obj_add_event_cb(
        switch_obj,
        event_cb,
        LV_EVENT_VALUE_CHANGED,
        NULL
    );

    return switch_obj;
}

static esp_err_t settings_screen_create_general_tab(
    lv_obj_t *tab
)
{
    if (tab == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    settings_screen_style_tab(
        tab
    );

    lv_obj_t *brightness_card =
        lv_obj_create(tab);

    if (brightness_card == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_size(
        brightness_card,
        LV_PCT(100),
        BRIGHTNESS_CARD_HEIGHT
    );

    settings_screen_style_card(
        brightness_card
    );

    lv_obj_t *brightness_label =
        lv_label_create(
            brightness_card
        );

    if (brightness_label == NULL) {
        return ESP_ERR_NO_MEM;
    }

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
        lv_label_create(
            brightness_card
        );

    if (s_brightness_value_label == NULL) {
        return ESP_ERR_NO_MEM;
    }

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
        lv_slider_create(
            brightness_card
        );

    if (s_brightness_slider == NULL) {
        return ESP_ERR_NO_MEM;
    }

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
        (int32_t)
            SETTINGS_DISPLAY_BRIGHTNESS_MIN,
        (int32_t)
            SETTINGS_DISPLAY_BRIGHTNESS_MAX
    );

    lv_slider_set_value(
        s_brightness_slider,
        (int32_t)
            SETTINGS_DISPLAY_BRIGHTNESS_DEFAULT,
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

    s_animations_switch =
        settings_screen_create_switch_card(
            tab,
            "Enable animations",
            animations_switch_event_cb
        );

    if (s_animations_switch == NULL) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t settings_screen_create_wifi_tab(
    lv_obj_t *tab
)
{
    if (tab == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    settings_screen_style_tab(
        tab
    );

    s_wifi_enabled_switch =
        settings_screen_create_switch_card(
            tab,
            "Wi-Fi SoftAP",
            wifi_enabled_switch_event_cb
        );

    if (s_wifi_enabled_switch == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_t *info_card =
        lv_obj_create(tab);

    if (info_card == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_width(
        info_card,
        LV_PCT(100)
    );

    lv_obj_set_height(
        info_card,
        LV_SIZE_CONTENT
    );

    lv_obj_set_flex_grow(
        info_card,
        0
    );

    settings_screen_style_card(
        info_card
    );

    lv_obj_remove_flag(
        info_card,
        LV_OBJ_FLAG_SCROLLABLE
    );

    s_wifi_info_label =
        lv_label_create(
            info_card
        );

    if (s_wifi_info_label == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_width(
        s_wifi_info_label,
        LV_PCT(100)
    );

    lv_label_set_long_mode(
        s_wifi_info_label,
        LV_LABEL_LONG_WRAP
    );

    lv_label_set_text(
        s_wifi_info_label,
        "State: Loading..."
    );

    lv_obj_set_style_text_font(
        s_wifi_info_label,
        &lv_font_montserrat_14,
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        s_wifi_info_label,
        lv_color_hex(0x374151U),
        LV_PART_MAIN
    );

    lv_obj_set_style_text_line_space(
        s_wifi_info_label,
        6,
        LV_PART_MAIN
    );

    return ESP_OK;
}

static esp_err_t settings_screen_create_usb_tab(
    lv_obj_t *tab
)
{
    if (tab == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    settings_screen_style_tab(
        tab
    );

    s_usb_rndis_enabled_switch =
        settings_screen_create_switch_card(
            tab,
            "USB RNDIS",
            usb_rndis_enabled_switch_event_cb
        );

    if (s_usb_rndis_enabled_switch == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_t *info_card =
        lv_obj_create(tab);

    if (info_card == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_width(
        info_card,
        LV_PCT(100)
    );

    lv_obj_set_height(
        info_card,
        LV_SIZE_CONTENT
    );

    settings_screen_style_card(
        info_card
    );

    lv_obj_remove_flag(
        info_card,
        LV_OBJ_FLAG_SCROLLABLE
    );

    s_usb_rndis_info_label =
        lv_label_create(
            info_card
        );

    if (s_usb_rndis_info_label == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_width(
        s_usb_rndis_info_label,
        LV_PCT(100)
    );

    lv_label_set_long_mode(
        s_usb_rndis_info_label,
        LV_LABEL_LONG_WRAP
    );

    lv_label_set_text(
        s_usb_rndis_info_label,
        "State: Loading..."
    );

    lv_obj_set_style_text_font(
        s_usb_rndis_info_label,
        &lv_font_montserrat_14,
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        s_usb_rndis_info_label,
        lv_color_hex(0x374151U),
        LV_PART_MAIN
    );

    lv_obj_set_style_text_line_space(
        s_usb_rndis_info_label,
        6,
        LV_PART_MAIN
    );

    return ESP_OK;
}

static esp_err_t settings_screen_create_storage_tab(
    lv_obj_t *tab
)
{
    if (tab == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    settings_screen_style_tab(
        tab
    );

    s_sd_logging_switch =
        settings_screen_create_switch_card(
            tab,
            "Log to SD card",
            sd_logging_switch_event_cb
        );

    if (s_sd_logging_switch == NULL) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t settings_screen_create_placeholder_tab(
    lv_obj_t *tab,
    const char *text
)
{
    if ((tab == NULL) ||
        (text == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    settings_screen_style_tab(
        tab
    );

    lv_obj_t *label =
        lv_label_create(tab);

    if (label == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_label_set_text(
        label,
        text
    );

    lv_obj_set_style_text_font(
        label,
        &lv_font_montserrat_16,
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        label,
        lv_color_hex(0x6B7280U),
        LV_PART_MAIN
    );

    lv_obj_center(
        label
    );

    return ESP_OK;
}

static esp_err_t settings_screen_create_tabs(
    lv_obj_t *screen
)
{
    if (screen == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_tabview =
        lv_tabview_create(
            screen
        );

    if (s_tabview == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_size(
        s_tabview,
        LV_PCT(100),
        LCD_V_RES - TOOLBAR_HEIGHT
    );

    lv_obj_align(
        s_tabview,
        LV_ALIGN_BOTTOM_MID,
        0,
        0
    );

    lv_tabview_set_tab_bar_position(
        s_tabview,
        LV_DIR_LEFT
    );

    lv_tabview_set_tab_bar_size(
        s_tabview,
        SETTINGS_TAB_BAR_WIDTH
    );

    lv_obj_t *tab_bar =
        lv_tabview_get_tab_bar(
            s_tabview
        );

    if (tab_bar != NULL) {
        lv_obj_set_style_text_font(
            tab_bar,
            &lv_font_montserrat_14,
            LV_PART_MAIN
        );

        lv_obj_set_style_pad_all(
            tab_bar,
            4,
            LV_PART_MAIN
        );

        lv_obj_set_style_pad_row(
            tab_bar,
            4,
            LV_PART_MAIN
        );
    }

    lv_obj_t *general_tab =
        lv_tabview_add_tab(
            s_tabview,
            "General"
        );

    lv_obj_t *wifi_tab =
        lv_tabview_add_tab(
            s_tabview,
            "Wi-Fi"
        );

    lv_obj_t *usb_tab =
        lv_tabview_add_tab(
            s_tabview,
            "USB"
        );

    lv_obj_t *storage_tab =
        lv_tabview_add_tab(
            s_tabview,
            "Storage"
        );

    lv_obj_t *system_tab =
        lv_tabview_add_tab(
            s_tabview,
            "System"
        );

    if ((general_tab == NULL) ||
        (wifi_tab == NULL) ||
        (usb_tab == NULL) ||
        (storage_tab == NULL) ||
        (system_tab == NULL)) {

        return ESP_ERR_NO_MEM;
    }

    esp_err_t result =
        settings_screen_create_general_tab(
            general_tab
        );

    if (result != ESP_OK) {
        return result;
    }

    result =
        settings_screen_create_wifi_tab(
            wifi_tab
        );

    if (result != ESP_OK) {
        return result;
    }

    result =
        settings_screen_create_usb_tab(
            usb_tab
        );

    if (result != ESP_OK) {
        return result;
    }

    result =
        settings_screen_create_storage_tab(
            storage_tab
        );

    if (result != ESP_OK) {
        return result;
    }

    return settings_screen_create_placeholder_tab(
        system_tab,
        "System information"
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
    s_tabview = NULL;
    s_brightness_slider = NULL;
    s_brightness_value_label = NULL;
    s_sd_logging_switch = NULL;
    s_animations_switch = NULL;
    s_wifi_enabled_switch = NULL;
    s_wifi_info_label = NULL;
    s_usb_rndis_enabled_switch = NULL;
    s_usb_rndis_info_label = NULL;
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

    esp_err_t result =
        settings_screen_create_tabs(
            screen
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to create settings tabs: %s",
            esp_err_to_name(result)
        );

        settings_screen_destroy(
            screen
        );

        return NULL;
    }

    result =
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

    settings_screen_start_wifi_refresh();
}

void settings_screen_on_hide(
    lv_obj_t *screen
)
{
    if ((screen == NULL) ||
        (screen != s_root)) {

        return;
    }

    settings_screen_stop_wifi_refresh();
}

void settings_screen_destroy(
    lv_obj_t *screen
)
{
    if ((screen == NULL) ||
        (screen != s_root)) {

        return;
    }

    settings_screen_stop_wifi_refresh();

    s_root = NULL;
    s_tabview = NULL;
    s_brightness_slider = NULL;
    s_brightness_value_label = NULL;
    s_sd_logging_switch = NULL;
    s_animations_switch = NULL;
    s_wifi_enabled_switch = NULL;
    s_wifi_info_label = NULL;
    s_usb_rndis_enabled_switch = NULL;
    s_usb_rndis_info_label = NULL;
    s_updating_controls = false;

    lv_obj_delete(
        screen
    );
}
