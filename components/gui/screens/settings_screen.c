/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "screens/settings_screen.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <time.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "assets/gui_images.h"
#include "board_config.h"
#include "gui_config.h"
#include "gui_feedback.h"
#include "gui_styles.h"
#include "gui_theme.h"
#include "buzzer_service.h"
#include "screen_manager.h"
#include "system_model.h"
#include "settings_model.h"
#include "settings_service.h"
#include "wifi_service.h"
#include "usb_network_service.h"
#include "storage_service.h"
#include "storage_sd_service.h"
#include "system_service.h"
#include "can_service.h"
#include "can_fd_service.h"
#include "time_service.h"

#include "widgets/toolbar.h"
#include "widgets/wifi_credentials_dialog.h"

#define SETTINGS_TOOLBAR_HEIGHT \
    GUI_THEME_TOOLBAR_HEIGHT
#define SETTINGS_TRANSITION_TIME_MS     (200U)

#define SETTINGS_TAB_BAR_WIDTH          (105)

#define CONTENT_PADDING \
    GUI_THEME_SPACE_MD

#define CONTENT_ROW_PADDING \
    GUI_THEME_SPACE_MD

#define BRIGHTNESS_CARD_HEIGHT          (82)
#define SETTINGS_ROW_CARD_HEIGHT        (70)

#define CARD_PADDING \
    GUI_THEME_SPACE_MD

#define SETTINGS_SWITCH_WIDTH           (48)
#define SETTINGS_SWITCH_HEIGHT          (26)

#define WIFI_SCAN_RSSI_COLUMN_WIDTH     (72)
#define WIFI_SCAN_CHANNEL_COLUMN_WIDTH  (38)
#define WIFI_SCAN_NETWORK_MIN_WIDTH     (160)

#define SETTINGS_STATUS_REFRESH_PERIOD_MS  (1000U)

#define SETTINGS_SCREEN_RESTART_DELAY_MS  (500U)

#define SETTINGS_CAN_NOMINAL_BITRATE_OPTIONS \
    "10 kbit/s\n"                            \
    "20 kbit/s\n"                            \
    "33.333 kbit/s\n"                        \
    "50 kbit/s\n"                            \
    "83.333 kbit/s\n"                        \
    "100 kbit/s\n"                           \
    "125 kbit/s\n"                           \
    "250 kbit/s\n"                           \
    "500 kbit/s\n"                           \
    "800 kbit/s\n"                           \
    "1 Mbit/s"

#if SETTINGS_CAN_FD_DATA_BITRATE_MAX >= 8000000U

#define SETTINGS_CAN_FD_DATA_BITRATE_OPTIONS \
    "1 Mbit/s\n"                             \
    "2 Mbit/s\n"                             \
    "4 Mbit/s\n"                             \
    "5 Mbit/s\n"                             \
    "8 Mbit/s"

#else

#define SETTINGS_CAN_FD_DATA_BITRATE_OPTIONS \
    "1 Mbit/s\n"                             \
    "2 Mbit/s\n"                             \
    "4 Mbit/s\n"                             \
    "5 Mbit/s"

#endif

static const char *TAG = "settings_screen";

/*
 * TODO: Obtain one wifi_service_info_t snapshot per refresh cycle and
 * pass it to both the SoftAP and Station update functions. Currently,
 * wifi_service_get_info() is called separately for each section. This
 * is acceptable at the current 1 Hz refresh rate but performs
 * redundant driver queries and may produce slightly different AP and
 * STA snapshots.
 */

static lv_timer_t *s_status_refresh_timer = NULL;

static lv_obj_t *s_root = NULL;
static lv_obj_t *s_tabview = NULL;

static lv_obj_t *s_brightness_slider = NULL;
static lv_obj_t *s_brightness_value_label = NULL;

static lv_obj_t *s_sound_enabled_switch = NULL;
static lv_obj_t *s_buzzer_volume_slider = NULL;
static lv_obj_t *s_buzzer_volume_value_label = NULL;

static lv_obj_t *s_sd_logging_switch = NULL;
static lv_obj_t *s_storage_info_label = NULL;

static lv_obj_t *s_animations_switch = NULL;
static lv_obj_t *s_theme_dropdown = NULL;
static lv_obj_t *s_restart_required_label = NULL;

static lv_obj_t *s_wifi_enabled_switch = NULL;
static lv_obj_t *s_wifi_info_label = NULL;

static lv_obj_t *s_wifi_scan_button = NULL;
static lv_obj_t *s_wifi_scan_status_label = NULL;
static lv_obj_t *s_wifi_scan_table = NULL;

static lv_obj_t *s_wifi_sta_enabled_switch = NULL;
static lv_obj_t *s_wifi_sta_info_label = NULL;

static wifi_service_scan_result_t
    s_selected_wifi_network;

static bool s_wifi_network_selected = false;

static bool s_wifi_scan_results_displayed = false;

static lv_obj_t *s_usb_rndis_enabled_switch = NULL;
static lv_obj_t *s_usb_rndis_info_label = NULL;

static lv_obj_t *s_system_info_label = NULL;

static bool s_updating_controls = false;

static lv_obj_t *s_can_enabled_switch = NULL;
static lv_obj_t *s_can_bitrate_dropdown = NULL;
static lv_obj_t *s_can_listen_only_switch = NULL;
static lv_obj_t *s_can_info_label = NULL;

static const uint32_t s_can_bitrates[] = {
    10000U,
    20000U,
    33333U,
    50000U,
    83333U,
    100000U,
    125000U,
    250000U,
    500000U,
    800000U,
    1000000U,
};

static lv_obj_t *s_can_secondary_enabled_switch = NULL;
static lv_obj_t *s_can_secondary_nominal_dropdown = NULL;
static lv_obj_t *s_can_secondary_data_dropdown = NULL;
static lv_obj_t *s_can_secondary_fd_switch = NULL;
static lv_obj_t *s_can_secondary_brs_switch = NULL;
static lv_obj_t *s_can_secondary_listen_only_switch = NULL;
static lv_obj_t *s_can_secondary_info_label = NULL;

static const uint32_t s_can_fd_data_bitrates[] = {
    1000000U,
    2000000U,
    4000000U,
    5000000U,

#if SETTINGS_CAN_FD_DATA_BITRATE_MAX >= 8000000U
    8000000U,
#endif
};

#define SETTINGS_CAN_FD_DATA_BITRATE_COUNT \
    (sizeof(s_can_fd_data_bitrates) / \
     sizeof(s_can_fd_data_bitrates[0]))

#define SETTINGS_CAN_BITRATE_COUNT \
    (sizeof(s_can_bitrates) / sizeof(s_can_bitrates[0]))

static void settings_screen_refresh_usb_rndis_info(
    const app_settings_t *settings
);

static void settings_screen_refresh_wifi_info(
    const app_settings_t *settings
);

static void settings_screen_refresh_wifi_sta_info(
    const app_settings_t *settings
);

static void settings_screen_refresh_wifi_scan(void);

static void wifi_scan_button_event_cb(
    lv_event_t *event
);
static void wifi_scan_table_event_cb(
    lv_event_t *event
);

static void wifi_sta_enabled_switch_event_cb(
    lv_event_t *event
);

static esp_err_t wifi_credentials_submit_cb(
    const char *ssid,
    const char *password,
    void *context
);

static void settings_screen_format_size(
    uint64_t bytes,
    char *buffer,
    size_t buffer_size
);

static void settings_screen_set_disabled_state(
    lv_obj_t *object,
    bool disabled
);

static void settings_screen_refresh_storage_info(void);

static void settings_screen_refresh_system_info(void);

static esp_err_t settings_screen_refresh(void);

static void settings_screen_refresh_can_info(
    const app_settings_t *settings
);

static void settings_screen_refresh_can_secondary_info(
    const app_settings_t *settings
);

static void settings_screen_set_switch_state(
    lv_obj_t *switch_object,
    bool checked
);

static bool settings_screen_find_can_fd_data_bitrate(
    uint32_t nominal_bitrate,
    uint32_t requested_bitrate,
    uint16_t *selected_index,
    uint32_t *selected_bitrate
)
{
    if ((selected_index == NULL) ||
        (selected_bitrate == NULL)) {

        return false;
    }

    const size_t bitrate_count =
        sizeof(s_can_fd_data_bitrates) /
        sizeof(s_can_fd_data_bitrates[0]);

    /*
     * Preserve the selected bitrate when it is valid.
     */
    for (size_t index = 0U;
         index < bitrate_count;
         ++index) {

        if ((s_can_fd_data_bitrates[index] ==
             requested_bitrate) &&
            (requested_bitrate >
             nominal_bitrate)) {

            *selected_index =
                (uint16_t)index;

            *selected_bitrate =
                requested_bitrate;

            return true;
        }
    }

    /*
     * Otherwise select the first supported bitrate greater than the
     * nominal bitrate.
     */
    for (size_t index = 0U;
         index < bitrate_count;
         ++index) {

        if (s_can_fd_data_bitrates[index] >
            nominal_bitrate) {

            *selected_index =
                (uint16_t)index;

            *selected_bitrate =
                s_can_fd_data_bitrates[index];

            return true;
        }
    }

    return false;
}

static void buzzer_volume_released_event_cb(
    lv_event_t *event
)
{
    (void)event;

    if (s_updating_controls) {
        return;
    }

    const esp_err_t result =
        buzzer_service_play(
            BUZZER_SIGNAL_CLICK
        );

    if ((result != ESP_OK) &&
        (result != ESP_ERR_INVALID_STATE)) {

        ESP_LOGW(
            TAG,
            "Failed to play volume preview: %s",
            esp_err_to_name(result)
        );
    }
}

static void sound_enabled_switch_event_cb(
    lv_event_t *event
)
{
    if (s_updating_controls) {
        return;
    }

    lv_obj_t *switch_object =
        lv_event_get_target_obj(
            event
        );

    if (switch_object == NULL) {
        return;
    }

    const bool enabled =
        lv_obj_has_state(
            switch_object,
            LV_STATE_CHECKED
        );

    app_settings_t settings;

    esp_err_t result =
        settings_model_get(
            &settings
        );

    if (result == ESP_OK) {
        result =
            settings_service_set_sound(
                enabled,
                settings.sound.volume_percent
            );
    }

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to apply sound setting: %s",
            esp_err_to_name(result)
        );

        (void)settings_screen_refresh();
        return;
    }

    if (s_buzzer_volume_slider != NULL) {
        if (enabled) {
            lv_obj_remove_state(
                s_buzzer_volume_slider,
                LV_STATE_DISABLED
            );
        } else {
            lv_obj_add_state(
                s_buzzer_volume_slider,
                LV_STATE_DISABLED
            );
        }
    }
}

static void buzzer_volume_slider_event_cb(
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

    const int32_t value =
        lv_slider_get_value(
            slider
        );

    app_settings_t settings;

    esp_err_t result =
        settings_model_get(
            &settings
        );

    if (result == ESP_OK) {
        result =
            settings_service_set_sound(
                settings.sound.enabled,
                (uint8_t)value
            );
    }

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to apply sound volume: %s",
            esp_err_to_name(result)
        );

        (void)settings_screen_refresh();
        return;
    }

    if (s_buzzer_volume_value_label != NULL) {
        lv_label_set_text_fmt(
            s_buzzer_volume_value_label,
            "%u%%",
            (unsigned int)value
        );
    }
}

static void settings_screen_status_refresh_timer_cb(
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

    settings_screen_refresh_wifi_sta_info(
        &settings
    );

    settings_screen_refresh_wifi_scan();

    settings_screen_refresh_usb_rndis_info(
        &settings
    );

    settings_screen_refresh_system_info();

    settings_screen_refresh_can_info(
        &settings
    );

    settings_screen_refresh_can_secondary_info(
        &settings
    );

    s_updating_controls = false;
}

static uint16_t settings_screen_can_bitrate_to_index(
    uint32_t bitrate
)
{
    for (uint16_t index = 0U;
         index < SETTINGS_CAN_BITRATE_COUNT;
         ++index) {

        if (s_can_bitrates[index] == bitrate) {
            return index;
        }
    }

    /*
     * Default: 500 kbit/s.
     */
    return 8U;
}

static uint16_t settings_screen_can_fd_data_bitrate_to_index(
    uint32_t bitrate,
    uint32_t nominal_bitrate
)
{
    uint16_t selected_index = 0U;
    uint32_t selected_bitrate = 0U;

    if (settings_screen_find_can_fd_data_bitrate(
            nominal_bitrate,
            bitrate,
            &selected_index,
            &selected_bitrate
        )) {

        return selected_index;
    }

    return 0U;
}

static void can_secondary_settings_event_cb(
    lv_event_t *event
)
{
    (void)event;

    if (s_updating_controls ||
        (s_can_secondary_enabled_switch == NULL) ||
        (s_can_secondary_nominal_dropdown == NULL) ||
        (s_can_secondary_data_dropdown == NULL) ||
        (s_can_secondary_fd_switch == NULL) ||
        (s_can_secondary_brs_switch == NULL) ||
        (s_can_secondary_listen_only_switch == NULL)) {

        return;
    }

    const uint16_t nominal_index =
        lv_dropdown_get_selected(
            s_can_secondary_nominal_dropdown
        );

    const uint16_t data_index =
        lv_dropdown_get_selected(
            s_can_secondary_data_dropdown
        );

    if ((nominal_index >= SETTINGS_CAN_BITRATE_COUNT) ||
        (data_index >= SETTINGS_CAN_FD_DATA_BITRATE_COUNT)) {

        (void)settings_screen_refresh();
        return;
    }

    const bool enabled =
        lv_obj_has_state(
            s_can_secondary_enabled_switch,
            LV_STATE_CHECKED
        );

    const bool fd_enabled =
        lv_obj_has_state(
            s_can_secondary_fd_switch,
            LV_STATE_CHECKED
        );

    bool brs_enabled =
        lv_obj_has_state(
            s_can_secondary_brs_switch,
            LV_STATE_CHECKED
        );

    const bool listen_only =
        lv_obj_has_state(
            s_can_secondary_listen_only_switch,
            LV_STATE_CHECKED
        );

    const uint32_t nominal_bitrate =
        s_can_bitrates[nominal_index];

    uint32_t data_bitrate =
        nominal_bitrate;

    /*
     * BRS is available only for CAN FD.
     */
    if (!fd_enabled) {
        brs_enabled = false;
    }

    if (brs_enabled) {
        const uint32_t requested_bitrate =
            s_can_fd_data_bitrates[data_index];

        uint16_t selected_index = 0U;

        if (!settings_screen_find_can_fd_data_bitrate(
                nominal_bitrate,
                requested_bitrate,
                &selected_index,
                &data_bitrate
            )) {

            ESP_LOGW(
                TAG,
                "No valid CAN FD data bitrate above %lu bit/s",
                (unsigned long)nominal_bitrate
            );

            (void)settings_screen_refresh();
            return;
        }

        lv_dropdown_set_selected(
            s_can_secondary_data_dropdown,
            selected_index
        );
    }

    if (!brs_enabled) {
        data_bitrate =
            nominal_bitrate;
    }

    const esp_err_t result =
        settings_service_set_can_secondary(
            enabled,
            nominal_bitrate,
            data_bitrate,
            fd_enabled,
            brs_enabled,
            listen_only
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to apply secondary CAN settings "
            "(nominal=%lu, data=%lu, FD=%s, BRS=%s): %s",
            (unsigned long)nominal_bitrate,
            (unsigned long)data_bitrate,
            fd_enabled ? "enabled" : "disabled",
            brs_enabled ? "enabled" : "disabled",
            esp_err_to_name(result)
        );
    }

    (void)settings_screen_refresh();
}

static void can_settings_event_cb(
    lv_event_t *event
)
{
    (void)event;

    if (s_updating_controls ||
        (s_can_enabled_switch == NULL) ||
        (s_can_bitrate_dropdown == NULL) ||
        (s_can_listen_only_switch == NULL)) {

        return;
    }

    const uint16_t bitrate_index =
        lv_dropdown_get_selected(
            s_can_bitrate_dropdown
        );

    if (bitrate_index >=
        SETTINGS_CAN_BITRATE_COUNT) {

        (void)settings_screen_refresh();
        return;
    }

    const bool enabled =
        lv_obj_has_state(
            s_can_enabled_switch,
            LV_STATE_CHECKED
        );

    const bool listen_only =
        lv_obj_has_state(
            s_can_listen_only_switch,
            LV_STATE_CHECKED
        );

    const esp_err_t result =
        settings_service_set_can_primary(
            enabled,
            s_can_bitrates[bitrate_index],
            listen_only
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to apply primary CAN settings: %s",
            esp_err_to_name(result)
        );

        (void)settings_screen_refresh();
        return;
    }

    (void)settings_screen_refresh();
}

static const char *settings_screen_can_fd_state_to_string(
    can_fd_mcp2518fd_state_t state
)
{
    switch (state) {
        case CAN_FD_MCP2518FD_STATE_STOPPED:
            return "Stopped";

        case CAN_FD_MCP2518FD_STATE_ERROR_ACTIVE:
            return "Active";

        case CAN_FD_MCP2518FD_STATE_ERROR_WARNING:
            return "Warning";

        case CAN_FD_MCP2518FD_STATE_ERROR_PASSIVE:
            return "Error passive";

        case CAN_FD_MCP2518FD_STATE_BUS_OFF:
            return "Bus off";

        case CAN_FD_MCP2518FD_STATE_UNKNOWN:
        default:
            return "Unknown";
    }
}

static void settings_screen_refresh_can_secondary_info(
    const app_settings_t *settings
)
{
    if (settings == NULL) {
        return;
    }

    const bool enabled =
        settings->can_secondary.enabled;

    const bool fd_enabled =
        settings->can_secondary.fd_enabled;

    const bool brs_enabled =
        settings->can_secondary.brs_enabled;

    settings_screen_set_switch_state(
        s_can_secondary_enabled_switch,
        enabled
    );

    settings_screen_set_switch_state(
        s_can_secondary_fd_switch,
        fd_enabled
    );

    settings_screen_set_switch_state(
        s_can_secondary_brs_switch,
        brs_enabled
    );

    settings_screen_set_switch_state(
        s_can_secondary_listen_only_switch,
        settings->can_secondary.listen_only
    );

    if (s_can_secondary_nominal_dropdown != NULL) {
        lv_dropdown_set_selected(
            s_can_secondary_nominal_dropdown,
            settings_screen_can_bitrate_to_index(
                settings->can_secondary.nominal_bitrate
            )
        );
    }

    if (s_can_secondary_data_dropdown != NULL) {
        lv_dropdown_set_selected(
            s_can_secondary_data_dropdown,
            settings_screen_can_fd_data_bitrate_to_index(
                settings->can_secondary.data_bitrate,
                settings->can_secondary.nominal_bitrate
            )
        );
    }

    lv_obj_t *enabled_controls[] = {
        s_can_secondary_nominal_dropdown,
        s_can_secondary_fd_switch,
        s_can_secondary_listen_only_switch,
    };

    for (size_t index = 0U;
        index < sizeof(enabled_controls) /
                sizeof(enabled_controls[0]);
        ++index) {

        settings_screen_set_disabled_state(
            enabled_controls[index],
            !enabled
        );
    }

    settings_screen_set_disabled_state(
        s_can_secondary_brs_switch,
        !(enabled && fd_enabled)
    );

    settings_screen_set_disabled_state(
        s_can_secondary_data_dropdown,
        !(enabled && fd_enabled && brs_enabled)
    );

    if (s_can_secondary_info_label == NULL) {
        return;
    }

    if (!enabled) {
        lv_label_set_text_fmt(
            s_can_secondary_info_label,
            "State: Disabled\n"
            "Nominal: %lu bit/s   Data: %lu bit/s",
            (unsigned long)
                settings->can_secondary.nominal_bitrate,
            (unsigned long)
                settings->can_secondary.data_bitrate
        );

        return;
    }

    can_fd_mcp2518fd_info_t info;

    const esp_err_t result =
        can_fd_service_get_info(
            &info
        );

    if (result != ESP_OK) {
        lv_label_set_text_fmt(
            s_can_secondary_info_label,
            "State: Unavailable\nError: %s",
            esp_err_to_name(result)
        );

        return;
    }

    lv_label_set_text_fmt(
        s_can_secondary_info_label,
        "State: %s\n"
        "Nominal: %lu   Data: %lu bit/s\n"
        "FD: %s   BRS: %s\n"
        "RX: %lu   TX: %lu   Dropped: %lu\n"
        "TEC: %u   REC: %u   Bus errors: %lu",
        settings_screen_can_fd_state_to_string(
            info.state
        ),
        (unsigned long)info.nominal_bitrate,
        (unsigned long)info.data_bitrate,
        info.fd_enabled ? "On" : "Off",
        info.brs_enabled ? "On" : "Off",
        (unsigned long)info.received_frames,
        (unsigned long)info.transmitted_frames,
        (unsigned long)info.dropped_rx_frames,
        (unsigned int)info.transmit_error_count,
        (unsigned int)info.receive_error_count,
        (unsigned long)info.bus_error_count
    );
}

static const char *settings_screen_can_state_to_string(
    can_twai_state_t state
)
{
    switch (state) {
        case CAN_TWAI_STATE_STOPPED:
            return "Stopped";

        case CAN_TWAI_STATE_ERROR_ACTIVE:
            return "Active";

        case CAN_TWAI_STATE_ERROR_WARNING:
            return "Warning";

        case CAN_TWAI_STATE_ERROR_PASSIVE:
            return "Error passive";

        case CAN_TWAI_STATE_BUS_OFF:
            return "Bus off";

        case CAN_TWAI_STATE_RECOVERING:
            return "Recovering";

        case CAN_TWAI_STATE_UNKNOWN:
        default:
            return "Unknown";
    }
}

static void settings_screen_refresh_can_info(
    const app_settings_t *settings
)
{
    if (settings == NULL) {
        return;
    }

    settings_screen_set_switch_state(
        s_can_enabled_switch,
        settings->can_primary.enabled
    );

    settings_screen_set_switch_state(
        s_can_listen_only_switch,
        settings->can_primary.listen_only
    );

    if (s_can_bitrate_dropdown != NULL) {
        lv_dropdown_set_selected(
            s_can_bitrate_dropdown,
            settings_screen_can_bitrate_to_index(
                settings->can_primary.bitrate
            )
        );
    }

    if (s_can_bitrate_dropdown != NULL) {
        if (settings->can_primary.enabled) {
            lv_obj_remove_state(
                s_can_bitrate_dropdown,
                LV_STATE_DISABLED
            );
        } else {
            lv_obj_add_state(
                s_can_bitrate_dropdown,
                LV_STATE_DISABLED
            );
        }
    }

    if (s_can_listen_only_switch != NULL) {
        if (settings->can_primary.enabled) {
            lv_obj_remove_state(
                s_can_listen_only_switch,
                LV_STATE_DISABLED
            );
        } else {
            lv_obj_add_state(
                s_can_listen_only_switch,
                LV_STATE_DISABLED
            );
        }
    }

    if (s_can_info_label == NULL) {
        return;
    }

    if (!settings->can_primary.enabled) {
        lv_label_set_text_fmt(
            s_can_info_label,
            "State: Disabled\n"
            "Configured bitrate: %lu bit/s",
            (unsigned long)settings->can_primary.bitrate
        );

        return;
    }

    can_twai_driver_info_t info;

    const esp_err_t result =
        can_service_get_info(
            &info
        );

    if (result != ESP_OK) {
        lv_label_set_text_fmt(
            s_can_info_label,
            "State: Unavailable\n"
            "Error: %s",
            esp_err_to_name(result)
        );

        return;
    }

    lv_label_set_text_fmt(
        s_can_info_label,
        "State: %s\n"
        "Bitrate: %lu bit/s\n"
        "RX: %lu   TX: %lu   Dropped: %lu\n"
        "TEC: %u   REC: %u   Bus errors: %lu",
        settings_screen_can_state_to_string(
            info.state
        ),
        (unsigned long)info.bitrate,
        (unsigned long)info.received_frames,
        (unsigned long)info.transmitted_frames,
        (unsigned long)info.dropped_rx_frames,
        (unsigned int)info.transmit_error_count,
        (unsigned int)info.receive_error_count,
        (unsigned long)info.bus_error_count
    );
}

static void settings_screen_start_status_refresh(void)
{
    if (s_status_refresh_timer != NULL) {
        return;
    }

    s_status_refresh_timer =
        lv_timer_create(
            settings_screen_status_refresh_timer_cb,
            SETTINGS_STATUS_REFRESH_PERIOD_MS,
            NULL
        );

    if (s_status_refresh_timer == NULL) {
        ESP_LOGW(
            TAG,
            "Failed to create settings status refresh timer"
        );
    }
}

static void settings_screen_stop_status_refresh(void)
{
    if (s_status_refresh_timer == NULL) {
        return;
    }

    lv_timer_delete(
        s_status_refresh_timer
    );

    s_status_refresh_timer = NULL;
}

static void settings_screen_delete_event_cb(
    lv_event_t *event
)
{
    (void)event;

    settings_screen_stop_status_refresh();

    s_root = NULL;
    s_tabview = NULL;
    s_brightness_slider = NULL;
    s_brightness_value_label = NULL;
    s_sound_enabled_switch = NULL;
    s_buzzer_volume_slider = NULL;
    s_buzzer_volume_value_label = NULL;
    s_sd_logging_switch = NULL;
    s_storage_info_label = NULL;
    s_animations_switch = NULL;
    s_theme_dropdown = NULL;
    s_restart_required_label = NULL;
    s_wifi_enabled_switch = NULL;
    s_wifi_info_label = NULL;
    s_wifi_scan_button = NULL;
    s_wifi_scan_status_label = NULL;
    s_wifi_scan_table = NULL;
    s_wifi_scan_results_displayed = false;
    s_wifi_sta_enabled_switch = NULL;
    s_wifi_sta_info_label = NULL;
    s_usb_rndis_enabled_switch = NULL;
    s_usb_rndis_info_label = NULL;
    s_system_info_label = NULL;
    s_updating_controls = false;
    s_can_secondary_enabled_switch = NULL;
    s_can_secondary_nominal_dropdown = NULL;
    s_can_secondary_data_dropdown = NULL;
    s_can_secondary_fd_switch = NULL;
    s_can_secondary_brs_switch = NULL;
    s_can_secondary_listen_only_switch = NULL;
    s_can_secondary_info_label = NULL;

    memset(
        &s_selected_wifi_network,
        0,
        sizeof(s_selected_wifi_network)
    );

    s_wifi_network_selected = false;
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

static const char *settings_screen_reset_reason_to_string(
    esp_reset_reason_t reason
)
{
    switch (reason) {
        case ESP_RST_POWERON:
            return "Power on";

        case ESP_RST_EXT:
            return "External reset";

        case ESP_RST_SW:
            return "Software restart";

        case ESP_RST_PANIC:
            return "Application panic";

        case ESP_RST_INT_WDT:
            return "Interrupt watchdog";

        case ESP_RST_TASK_WDT:
            return "Task watchdog";

        case ESP_RST_WDT:
            return "Other watchdog";

        case ESP_RST_DEEPSLEEP:
            return "Deep sleep";

        case ESP_RST_BROWNOUT:
            return "Brownout";

        case ESP_RST_SDIO:
            return "SDIO reset";

        case ESP_RST_USB:
            return "USB reset";

        case ESP_RST_JTAG:
            return "JTAG reset";

        case ESP_RST_EFUSE:
            return "eFuse error";

        case ESP_RST_PWR_GLITCH:
            return "Power glitch";

        case ESP_RST_CPU_LOCKUP:
            return "CPU lockup";

        case ESP_RST_UNKNOWN:
        default:
            return "Unknown";
    }
}

static void settings_screen_refresh_system_info(void)
{
    if (s_system_info_label == NULL) {
        return;
    }

    system_model_t model;

    const esp_err_t result =
        system_model_get_snapshot(
            &model
        );

    if (result != ESP_OK) {
        lv_label_set_text(
            s_system_info_label,
            "System information unavailable"
        );

        return;
    }

    const uint32_t days =
        model.uptime_sec / 86400U;

    const uint32_t hours =
        (model.uptime_sec % 86400U) / 3600U;

    const uint32_t minutes =
        (model.uptime_sec % 3600U) / 60U;

    const uint32_t seconds =
        model.uptime_sec % 60U;

    const char *device_name =
        model.device_name[0] != '\0'
            ? model.device_name
            : "N/A";

    const char *firmware_version =
        model.firmware_version[0] != '\0'
            ? model.firmware_version
            : "N/A";

    const char *hardware_version =
        model.hardware_version[0] != '\0'
            ? model.hardware_version
            : "N/A";

    const char *serial_number =
        model.serial_number[0] != '\0'
            ? model.serial_number
            : "N/A";

    const char *chip_model =
        model.chip_model[0] != '\0'
            ? model.chip_model
            : "N/A";

    const char *reset_reason =
        settings_screen_reset_reason_to_string(
            model.reset_reason
        );

    char temperature[24];

    if (model.chip_temperature_valid) {
        const int32_t temperature_tenths =
            (int32_t)(
                model.chip_temperature_celsius *
                10.0F
            );

        const bool negative =
            temperature_tenths < 0;

        const uint32_t absolute_tenths =
            (uint32_t)(
                negative
                    ? -temperature_tenths
                    : temperature_tenths
            );

        (void)snprintf(
            temperature,
            sizeof(temperature),
            "%s%u.%u C",
            negative ? "-" : "",
            (unsigned int)(
                absolute_tenths / 10U
            ),
            (unsigned int)(
                absolute_tenths % 10U
            )
        );
    } else {
        (void)strlcpy(
            temperature,
            "Unavailable",
            sizeof(temperature)
        );
    }

    const uint32_t chip_revision_major =
        model.chip_revision / 100U;

    const uint32_t chip_revision_minor =
        model.chip_revision % 100U;

    char current_time[48];

    struct tm local_time = {0};

    const esp_err_t time_result =
        time_service_get_local_time(
            &local_time
        );

    if ((time_result == ESP_OK) &&
        (strftime(
            current_time,
            sizeof(current_time),
            "%Y-%m-%d %H:%M:%S %Z",
            &local_time
        ) > 0U)) {

        /* The formatted local time is ready. */
    } else {
        (void)strlcpy(
            current_time,
            "Waiting for synchronization",
            sizeof(current_time)
        );
    }

    time_service_info_t time_info;

    const esp_err_t time_info_result =
        time_service_get_info(
            &time_info
        );

    const char *time_sync_state = "Unavailable";

    if (time_info_result == ESP_OK) {
        if (time_info.synchronized) {
            time_sync_state = "Synchronized";
        } else if (time_info.running) {
            time_sync_state = "Waiting for SNTP";
        } else if (time_info.time_valid) {
            time_sync_state = "Clock valid";
        } else {
            time_sync_state = "Stopped";
        }
    }

    lv_label_set_text_fmt(
        s_system_info_label,

        "Device\n"
        "  Name: %s\n"
        "  Firmware: %s\n"
        "  Hardware: %s\n"
        "  Serial: %s\n"
        "\n"
        "Processor\n"
        "  Model: %s\n"
        "  Revision: v%u.%u\n"
        "  Cores: %u\n"
        "  Frequency: %u MHz\n"
        "  Temperature: %s\n"
        "  CPU usage: %u%%\n"
        "\n"
        "Memory\n"
        "  Flash: %u MB\n"
        "  PSRAM: %u MB\n"
        "  PSRAM free: %u KB\n"
        "  PSRAM minimum: %u KB\n"
        "  Internal heap free: %u KB\n"
        "  Internal heap minimum: %u KB\n"
        "\n"
        "Runtime\n"
        "  Local time: %s\n"
        "  Time sync: %s\n"
        "  Uptime: %u d %02u:%02u:%02u\n"
        "  Last reset: %s",

        device_name,
        firmware_version,
        hardware_version,
        serial_number,

        chip_model,
        (unsigned int)chip_revision_major,
        (unsigned int)chip_revision_minor,
        (unsigned int)model.chip_cores,
        (unsigned int)model.cpu_frequency_mhz,
        temperature,
        (unsigned int)model.cpu_usage,

        (unsigned int)(
            model.flash_size /
            (1024U * 1024U)
        ),
        (unsigned int)(
            model.psram_size /
            (1024U * 1024U)
        ),
        (unsigned int)(
            model.psram_free / 1024U
        ),
        (unsigned int)(
            model.psram_minimum_free / 1024U
        ),
        (unsigned int)(
            model.free_heap / 1024U
        ),
        (unsigned int)(
            model.minimum_free_heap / 1024U
        ),

        current_time,
        time_sync_state,

        (unsigned int)days,
        (unsigned int)hours,
        (unsigned int)minutes,
        (unsigned int)seconds,
        reset_reason
    );
}

static void settings_screen_refresh_storage_info(void)
{
    if (s_storage_info_label == NULL) {
        return;
    }

    bool internal_mounted = false;

    const esp_err_t internal_result =
        storage_service_get_mounted(
            &internal_mounted
        );

    const char *internal_state;

    if (internal_result != ESP_OK) {
        internal_state = "Unavailable";
    } else {
        internal_state =
            internal_mounted
                ? "Mounted"
                : "Not mounted";
    }

    storage_sd_info_t sd_info;

    const esp_err_t sd_result =
        storage_sd_service_get_info(
            &sd_info
        );

    if (sd_result != ESP_OK) {
        storage_sd_state_t sd_state =
            STORAGE_SD_STATE_UNAVAILABLE;

        const esp_err_t state_result =
            storage_sd_service_get_state(
                &sd_state
            );

        const char *card_state =
            "Unavailable";

        if (state_result == ESP_OK) {
            switch (sd_state) {
                case STORAGE_SD_STATE_MOUNTED:
                    card_state = "Information unavailable";
                    break;

                case STORAGE_SD_STATE_ERROR:
                    card_state = "Error";
                    break;

                case STORAGE_SD_STATE_UNAVAILABLE:
                default:
                    card_state = "Not mounted";
                    break;
            }
        }

        lv_label_set_text_fmt(
            s_storage_info_label,
            "Internal storage: %s\n"
            "SD card: %s",
            internal_state,
            card_state
        );

        return;
    }

    char total_text[24];
    char used_text[24];
    char free_text[24];

    settings_screen_format_size(
        sd_info.total_bytes,
        total_text,
        sizeof(total_text)
    );

    settings_screen_format_size(
        sd_info.used_bytes,
        used_text,
        sizeof(used_text)
    );

    settings_screen_format_size(
        sd_info.free_bytes,
        free_text,
        sizeof(free_text)
    );

    lv_label_set_text_fmt(
        s_storage_info_label,
        "Internal storage: %s\n"
        "SD card: Mounted\n"
        "Filesystem: %s\n"
        "Capacity: %s\n"
        "Used: %s\n"
        "Free: %s",
        internal_state,
        sd_info.filesystem,
        total_text,
        used_text,
        free_text
    );
}

static void settings_screen_refresh_wifi_sta_info(
    const app_settings_t *settings
)
{
    if (settings == NULL) {
        return;
    }

    const bool credentials_configured =
        (settings->wifi_sta.ssid[0] != '\0') &&
        (settings->wifi_sta.credential_id[0] != '\0');

    settings_screen_set_switch_state(
        s_wifi_sta_enabled_switch,
        settings->wifi_sta.enabled
    );

    if (s_wifi_sta_enabled_switch != NULL) {
        if (credentials_configured) {
            lv_obj_remove_state(
                s_wifi_sta_enabled_switch,
                LV_STATE_DISABLED
            );
        } else {
            lv_obj_add_state(
                s_wifi_sta_enabled_switch,
                LV_STATE_DISABLED
            );
        }
    }

    if (s_wifi_sta_info_label == NULL) {
        return;
    }

    wifi_service_info_t info = {0};

    const esp_err_t result =
        wifi_service_get_info(
            &info
        );

    const char *configured_ssid =
        settings->wifi_sta.ssid[0] != '\0'
            ? settings->wifi_sta.ssid
            : "Not configured";

    if (result != ESP_OK) {
        lv_label_set_text_fmt(
            s_wifi_sta_info_label,
            "State: Unavailable\n"
            "SSID: %s\n"
            "IP address: N/A\n"
            "Gateway: N/A\n"
            "DNS: N/A",
            configured_ssid
        );

        return;
    }

    const bool sta_running =
        info.started &&
        info.sta_enabled;

    const bool sta_connected =
        sta_running &&
        info.sta_connected;

    const char *state;

    if (!credentials_configured) {
        state = "Select a network below";

    } else if (!settings->wifi_sta.enabled) {
        state = "Disabled";

    } else if (!sta_running) {
        state = "Stopped";

    } else if (!sta_connected) {
        state = "Connecting";

    } else {
        state = "Connected";
    }

    const char *ssid =
        info.sta_ssid[0] != '\0'
            ? info.sta_ssid
            : configured_ssid;

    if (sta_connected) {
        lv_label_set_text_fmt(
            s_wifi_sta_info_label,
            "State: %s\n"
            "SSID: %s\n"
            "IP address: %s\n"
            "Netmask: %s\n"
            "Gateway: %s\n"
            "DNS: %s\n"
            "Signal: %d dBm",
            state,
            ssid,
            info.sta_ip_address[0] != '\0'
                ? info.sta_ip_address
                : "N/A",
            info.sta_netmask[0] != '\0'
                ? info.sta_netmask
                : "N/A",
            info.sta_gateway[0] != '\0'
                ? info.sta_gateway
                : "N/A",
            info.sta_dns_address[0] != '\0'
                ? info.sta_dns_address
                : "N/A",
            (int)info.sta_rssi
        );
    } else {
        lv_label_set_text_fmt(
            s_wifi_sta_info_label,
            "State: %s\n"
            "SSID: %s\n"
            "IP address: Not connected\n"
            "Gateway: N/A\n"
            "DNS: N/A",
            state,
            ssid
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

    const bool ap_running =
        info.started &&
        info.ap_enabled;

    const char *state =
        ap_running
            ? "Running"
            : "Stopped";

    const char *ssid =
        info.ssid[0] != '\0'
            ? info.ssid
            : settings->wifi_ap.ssid;

    const char *ip_address =
        ap_running &&
        info.ip_address[0] != '\0'
            ? info.ip_address
            : "N/A";

    const char *dhcp_start =
        ap_running &&
        info.dhcp_start[0] != '\0'
            ? info.dhcp_start
            : "N/A";

    const char *dhcp_end =
        ap_running &&
        info.dhcp_end[0] != '\0'
            ? info.dhcp_end
            : "N/A";

    const char *dns_address =
        ap_running &&
        info.dns_address[0] != '\0'
            ? info.dns_address
            : "N/A";

    if (ap_running) {
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

static void settings_screen_format_size(
    uint64_t bytes,
    char *buffer,
    size_t buffer_size
)
{
    if ((buffer == NULL) ||
        (buffer_size == 0U)) {

        return;
    }

    const uint64_t kibibyte = 1024U;
    const uint64_t mebibyte =
        1024U * kibibyte;
    const uint64_t gibibyte =
        1024U * mebibyte;

    if (bytes >= gibibyte) {
        const uint64_t whole =
            bytes / gibibyte;

        const uint64_t decimal =
            ((bytes % gibibyte) * 10U) /
            gibibyte;

        (void)snprintf(
            buffer,
            buffer_size,
            "%llu.%llu GB",
            (unsigned long long)whole,
            (unsigned long long)decimal
        );
    } else if (bytes >= mebibyte) {
        const uint64_t whole =
            bytes / mebibyte;

        const uint64_t decimal =
            ((bytes % mebibyte) * 10U) /
            mebibyte;

        (void)snprintf(
            buffer,
            buffer_size,
            "%llu.%llu MB",
            (unsigned long long)whole,
            (unsigned long long)decimal
        );
    } else if (bytes >= kibibyte) {
        (void)snprintf(
            buffer,
            buffer_size,
            "%llu KB",
            (unsigned long long)(
                bytes / kibibyte
            )
        );
    } else {
        (void)snprintf(
            buffer,
            buffer_size,
            "%llu B",
            (unsigned long long)bytes
        );
    }
}

static void settings_screen_refresh_restart_required(void)
{
    if (s_restart_required_label == NULL) {
        return;
    }

    bool restart_required = false;

    const esp_err_t result =
        settings_service_get_restart_required(
            &restart_required
        );

    if (result != ESP_OK) {
        ESP_LOGD(
            TAG,
            "Failed to get restart-required state: %s",
            esp_err_to_name(result)
        );

        lv_obj_add_flag(
            s_restart_required_label,
            LV_OBJ_FLAG_HIDDEN
        );

        return;
    }

    if (restart_required) {
        lv_obj_remove_flag(
            s_restart_required_label,
            LV_OBJ_FLAG_HIDDEN
        );
    } else {
        lv_obj_add_flag(
            s_restart_required_label,
            LV_OBJ_FLAG_HIDDEN
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
        s_sound_enabled_switch,
        settings.sound.enabled
    );

    if (s_buzzer_volume_slider != NULL) {
        if (settings.sound.enabled) {
            lv_obj_remove_state(
                s_buzzer_volume_slider,
                LV_STATE_DISABLED
            );
        } else {
            lv_obj_add_state(
                s_buzzer_volume_slider,
                LV_STATE_DISABLED
            );
        }

        lv_slider_set_value(
            s_buzzer_volume_slider,
            (int32_t)settings.sound.volume_percent,
            LV_ANIM_OFF
        );
    }

    if (s_buzzer_volume_value_label != NULL) {
        lv_label_set_text_fmt(
            s_buzzer_volume_value_label,
            "%u%%",
            (unsigned int)settings.sound.volume_percent
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

    if (s_theme_dropdown != NULL) {
        lv_dropdown_set_selected(
            s_theme_dropdown,
            settings.ui.theme_mode ==
                UI_THEME_MODE_DARK
                    ? 1U
                    : 0U
        );
    }

    settings_screen_refresh_restart_required();

    settings_screen_refresh_wifi_info(
        &settings
    );

    settings_screen_refresh_wifi_sta_info(
        &settings
    );

    settings_screen_refresh_wifi_scan();

    settings_screen_refresh_usb_rndis_info(
        &settings
    );

    settings_screen_refresh_storage_info();

    settings_screen_refresh_system_info();

    settings_screen_refresh_can_info(
        &settings
    );

    settings_screen_refresh_can_secondary_info(
        &settings
    );

    s_updating_controls = false;

    return ESP_OK;
}

static void settings_screen_refresh_wifi_scan(void)
{
    if ((s_wifi_scan_button == NULL) ||
        (s_wifi_scan_status_label == NULL) ||
        (s_wifi_scan_table == NULL)) {

        return;
    }

    wifi_service_scan_info_t scan_info = {0};

    const esp_err_t info_result =
        wifi_service_get_scan_info(
            &scan_info
        );

    if (info_result != ESP_OK) {
        lv_obj_remove_state(
            s_wifi_scan_button,
            LV_STATE_DISABLED
        );

        lv_label_set_text(
            s_wifi_scan_status_label,
            "Scanner unavailable"
        );

        return;
    }

    if (scan_info.state ==
        WIFI_SERVICE_SCAN_STATE_RUNNING) {

        lv_obj_add_state(
            s_wifi_scan_button,
            LV_STATE_DISABLED
        );

        lv_label_set_text(
            s_wifi_scan_status_label,
            "Scanning..."
        );

        return;
    }

    lv_obj_remove_state(
        s_wifi_scan_button,
        LV_STATE_DISABLED
    );

    if (scan_info.state ==
        WIFI_SERVICE_SCAN_STATE_ERROR) {

        lv_label_set_text_fmt(
            s_wifi_scan_status_label,
            "Scan failed: %s",
            esp_err_to_name(
                scan_info.last_error
            )
        );

        s_wifi_scan_results_displayed = false;

        return;
    }

    if (scan_info.state !=
        WIFI_SERVICE_SCAN_STATE_COMPLETE) {

        lv_label_set_text(
            s_wifi_scan_status_label,
            "Press Scan to find networks"
        );

        return;
    }

    if (s_wifi_scan_results_displayed) {
        return;
    }

    size_t result_capacity =
        scan_info.result_count;

    if (result_capacity >
        WIFI_SERVICE_SCAN_MAX_RESULT_COUNT) {

        result_capacity =
            WIFI_SERVICE_SCAN_MAX_RESULT_COUNT;
    }

    /*
     * A completed scan may legitimately contain no networks.
     */
    if (result_capacity == 0U) {
        lv_table_set_row_count(
            s_wifi_scan_table,
            1U
        );

        lv_label_set_text(
            s_wifi_scan_status_label,
            "No networks found"
        );

        s_wifi_scan_results_displayed = true;

        return;
    }

    /*
     * Scan results are temporary GUI data and do not require internal or
     * DMA-capable memory, so keep them in PSRAM.
     */
    wifi_service_scan_result_t *results =
        heap_caps_calloc(
            result_capacity,
            sizeof(*results),
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

    if (results == NULL) {
        lv_label_set_text(
            s_wifi_scan_status_label,
            "Not enough PSRAM for scan results"
        );

        ESP_LOGE(
            TAG,
            "Failed to allocate Wi-Fi scan results in PSRAM"
        );

        return;
    }

    size_t result_count = 0U;

    const esp_err_t result =
        wifi_service_get_scan_results(
            results,
            result_capacity,
            &result_count
        );

    if (result != ESP_OK) {
        heap_caps_free(
            results
        );

        lv_label_set_text_fmt(
            s_wifi_scan_status_label,
            "Failed to read results: %s",
            esp_err_to_name(result)
        );

        return;
    }

    lv_table_set_row_count(
        s_wifi_scan_table,
        (uint32_t)result_count + 1U
    );

    lv_table_set_cell_value(
        s_wifi_scan_table,
        0U,
        0U,
        "Network"
    );

    lv_table_set_cell_value(
        s_wifi_scan_table,
        0U,
        1U,
        "RSSI"
    );

    lv_table_set_cell_value(
        s_wifi_scan_table,
        0U,
        2U,
        "Ch"
    );

    for (size_t index = 0U;
         index < result_count;
         ++index) {

        const uint32_t row =
            (uint32_t)index + 1U;

        const char *ssid =
            results[index].ssid[0] != '\0'
                ? results[index].ssid
                : "<hidden>";

        char network_text[
            WIFI_SERVICE_STA_SSID_MAX_LENGTH + 3U
        ];

        (void)snprintf(
            network_text,
            sizeof(network_text),
            "%s%s",
            results[index].password_required
                ? "* "
                : "",
            ssid
        );

        char rssi_text[12];

        (void)snprintf(
            rssi_text,
            sizeof(rssi_text),
            "%d dBm",
            (int)results[index].rssi
        );

        char channel_text[8];

        (void)snprintf(
            channel_text,
            sizeof(channel_text),
            "%u",
            (unsigned int)results[index].channel
        );

        lv_table_set_cell_value(
            s_wifi_scan_table,
            row,
            0U,
            network_text
        );

        lv_table_set_cell_value(
            s_wifi_scan_table,
            row,
            1U,
            rssi_text
        );

        lv_table_set_cell_value(
            s_wifi_scan_table,
            row,
            2U,
            channel_text
        );
    }

    heap_caps_free(
        results
    );

    results = NULL;

    lv_label_set_text_fmt(
        s_wifi_scan_status_label,
        "%u network(s)%s",
        (unsigned int)result_count,
        scan_info.truncated
            ? ", list truncated"
            : ""
    );

    s_wifi_scan_results_displayed = true;
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

static void settings_screen_restart_event_cb(
    lv_event_t *event
)
{
    if (lv_event_get_code(event) !=
        LV_EVENT_CLICKED) {

        return;
    }

    lv_obj_t *button =
        lv_event_get_target(event);

    if (button == NULL) {
        return;
    }
    
    /*
     * Prevent repeated presses while the restart is pending.
     */
    lv_obj_add_state(
        button,
        LV_STATE_DISABLED
    );

    const esp_err_t result =
        system_service_schedule_restart(
            SETTINGS_SCREEN_RESTART_DELAY_MS
        );

    if (result == ESP_OK) {
        settings_screen_stop_status_refresh();

        lv_obj_t *label =
            lv_obj_get_child(
                button,
                0
            );

        if (label != NULL) {
            lv_label_set_text(
                label,
                "Restarting..."
            );
        }

        ESP_LOGI(
            TAG,
            "Graceful restart requested"
        );

        return;
    }

    if (result == ESP_ERR_INVALID_STATE) {
        lv_obj_t *label =
            lv_obj_get_child(
                button,
                0
            );

        if (label != NULL) {
            lv_label_set_text(
                label,
                "Restart pending..."
            );
        }

        settings_screen_stop_status_refresh();

        ESP_LOGW(
            TAG,
            "Restart is already scheduled"
        );

        return;
    }

    /*
     * Scheduling failed, so allow another attempt.
     */
    lv_obj_remove_state(
        button,
        LV_STATE_DISABLED
    );

    ESP_LOGE(
        TAG,
        "Failed to schedule restart: %s",
        esp_err_to_name(result)
    );
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

static void theme_dropdown_event_cb(
    lv_event_t *event
)
{
    if (s_updating_controls) {
        return;
    }

    lv_obj_t *dropdown =
        lv_event_get_target_obj(
            event
        );

    if (dropdown == NULL) {
        return;
    }

    const uint32_t selected =
        lv_dropdown_get_selected(
            dropdown
        );

    const ui_theme_mode_t theme_mode =
        (selected == 1U)
            ? UI_THEME_MODE_DARK
            : UI_THEME_MODE_LIGHT;

    const esp_err_t result =
        settings_service_set_theme_mode(
            theme_mode
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to select GUI theme: %s",
            esp_err_to_name(result)
        );

        (void)settings_screen_refresh();
        return;
    }

    /*
     * Selecting the currently applied theme again clears the pending
     * restart state, so always query the settings service.
     */
    settings_screen_refresh_restart_required();
}

static esp_err_t wifi_credentials_submit_cb(
    const char *ssid,
    const char *password,
    void *context
)
{
    (void)context;

    if ((ssid == NULL) ||
        (password == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        settings_service_set_wifi_sta_credentials(
            ssid,
            password
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to set Wi-Fi Station credentials: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        settings_service_set_wifi_sta_enabled(
            true
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to enable Wi-Fi Station: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        settings_service_save();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to save Wi-Fi Station settings: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    /*
     * Force one refresh of the scan table status. Station connection
     * progress is displayed by the dedicated Station information card.
     */
    s_wifi_scan_results_displayed = false;

    memset(
        &s_selected_wifi_network,
        0,
        sizeof(s_selected_wifi_network)
    );

    s_wifi_network_selected = false;

    ESP_LOGI(
        TAG,
        "Wi-Fi Station connection requested: SSID=%s",
        ssid
    );

    return ESP_OK;
}

static void wifi_scan_table_event_cb(
    lv_event_t *event
)
{
    lv_obj_t *table =
        lv_event_get_target_obj(event);

    if ((table == NULL) ||
        (table != s_wifi_scan_table)) {

        return;
    }

    uint32_t selected_row = 0U;
    uint32_t selected_column = 0U;

    lv_table_get_selected_cell(
        table,
        &selected_row,
        &selected_column
    );

    (void)selected_column;

    /*
     * Row zero contains the table header.
     */
    if (selected_row == 0U) {
        return;
    }

    wifi_service_scan_info_t scan_info = {0};

    esp_err_t result =
        wifi_service_get_scan_info(
            &scan_info
        );

    if ((result != ESP_OK) ||
        (scan_info.state !=
         WIFI_SERVICE_SCAN_STATE_COMPLETE) ||
        (scan_info.result_count == 0U)) {

        return;
    }

    size_t capacity =
        scan_info.result_count;

    if (capacity >
        WIFI_SERVICE_SCAN_MAX_RESULT_COUNT) {

        capacity =
            WIFI_SERVICE_SCAN_MAX_RESULT_COUNT;
    }

    const size_t selected_index =
        (size_t)selected_row - 1U;

    if (selected_index >= capacity) {
        return;
    }

    wifi_service_scan_result_t *results =
        heap_caps_calloc(
            capacity,
            sizeof(*results),
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

    if (results == NULL) {
        if (s_wifi_scan_status_label != NULL) {
            lv_label_set_text(
                s_wifi_scan_status_label,
                "Not enough PSRAM"
            );
        }

        return;
    }

    size_t result_count = 0U;

    result =
        wifi_service_get_scan_results(
            results,
            capacity,
            &result_count
        );

    if ((result != ESP_OK) ||
        (selected_index >= result_count)) {

        heap_caps_free(
            results
        );

        return;
    }

    s_selected_wifi_network =
        results[selected_index];

    s_wifi_network_selected = true;

    heap_caps_free(
        results
    );

    if (s_selected_wifi_network.ssid[0] == '\0') {
        memset(
            &s_selected_wifi_network,
            0,
            sizeof(s_selected_wifi_network)
        );

        s_wifi_network_selected = false;

        if (s_wifi_scan_status_label != NULL) {
            lv_label_set_text(
                s_wifi_scan_status_label,
                "Hidden networks require manual configuration"
            );
        }

        return;
    }

    if (s_wifi_scan_status_label != NULL) {
        lv_label_set_text_fmt(
            s_wifi_scan_status_label,
            "Selected: %s (%d dBm)",
            s_selected_wifi_network.ssid,
            (int)s_selected_wifi_network.rssi
        );
    }

    ESP_LOGI(
        TAG,
        "Selected Wi-Fi network: SSID=%s, channel=%u",
        s_selected_wifi_network.ssid,
        (unsigned int)
            s_selected_wifi_network.channel
    );

    if (s_root == NULL) {
        return;
    }

    const bool dialog_opened =
        wifi_credentials_dialog_open(
            s_root,
            s_selected_wifi_network.ssid,
            s_selected_wifi_network.password_required,
            wifi_credentials_submit_cb,
            NULL
        );

    if (!dialog_opened) {
        ESP_LOGE(
            TAG,
            "Failed to open Wi-Fi credentials dialog"
        );

        if (s_wifi_scan_status_label != NULL) {
            lv_label_set_text(
                s_wifi_scan_status_label,
                "Failed to open connection dialog"
            );
        }
    }
}

static void wifi_sta_enabled_switch_event_cb(
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
        settings_service_set_wifi_sta_enabled(
            enabled
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to change Wi-Fi Station state: %s",
            esp_err_to_name(result)
        );

        if (s_wifi_sta_info_label != NULL) {
            lv_label_set_text_fmt(
                s_wifi_sta_info_label,
                "Failed to change Station state: %s",
                esp_err_to_name(result)
            );
        }

        /*
         * Restore the switch from the settings model.
         */
        (void)settings_screen_refresh();

        return;
    }

    (void)settings_screen_refresh();
}

static void wifi_scan_button_event_cb(
    lv_event_t *event
)
{
    (void)event;

    if (s_wifi_scan_button == NULL) {
        return;
    }

    memset(
        &s_selected_wifi_network,
        0,
        sizeof(s_selected_wifi_network)
    );

    s_wifi_network_selected = false;

    const esp_err_t result =
        wifi_service_start_scan();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to start Wi-Fi scan: %s",
            esp_err_to_name(result)
        );

        if (s_wifi_scan_status_label != NULL) {
            lv_label_set_text_fmt(
                s_wifi_scan_status_label,
                "Scan failed: %s",
                esp_err_to_name(result)
            );
        }

        return;
    }

    s_wifi_scan_results_displayed = false;

    lv_obj_add_state(
        s_wifi_scan_button,
        LV_STATE_DISABLED
    );

    if (s_wifi_scan_status_label != NULL) {
        lv_label_set_text(
            s_wifi_scan_status_label,
            "Scanning..."
        );
    }

    if (s_wifi_scan_table != NULL) {
        lv_table_set_row_count(
            s_wifi_scan_table,
            1U
        );

        lv_table_set_cell_value(
            s_wifi_scan_table,
            0U,
            0U,
            "Network"
        );

        lv_table_set_cell_value(
            s_wifi_scan_table,
            0U,
            1U,
            "RSSI"
        );

        lv_table_set_cell_value(
            s_wifi_scan_table,
            0U,
            2U,
            "Ch"
        );
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
    if (card == NULL) {
        return;
    }

    lv_obj_add_style(
        card,
        gui_styles_card(),
        LV_PART_MAIN
    );

    const gui_theme_t *theme =
        gui_theme_get();

    if (theme == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(
        card,
        theme->settings.card_background,
        LV_PART_MAIN
    );

    lv_obj_set_style_border_color(
        card,
        theme->settings.card_border,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        card,
        CARD_PADDING,
        LV_PART_MAIN
    );

    lv_obj_remove_flag(
        card,
        LV_OBJ_FLAG_SCROLLABLE
    );
}

static void settings_screen_style_switch(
    lv_obj_t *switch_obj
)
{
    if (switch_obj == NULL) {
        return;
    }

    const gui_theme_t *theme =
        gui_theme_get();

    if (theme == NULL) {
        return;
    }

    lv_obj_set_size(
        switch_obj,
        SETTINGS_SWITCH_WIDTH,
        SETTINGS_SWITCH_HEIGHT
    );

    lv_obj_set_style_bg_color(
        switch_obj,
        theme->settings.control_track,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        switch_obj,
        theme->colors.control_accent,
        LV_PART_INDICATOR |
        LV_STATE_CHECKED
    );

    lv_obj_set_style_bg_color(
        switch_obj,
        theme->colors.text_on_primary,
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

    const gui_theme_t *theme =
        gui_theme_get();

    if (theme == NULL) {
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
        theme->colors.background,
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

static lv_obj_t *settings_screen_create_restart_button(
    lv_obj_t *parent
)
{
    if (parent == NULL) {
        return NULL;
    }

    lv_obj_t *button =
        lv_button_create(
            parent
        );

    if (button == NULL) {
        return NULL;
    }

    gui_feedback_attach(
        button
    );

    lv_obj_set_width(
        button,
        LV_PCT(100)
    );

    lv_obj_set_height(
        button,
        GUI_THEME_CONTROL_HEIGHT
    );

    lv_obj_add_style(
        button,
        gui_styles_button_base(),
        LV_PART_MAIN
    );

    lv_obj_add_style(
        button,
        gui_styles_button_danger(),
        LV_PART_MAIN |
        LV_STATE_DEFAULT
    );

    lv_obj_add_style(
        button,
        gui_styles_button_disabled(),
        LV_PART_MAIN |
        LV_STATE_DISABLED
    );

    lv_obj_add_event_cb(
        button,
        settings_screen_restart_event_cb,
        LV_EVENT_CLICKED,
        NULL
    );

    lv_obj_t *label =
        lv_label_create(
            button
        );

    if (label == NULL) {
        lv_obj_delete(button);
        return NULL;
    }

    lv_label_set_text(
        label,
        "Restart device"
    );

    lv_obj_center(label);

    return button;
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

    lv_obj_add_style(
        label,
        gui_styles_text_small(),
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

    gui_feedback_attach(
        switch_obj
    );

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

static esp_err_t settings_screen_create_buzzer_volume_card(
    lv_obj_t *tab
)
{
    if (tab == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const gui_theme_t *theme =
        gui_theme_get();

    if (theme == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    lv_obj_t *card =
        lv_obj_create(
            tab
        );

    if (card == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_size(
        card,
        LV_PCT(100),
        BRIGHTNESS_CARD_HEIGHT
    );

    settings_screen_style_card(
        card
    );

    lv_obj_t *label =
        lv_label_create(
            card
        );

    if (label == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_add_style(
        label,
        gui_styles_text_body(),
        LV_PART_MAIN
    );

    lv_label_set_text(
        label,
        "Sound volume"
    );

    lv_obj_align(
        label,
        LV_ALIGN_TOP_LEFT,
        0,
        0
    );

    s_buzzer_volume_value_label =
        lv_label_create(
            card
        );

    if (s_buzzer_volume_value_label == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_add_style(
        s_buzzer_volume_value_label,
        gui_styles_text_body(),
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        s_buzzer_volume_value_label,
        theme->colors.control_accent,
        LV_PART_MAIN
    );

    lv_label_set_text(
        s_buzzer_volume_value_label,
        "0%"
    );

    lv_obj_align(
        s_buzzer_volume_value_label,
        LV_ALIGN_TOP_RIGHT,
        0,
        0
    );

    s_buzzer_volume_slider =
        lv_slider_create(
            card
        );

    if (s_buzzer_volume_slider == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_size(
        s_buzzer_volume_slider,
        LV_PCT(100),
        12
    );

    lv_obj_align(
        s_buzzer_volume_slider,
        LV_ALIGN_BOTTOM_MID,
        0,
        0
    );

    lv_slider_set_range(
        s_buzzer_volume_slider,
        SETTINGS_SOUND_VOLUME_MIN,
        SETTINGS_SOUND_VOLUME_MAX
    );

    lv_slider_set_value(
        s_buzzer_volume_slider,
        SETTINGS_SOUND_VOLUME_DEFAULT,
        LV_ANIM_OFF
    );

    lv_obj_set_style_bg_color(
        s_buzzer_volume_slider,
        theme->settings.control_track,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        s_buzzer_volume_slider,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        s_buzzer_volume_slider,
        theme->colors.control_accent,
        LV_PART_INDICATOR
    );

    lv_obj_set_style_bg_opa(
        s_buzzer_volume_slider,
        LV_OPA_COVER,
        LV_PART_INDICATOR
    );

    lv_obj_set_style_bg_color(
        s_buzzer_volume_slider,
        theme->colors.control_accent,
        LV_PART_KNOB
    );

    lv_obj_set_style_bg_opa(
        s_buzzer_volume_slider,
        LV_OPA_COVER,
        LV_PART_KNOB
    );

    lv_obj_set_style_pad_all(
        s_buzzer_volume_slider,
        5,
        LV_PART_KNOB
    );

    lv_obj_add_event_cb(
        s_buzzer_volume_slider,
        buzzer_volume_slider_event_cb,
        LV_EVENT_VALUE_CHANGED,
        NULL
    );

    lv_obj_add_event_cb(
        s_buzzer_volume_slider,
        buzzer_volume_released_event_cb,
        LV_EVENT_RELEASED,
        NULL
    );

    return ESP_OK;
}

static esp_err_t settings_screen_create_theme_card(
    lv_obj_t *parent
)
{
    if (parent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const gui_theme_t *theme =
        gui_theme_get();

    if (theme == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    lv_obj_t *card =
        lv_obj_create(
            parent
        );

    if (card == NULL) {
        return ESP_ERR_NO_MEM;
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
        lv_label_create(
            card
        );

    if (label == NULL) {
        lv_obj_delete(card);
        return ESP_ERR_NO_MEM;
    }

    lv_label_set_text(
        label,
        "Color theme"
    );

    lv_obj_add_style(
        label,
        gui_styles_text_small(),
        LV_PART_MAIN
    );

    lv_obj_align(
        label,
        LV_ALIGN_LEFT_MID,
        0,
        0
    );

    s_restart_required_label =
        lv_label_create(
            card
        );

    if (s_restart_required_label == NULL) {
        lv_obj_delete(card);
        return ESP_ERR_NO_MEM;
    }

    lv_label_set_text(
        s_restart_required_label,
        "Restart required"
    );

    lv_obj_add_style(
        s_restart_required_label,
        gui_styles_text_small(),
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        s_restart_required_label,
        theme->colors.warning,
        LV_PART_MAIN
    );

    lv_obj_align(
        s_restart_required_label,
        LV_ALIGN_BOTTOM_LEFT,
        0,
        0
    );

    lv_obj_add_flag(
        s_restart_required_label,
        LV_OBJ_FLAG_HIDDEN
    );

    s_theme_dropdown =
        lv_dropdown_create(
            card
        );

    if (s_theme_dropdown == NULL) {
        lv_obj_delete(card);
        return ESP_ERR_NO_MEM;
    }

    gui_feedback_attach(
        s_theme_dropdown
    );

    lv_dropdown_set_options(
        s_theme_dropdown,
        "Light\nDark"
    );

    lv_dropdown_set_symbol(
        s_theme_dropdown,
        LV_SYMBOL_DOWN
    );

    lv_obj_set_size(
        s_theme_dropdown,
        120,
        GUI_THEME_CONTROL_HEIGHT
    );

    lv_obj_add_style(
        s_theme_dropdown,
        gui_styles_input(),
        LV_PART_MAIN
    );

    lv_obj_add_style(
        s_theme_dropdown,
        gui_styles_input_focused(),
        LV_PART_MAIN |
        LV_STATE_FOCUSED
    );

    lv_obj_align(
        s_theme_dropdown,
        LV_ALIGN_RIGHT_MID,
        0,
        0
    );

    lv_obj_add_event_cb(
        s_theme_dropdown,
        theme_dropdown_event_cb,
        LV_EVENT_VALUE_CHANGED,
        NULL
    );

    return ESP_OK;
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

    const gui_theme_t *theme =
        gui_theme_get();

    if (theme == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    lv_obj_t *brightness_label =
        lv_label_create(
            brightness_card
        );

    if (brightness_label == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_add_style(
        brightness_label,
        gui_styles_text_body(),
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        brightness_label,
        theme->colors.text,
        LV_PART_MAIN
    );

    lv_label_set_text(
        brightness_label,
        "Display brightness"
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

    lv_obj_add_style(
        s_brightness_value_label,
        gui_styles_text_body(),
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        s_brightness_value_label,
        theme->colors.control_accent,
        LV_PART_MAIN
    );

    lv_label_set_text(
        s_brightness_value_label,
        "0%"
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
        0
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
        theme->settings.control_track,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        s_brightness_slider,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        s_brightness_slider,
        theme->colors.control_accent,
        LV_PART_INDICATOR
    );

    lv_obj_set_style_bg_opa(
        s_brightness_slider,
        LV_OPA_COVER,
        LV_PART_INDICATOR
    );

    lv_obj_set_style_bg_color(
        s_brightness_slider,
        theme->colors.control_accent,
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

    s_sound_enabled_switch =
        settings_screen_create_switch_card(
            tab,
            "Enable sounds",
            sound_enabled_switch_event_cb
        );

    if (s_sound_enabled_switch == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t volume_result =
        settings_screen_create_buzzer_volume_card(
            tab
        );

    if (volume_result != ESP_OK) {
        return volume_result;
    }

    s_animations_switch =
        settings_screen_create_switch_card(
            tab,
            "Enable animations",
            animations_switch_event_cb
        );

    if (s_animations_switch == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t theme_result =
        settings_screen_create_theme_card(
            tab
        );

    if (theme_result != ESP_OK) {
        return theme_result;
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

    lv_obj_add_style(
        s_wifi_info_label,
        gui_styles_text_small(),
        LV_PART_MAIN
    );

    lv_obj_set_style_text_line_space(
        s_wifi_info_label,
        6,
        LV_PART_MAIN
    );

    s_wifi_sta_enabled_switch =
        settings_screen_create_switch_card(
            tab,
            "Wi-Fi Station",
            wifi_sta_enabled_switch_event_cb
        );

    if (s_wifi_sta_enabled_switch == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_t *sta_info_card =
        lv_obj_create(tab);

    if (sta_info_card == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_width(
        sta_info_card,
        LV_PCT(100)
    );

    lv_obj_set_height(
        sta_info_card,
        LV_SIZE_CONTENT
    );

    lv_obj_set_flex_grow(
        sta_info_card,
        0
    );

    settings_screen_style_card(
        sta_info_card
    );

    lv_obj_remove_flag(
        sta_info_card,
        LV_OBJ_FLAG_SCROLLABLE
    );

    s_wifi_sta_info_label =
        lv_label_create(
            sta_info_card
        );

    if (s_wifi_sta_info_label == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_width(
        s_wifi_sta_info_label,
        LV_PCT(100)
    );

    lv_label_set_long_mode(
        s_wifi_sta_info_label,
        LV_LABEL_LONG_WRAP
    );

    lv_label_set_text(
        s_wifi_sta_info_label,
        "State: Loading..."
    );

    lv_obj_add_style(
        s_wifi_sta_info_label,
        gui_styles_text_small(),
        LV_PART_MAIN
    );

    lv_obj_set_style_text_line_space(
        s_wifi_sta_info_label,
        6,
        LV_PART_MAIN
    );

    lv_obj_t *scan_card =
        lv_obj_create(tab);

    if (scan_card == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_width(
        scan_card,
        LV_PCT(100)
    );

    lv_obj_set_height(
        scan_card,
        230
    );

    settings_screen_style_card(
        scan_card
    );

    lv_obj_set_style_pad_left(
        scan_card,
        8,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_right(
        scan_card,
        8,
        LV_PART_MAIN
    );

    lv_obj_set_flex_flow(
        scan_card,
        LV_FLEX_FLOW_COLUMN
    );

    lv_obj_set_style_pad_row(
        scan_card,
        4,
        LV_PART_MAIN
    );

    lv_obj_t *scan_header =
        lv_obj_create(scan_card);

    if (scan_header == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_size(
        scan_header,
        LV_PCT(100),
        LV_SIZE_CONTENT
    );

    lv_obj_set_style_bg_opa(
        scan_header,
        LV_OPA_TRANSP,
        LV_PART_MAIN
    );

    lv_obj_set_style_border_width(
        scan_header,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        scan_header,
        0,
        LV_PART_MAIN
    );

    lv_obj_remove_flag(
        scan_header,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_flex_flow(
        scan_header,
        LV_FLEX_FLOW_ROW
    );

    lv_obj_set_flex_align(
        scan_header,
        LV_FLEX_ALIGN_SPACE_BETWEEN,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    s_wifi_scan_status_label =
        lv_label_create(scan_header);

    if (s_wifi_scan_status_label == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_label_set_text(
        s_wifi_scan_status_label,
        "Press Scan to find networks"
    );

    lv_obj_add_style(
        s_wifi_scan_status_label,
        gui_styles_text_muted(),
        LV_PART_MAIN
    );

    lv_obj_set_flex_grow(
        s_wifi_scan_status_label,
        1
    );

    lv_label_set_long_mode(
        s_wifi_scan_status_label,
        LV_LABEL_LONG_DOT
    );

    s_wifi_scan_button =
        lv_button_create(scan_header);

    if (s_wifi_scan_button == NULL) {
        return ESP_ERR_NO_MEM;
    }

    gui_feedback_attach(
        s_wifi_scan_button
    );

    lv_obj_add_style(
        s_wifi_scan_button,
        gui_styles_button_base(),
        LV_PART_MAIN
    );

    lv_obj_add_style(
        s_wifi_scan_button,
        gui_styles_button_primary(),
        LV_PART_MAIN
    );

    lv_obj_add_style(
        s_wifi_scan_button,
        gui_styles_button_primary_pressed(),
        LV_PART_MAIN | LV_STATE_PRESSED
    );

    lv_obj_add_style(
        s_wifi_scan_button,
        gui_styles_button_disabled(),
        LV_PART_MAIN | LV_STATE_DISABLED
    );

    lv_obj_add_event_cb(
        s_wifi_scan_button,
        wifi_scan_button_event_cb,
        LV_EVENT_CLICKED,
        NULL
    );

    lv_obj_set_size(
        s_wifi_scan_button,
        68,
        GUI_THEME_CONTROL_HEIGHT
    );

    lv_obj_t *button_label =
        lv_label_create(
            s_wifi_scan_button
        );

    if (button_label == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_add_style(
        button_label,
        gui_styles_text_small(),
        LV_PART_MAIN
    );

    const gui_theme_t *theme =
        gui_theme_get();

    if (theme == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    lv_obj_set_style_text_color(
        button_label,
        theme->colors.text_on_primary,
        LV_PART_MAIN
    );

    lv_label_set_text(
        button_label,
        "Scan"
    );

    lv_obj_center(
        button_label
    );

    s_wifi_scan_table =
        lv_table_create(scan_card);

    if (s_wifi_scan_table == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_style_bg_color(
        s_wifi_scan_table,
        theme->colors.input,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        s_wifi_scan_table,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_set_style_border_color(
        s_wifi_scan_table,
        theme->settings.card_border,
        LV_PART_MAIN
    );

    lv_obj_set_style_border_width(
        s_wifi_scan_table,
        GUI_THEME_BORDER_WIDTH,
        LV_PART_MAIN
    );

    lv_obj_set_style_radius(
        s_wifi_scan_table,
        GUI_THEME_RADIUS_SM,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        s_wifi_scan_table,
        theme->colors.input,
        LV_PART_ITEMS
    );

    lv_obj_set_style_text_color(
        s_wifi_scan_table,
        theme->colors.text_secondary,
        LV_PART_ITEMS
    );

    lv_obj_set_style_text_font(
        s_wifi_scan_table,
        theme->fonts.small,
        LV_PART_ITEMS
    );

    lv_obj_set_style_border_color(
        s_wifi_scan_table,
        theme->settings.card_border,
        LV_PART_ITEMS
    );

    lv_obj_set_style_border_width(
        s_wifi_scan_table,
        GUI_THEME_BORDER_WIDTH,
        LV_PART_ITEMS
    );

    lv_obj_set_style_bg_color(
        s_wifi_scan_table,
        theme->colors.surface_hover,
        LV_PART_ITEMS | LV_STATE_PRESSED
    );

    lv_obj_add_event_cb(
        s_wifi_scan_table,
        wifi_scan_table_event_cb,
        LV_EVENT_VALUE_CHANGED,
        NULL
    );

    lv_obj_set_style_pad_left(
        s_wifi_scan_table,
        GUI_THEME_SPACE_XS,
        LV_PART_ITEMS
    );

    lv_obj_set_style_pad_right(
        s_wifi_scan_table,
        GUI_THEME_SPACE_XS,
        LV_PART_ITEMS
    );

    lv_obj_set_style_pad_top(
        s_wifi_scan_table,
        5,
        LV_PART_ITEMS
    );

    lv_obj_set_style_pad_bottom(
        s_wifi_scan_table,
        5,
        LV_PART_ITEMS
    );

    lv_obj_set_width(
        s_wifi_scan_table,
        LV_PCT(100)
    );

    lv_obj_set_flex_grow(
        s_wifi_scan_table,
        1
    );

    lv_table_set_column_count(
        s_wifi_scan_table,
        3U
    );

    lv_table_set_row_count(
        s_wifi_scan_table,
        1U
    );

    lv_obj_update_layout(
        s_wifi_scan_table
    );

    const int32_t table_content_width =
        lv_obj_get_content_width(
            s_wifi_scan_table
        );

    int32_t network_column_width =
        table_content_width -
        WIFI_SCAN_RSSI_COLUMN_WIDTH -
        WIFI_SCAN_CHANNEL_COLUMN_WIDTH;

    if (network_column_width <
        WIFI_SCAN_NETWORK_MIN_WIDTH) {

        network_column_width =
            WIFI_SCAN_NETWORK_MIN_WIDTH;
    }

    lv_table_set_column_width(
        s_wifi_scan_table,
        0U,
        network_column_width
    );

    lv_table_set_column_width(
        s_wifi_scan_table,
        1U,
        WIFI_SCAN_RSSI_COLUMN_WIDTH
    );

    lv_table_set_column_width(
        s_wifi_scan_table,
        2U,
        WIFI_SCAN_CHANNEL_COLUMN_WIDTH
    );

    lv_table_set_cell_value(
        s_wifi_scan_table,
        0U,
        0U,
        "Network"
    );

    lv_table_set_cell_value(
        s_wifi_scan_table,
        0U,
        1U,
        "RSSI"
    );

    lv_table_set_cell_value(
        s_wifi_scan_table,
        0U,
        2U,
        "Ch"
    );

    return ESP_OK;
}

static void settings_screen_set_disabled_state(
    lv_obj_t *object,
    bool disabled
)
{
    if (object == NULL) {
        return;
    }

    const bool currently_disabled =
        lv_obj_has_state(
            object,
            LV_STATE_DISABLED
        );

    if (currently_disabled == disabled) {
        return;
    }

    if (disabled) {
        lv_obj_add_state(
            object,
            LV_STATE_DISABLED
        );
    } else {
        lv_obj_remove_state(
            object,
            LV_STATE_DISABLED
        );
    }
}

static lv_obj_t *settings_screen_create_dropdown_card(
    lv_obj_t *parent,
    const char *title,
    const char *options,
    lv_event_cb_t event_callback
)
{
    if ((parent == NULL) ||
        (title == NULL) ||
        (options == NULL)) {

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
        title
    );

    lv_obj_add_style(
        label,
        gui_styles_text_small(),
        LV_PART_MAIN
    );

    lv_obj_align(
        label,
        LV_ALIGN_LEFT_MID,
        0,
        0
    );

    lv_obj_t *dropdown =
        lv_dropdown_create(card);

    if (dropdown == NULL) {
        lv_obj_delete(card);
        return NULL;
    }

    /*
     * Dropdown availability is controlled programmatically during periodic
     * settings refreshes. Disable theme transitions to avoid creating
     * unnecessary animations when the disabled state changes.
     */
    lv_obj_set_style_transition(
        dropdown,
        NULL,
        LV_PART_MAIN
    );

    lv_dropdown_set_options(
        dropdown,
        options
    );

    lv_obj_set_width(
        dropdown,
        150
    );

    lv_obj_add_style(
        dropdown,
        gui_styles_input(),
        LV_PART_MAIN
    );

    lv_obj_align(
        dropdown,
        LV_ALIGN_RIGHT_MID,
        0,
        0
    );

    gui_feedback_attach(
        dropdown
    );

    if (event_callback != NULL) {
        lv_obj_add_event_cb(
            dropdown,
            event_callback,
            LV_EVENT_VALUE_CHANGED,
            NULL
        );
    }

    return dropdown;
}

static esp_err_t settings_screen_create_can_tab(
    lv_obj_t *tab
)
{
    if (tab == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    settings_screen_style_tab(
        tab
    );

    s_can_enabled_switch =
        settings_screen_create_switch_card(
            tab,
            "Primary CAN",
            can_settings_event_cb
        );

    if (s_can_enabled_switch == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_t *bitrate_card =
        lv_obj_create(tab);

    if (bitrate_card == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_size(
        bitrate_card,
        LV_PCT(100),
        SETTINGS_ROW_CARD_HEIGHT
    );

    settings_screen_style_card(
        bitrate_card
    );

    lv_obj_t *bitrate_label =
        lv_label_create(
            bitrate_card
        );

    if (bitrate_label == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_label_set_text(
        bitrate_label,
        "Bitrate"
    );

    lv_obj_add_style(
        bitrate_label,
        gui_styles_text_small(),
        LV_PART_MAIN
    );

    lv_obj_align(
        bitrate_label,
        LV_ALIGN_LEFT_MID,
        0,
        0
    );

    s_can_bitrate_dropdown =
        lv_dropdown_create(
            bitrate_card
        );

    if (s_can_bitrate_dropdown == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_dropdown_set_options(
        s_can_bitrate_dropdown,
        SETTINGS_CAN_NOMINAL_BITRATE_OPTIONS
    );

    lv_obj_set_width(
        s_can_bitrate_dropdown,
        150
    );

    lv_obj_add_style(
        s_can_bitrate_dropdown,
        gui_styles_input(),
        LV_PART_MAIN
    );

    lv_obj_align(
        s_can_bitrate_dropdown,
        LV_ALIGN_RIGHT_MID,
        0,
        0
    );

    gui_feedback_attach(
        s_can_bitrate_dropdown
    );

    lv_obj_add_event_cb(
        s_can_bitrate_dropdown,
        can_settings_event_cb,
        LV_EVENT_VALUE_CHANGED,
        NULL
    );

    s_can_listen_only_switch =
        settings_screen_create_switch_card(
            tab,
            "Safe listen-only mode",
            can_settings_event_cb
        );

    if (s_can_listen_only_switch == NULL) {
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

    s_can_info_label =
        lv_label_create(
            info_card
        );

    if (s_can_info_label == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_width(
        s_can_info_label,
        LV_PCT(100)
    );

    lv_label_set_text(
        s_can_info_label,
        "State: Loading..."
    );

    lv_obj_add_style(
        s_can_info_label,
        gui_styles_text_small(),
        LV_PART_MAIN
    );

    s_can_secondary_enabled_switch =
        settings_screen_create_switch_card(
            tab,
            "Secondary CAN (MCP2518FD)",
            can_secondary_settings_event_cb
        );

    if (s_can_secondary_enabled_switch == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_can_secondary_nominal_dropdown =
        settings_screen_create_dropdown_card(
            tab,
            "Nominal bitrate",
            SETTINGS_CAN_NOMINAL_BITRATE_OPTIONS,
            can_secondary_settings_event_cb
        );

    if (s_can_secondary_nominal_dropdown == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_can_secondary_data_dropdown =
        settings_screen_create_dropdown_card(
            tab,
            "Data bitrate",
            SETTINGS_CAN_FD_DATA_BITRATE_OPTIONS,
            can_secondary_settings_event_cb
        );

    if (s_can_secondary_data_dropdown == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_can_secondary_fd_switch =
        settings_screen_create_switch_card(
            tab,
            "CAN FD frames",
            can_secondary_settings_event_cb
        );

    if (s_can_secondary_fd_switch == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_can_secondary_brs_switch =
        settings_screen_create_switch_card(
            tab,
            "Bit rate switching",
            can_secondary_settings_event_cb
        );

    if (s_can_secondary_brs_switch == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_can_secondary_listen_only_switch =
        settings_screen_create_switch_card(
            tab,
            "Secondary listen-only mode",
            can_secondary_settings_event_cb
        );

    if (s_can_secondary_listen_only_switch == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_t *secondary_info_card =
        lv_obj_create(tab);

    if (secondary_info_card == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_width(
        secondary_info_card,
        LV_PCT(100)
    );

    lv_obj_set_height(
        secondary_info_card,
        LV_SIZE_CONTENT
    );

    settings_screen_style_card(
        secondary_info_card
    );

    lv_obj_remove_flag(
        secondary_info_card,
        LV_OBJ_FLAG_SCROLLABLE
    );

    s_can_secondary_info_label =
        lv_label_create(
            secondary_info_card
        );

    if (s_can_secondary_info_label == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_width(
        s_can_secondary_info_label,
        LV_PCT(100)
    );

    lv_label_set_long_mode(
        s_can_secondary_info_label,
        LV_LABEL_LONG_WRAP
    );

    lv_label_set_text(
        s_can_secondary_info_label,
        "State: Loading..."
    );

    lv_obj_add_style(
        s_can_secondary_info_label,
        gui_styles_text_small(),
        LV_PART_MAIN
    );

    lv_obj_set_style_text_line_space(
        s_can_secondary_info_label,
        4,
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

    lv_obj_add_style(
        s_usb_rndis_info_label,
        gui_styles_text_small(),
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

    s_storage_info_label =
        lv_label_create(
            info_card
        );

    if (s_storage_info_label == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_width(
        s_storage_info_label,
        LV_PCT(100)
    );

    lv_label_set_long_mode(
        s_storage_info_label,
        LV_LABEL_LONG_WRAP
    );

    lv_label_set_text(
        s_storage_info_label,
        "Internal storage: Loading...\n"
        "SD card: Loading..."
    );

    lv_obj_add_style(
        s_storage_info_label,
        gui_styles_text_small(),
        LV_PART_MAIN
    );

    lv_obj_set_style_text_line_space(
        s_storage_info_label,
        6,
        LV_PART_MAIN
    );

    return ESP_OK;
}

static esp_err_t settings_screen_create_system_tab(
    lv_obj_t *tab
)
{
    if (tab == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    settings_screen_style_tab(
        tab
    );

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

    s_system_info_label =
        lv_label_create(
            info_card
        );

    if (s_system_info_label == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_width(
        s_system_info_label,
        LV_PCT(100)
    );

    lv_label_set_long_mode(
        s_system_info_label,
        LV_LABEL_LONG_WRAP
    );

    lv_label_set_text(
        s_system_info_label,
        "Loading system information..."
    );

    lv_obj_add_style(
        s_system_info_label,
        gui_styles_text_small(),
        LV_PART_MAIN
    );

    lv_obj_set_style_text_line_space(
        s_system_info_label,
        6,
        LV_PART_MAIN
    );

    lv_obj_t *restart_button =
        settings_screen_create_restart_button(
            tab
        );

    if (restart_button == NULL) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t settings_screen_create_tabs(
    lv_obj_t *screen
)
{
    if (screen == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const gui_theme_t *theme =
        gui_theme_get();

    if (theme == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_tabview =
        lv_tabview_create(
            screen
        );

    if (s_tabview == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_style_bg_color(
        s_tabview,
        theme->colors.background,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        s_tabview,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_set_style_border_width(
        s_tabview,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_radius(
        s_tabview,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_size(
        s_tabview,
        LV_PCT(100),
        LCD_V_RES - SETTINGS_TOOLBAR_HEIGHT
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
        gui_feedback_attach(
            tab_bar
        );

        lv_obj_add_style(
            tab_bar,
            gui_styles_text_small(),
            LV_PART_MAIN
        );

        lv_obj_set_style_pad_all(
            tab_bar,
            GUI_THEME_SPACE_XS,
            LV_PART_MAIN
        );

        lv_obj_set_style_pad_row(
            tab_bar,
            GUI_THEME_SPACE_XS,
            LV_PART_MAIN
        );

        lv_obj_set_style_bg_color(
            tab_bar,
            theme->colors.surface,
            LV_PART_MAIN
        );

        lv_obj_set_style_bg_opa(
            tab_bar,
            LV_OPA_COVER,
            LV_PART_MAIN
        );

        lv_obj_set_style_border_width(
            tab_bar,
            0,
            LV_PART_MAIN
        );

        lv_obj_set_style_bg_opa(
            tab_bar,
            LV_OPA_TRANSP,
            LV_PART_ITEMS
        );

        lv_obj_set_style_text_color(
            tab_bar,
            theme->colors.text_muted,
            LV_PART_ITEMS
        );

        lv_obj_set_style_bg_color(
            tab_bar,
            theme->colors.control_accent,
            LV_PART_ITEMS | LV_STATE_CHECKED
        );

        lv_obj_set_style_bg_opa(
            tab_bar,
            LV_OPA_COVER,
            LV_PART_ITEMS | LV_STATE_CHECKED
        );

        lv_obj_set_style_text_color(
            tab_bar,
            theme->colors.text_on_primary,
            LV_PART_ITEMS | LV_STATE_CHECKED
        );

        lv_obj_set_style_radius(
            tab_bar,
            GUI_THEME_RADIUS_SM,
            LV_PART_ITEMS | LV_STATE_CHECKED
        );

        lv_obj_set_style_bg_color(
            tab_bar,
            theme->colors.surface_hover,
            LV_PART_ITEMS | LV_STATE_PRESSED
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

    lv_obj_t *can_tab =
        lv_tabview_add_tab(
            s_tabview,
            "CAN"
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
        (can_tab == NULL) ||
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
        settings_screen_create_can_tab(
            can_tab
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

    return settings_screen_create_system_tab(
        system_tab
    );
}

lv_obj_t *settings_screen_create(void)
{
    if (!gui_theme_is_initialized() ||
        !gui_styles_is_initialized()) {

        ESP_LOGE(
            TAG,
            "GUI theme or styles are not initialized"
        );

        return NULL;
    }

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
    s_sound_enabled_switch = NULL;
    s_buzzer_volume_slider = NULL;
    s_buzzer_volume_value_label = NULL;
    s_sd_logging_switch = NULL;
    s_storage_info_label = NULL;
    s_animations_switch = NULL;
    s_theme_dropdown = NULL;
    s_restart_required_label = NULL;
    s_wifi_enabled_switch = NULL;
    s_wifi_info_label = NULL;
    s_wifi_scan_button = NULL;
    s_wifi_scan_status_label = NULL;
    s_wifi_scan_table = NULL;
    s_wifi_scan_results_displayed = false;
    s_wifi_sta_enabled_switch = NULL;
    s_wifi_sta_info_label = NULL;
    s_can_enabled_switch = NULL;
    s_can_bitrate_dropdown = NULL;
    s_can_listen_only_switch = NULL;
    s_can_info_label = NULL;
    s_usb_rndis_enabled_switch = NULL;
    s_usb_rndis_info_label = NULL;
    s_system_info_label = NULL;
    s_updating_controls = false;
    s_can_secondary_enabled_switch = NULL;
    s_can_secondary_nominal_dropdown = NULL;
    s_can_secondary_data_dropdown = NULL;
    s_can_secondary_fd_switch = NULL;
    s_can_secondary_brs_switch = NULL;
    s_can_secondary_listen_only_switch = NULL;
    s_can_secondary_info_label = NULL;

    memset(
        &s_selected_wifi_network,
        0,
        sizeof(s_selected_wifi_network)
    );

    s_wifi_network_selected = false;

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

    lv_obj_add_style(
        screen,
        gui_styles_screen(),
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

    settings_screen_start_status_refresh();
}

void settings_screen_on_hide(
    lv_obj_t *screen
)
{
    if ((screen == NULL) ||
        (screen != s_root)) {

        return;
    }

    wifi_credentials_dialog_close();

    settings_screen_stop_status_refresh();
}

void settings_screen_destroy(
    lv_obj_t *screen
)
{
    if ((screen == NULL) ||
        (screen != s_root)) {

        return;
    }

    wifi_credentials_dialog_close();

    settings_screen_stop_status_refresh();

    s_root = NULL;
    s_tabview = NULL;
    s_brightness_slider = NULL;
    s_brightness_value_label = NULL;
    s_sound_enabled_switch = NULL;
    s_buzzer_volume_slider = NULL;
    s_buzzer_volume_value_label = NULL;
    s_sd_logging_switch = NULL;
    s_storage_info_label = NULL;
    s_animations_switch = NULL;
    s_theme_dropdown = NULL;
    s_restart_required_label = NULL;
    s_wifi_enabled_switch = NULL;
    s_wifi_info_label = NULL;
    s_wifi_scan_button = NULL;
    s_wifi_scan_status_label = NULL;
    s_wifi_scan_table = NULL;
    s_wifi_scan_results_displayed = false;
    s_wifi_sta_enabled_switch = NULL;
    s_wifi_sta_info_label = NULL;
    s_usb_rndis_enabled_switch = NULL;
    s_usb_rndis_info_label = NULL;
    s_can_enabled_switch = NULL;
    s_can_bitrate_dropdown = NULL;
    s_can_listen_only_switch = NULL;
    s_can_info_label = NULL;
    s_system_info_label = NULL;
    s_updating_controls = false;
    s_can_secondary_enabled_switch = NULL;
    s_can_secondary_nominal_dropdown = NULL;
    s_can_secondary_data_dropdown = NULL;
    s_can_secondary_fd_switch = NULL;
    s_can_secondary_brs_switch = NULL;
    s_can_secondary_listen_only_switch = NULL;
    s_can_secondary_info_label = NULL;

    memset(
        &s_selected_wifi_network,
        0,
        sizeof(s_selected_wifi_network)
    );

    s_wifi_network_selected = false;

    lv_obj_delete(
        screen
    );
}
