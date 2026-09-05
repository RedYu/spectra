/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "screens/main_screen.h"

#include <string.h>

#include "esp_log.h"

#include "system_model.h"
#include "widgets/toolbar.h"
#include "widgets/modal_dialog.h"
#include "widgets/sd_card_modal.h"
#include "assets/gui_images.h"
#include "gui_feedback.h"
#include "gui_styles.h"
#include "gui_theme.h"

#include "gui_config.h"
#include "board_config.h"
#include "screen_manager.h"
#include "usb_network_service.h"
#include "wifi_service.h"
#include "can_service.h"
#include "can_fd_service.h"
#include "can_monitor_service.h"

#define MAIN_TOOLBAR_HEIGHT \
    GUI_THEME_TOOLBAR_HEIGHT
#define MAIN_SCREEN_UPDATE_MS   (1000U)

#define MAIN_CAN_CHANNEL_COUNT  (2U)
#define MAIN_CAN_ROW_HEIGHT     (132)
#define MAIN_RECORDING_ROW_HEIGHT  (44)
#define MAIN_ACTION_ROW_HEIGHT  (44)

_Static_assert(
    MAIN_CAN_CHANNEL_COUNT == CAN_BUS_COUNT,
    "Main screen CAN channel count must match shared CAN bus count"
);

typedef struct
{
    lv_obj_t *root;

    toolbar_t toolbar;

    /*
     * Dashboard recording status.
     */
    lv_obj_t *recording_indicator;
    lv_obj_t *recording_status_label;
    lv_obj_t *recording_details_label;

    /*
     * CAN channel cards.
     */
    lv_obj_t *can_state_label[
        MAIN_CAN_CHANNEL_COUNT
    ];

    lv_obj_t *can_bitrate_label[
        MAIN_CAN_CHANNEL_COUNT
    ];

    lv_obj_t *can_mode_label[
        MAIN_CAN_CHANNEL_COUNT
    ];

    lv_obj_t *can_frames_label[
        MAIN_CAN_CHANNEL_COUNT
    ];

    lv_obj_t *can_errors_label[
        MAIN_CAN_CHANNEL_COUNT
    ];

    lv_obj_t *capture_button;
    lv_obj_t *capture_button_label;
    lv_obj_t *monitor_button;

    lv_obj_t *can_state_indicator[
        MAIN_CAN_CHANNEL_COUNT
    ];

    uint64_t can_previous_rx_count[
        MAIN_CAN_CHANNEL_COUNT
    ];

    uint64_t can_previous_tx_count[
        MAIN_CAN_CHANNEL_COUNT
    ];

    bool can_frame_counter_initialized[
        MAIN_CAN_CHANNEL_COUNT
    ];

    lv_timer_t *update_timer;

} main_screen_context_t;

static const char *TAG = "main_screen";

static main_screen_context_t s_context = {0};

static modal_dialog_t s_update_dialog = {0};

static const char *main_screen_can_fd_state_to_string(
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

static const char *main_screen_can_fd_mode_to_string(
    can_fd_mcp2518fd_mode_t mode
)
{
    switch (mode) {
        case CAN_FD_MCP2518FD_MODE_NORMAL:
            return "Normal";

        case CAN_FD_MCP2518FD_MODE_LISTEN_ONLY:
            return "Listen only";

        case CAN_FD_MCP2518FD_MODE_INTERNAL_LOOPBACK:
            return "Internal loopback";

        case CAN_FD_MCP2518FD_MODE_EXTERNAL_LOOPBACK:
            return "External loopback";

        case CAN_FD_MCP2518FD_MODE_RESTRICTED:
            return "Restricted";

        default:
            return "Unknown";
    }
}

typedef struct
{
    uint32_t rx;
    uint32_t tx;

} main_screen_can_rate_t;

static uint32_t main_screen_counter_difference(
    uint64_t current,
    uint64_t previous
)
{
    const uint64_t difference =
        current >= previous
            ? current - previous
            : current;

    return difference > UINT32_MAX
        ? UINT32_MAX
        : (uint32_t)difference;
}

static main_screen_can_rate_t main_screen_update_frame_rate(
    size_t channel,
    uint64_t received_frames,
    uint64_t transmitted_frames
)
{
    main_screen_can_rate_t rate = {0};

    if (channel >= MAIN_CAN_CHANNEL_COUNT) {
        return rate;
    }

    if (s_context.can_frame_counter_initialized[channel]) {
        rate.rx =
            main_screen_counter_difference(
                received_frames,
                s_context.can_previous_rx_count[channel]
            );

        rate.tx =
            main_screen_counter_difference(
                transmitted_frames,
                s_context.can_previous_tx_count[channel]
            );
    }

    s_context.can_previous_rx_count[channel] =
        received_frames;

    s_context.can_previous_tx_count[channel] =
        transmitted_frames;

    s_context.can_frame_counter_initialized[channel] =
        true;

    return rate;
}

static void main_screen_reset_frame_rate(
    size_t channel
)
{
    if (channel >= MAIN_CAN_CHANNEL_COUNT) {
        return;
    }

    s_context.can_previous_rx_count[channel] = 0U;
    s_context.can_previous_tx_count[channel] = 0U;
    s_context.can_frame_counter_initialized[channel] = false;
}

static void main_screen_set_can_state_indicator(
    size_t channel,
    lv_color_t color
)
{
    if ((channel >= MAIN_CAN_CHANNEL_COUNT) ||
        (s_context.can_state_indicator[channel] == NULL)) {

        return;
    }

    lv_obj_set_style_bg_color(
        s_context.can_state_indicator[channel],
        color,
        LV_PART_MAIN
    );
}

static lv_color_t main_screen_twai_state_color(
    can_twai_state_t state
)
{
    switch (state) {
        case CAN_TWAI_STATE_ERROR_ACTIVE:
            return lv_palette_main(LV_PALETTE_GREEN);

        case CAN_TWAI_STATE_ERROR_WARNING:
            return lv_palette_main(LV_PALETTE_AMBER);

        case CAN_TWAI_STATE_ERROR_PASSIVE:
            return lv_palette_main(LV_PALETTE_ORANGE);

        case CAN_TWAI_STATE_BUS_OFF:
            return lv_palette_main(LV_PALETTE_RED);

        case CAN_TWAI_STATE_RECOVERING:
            return lv_palette_main(LV_PALETTE_BLUE);

        case CAN_TWAI_STATE_STOPPED:
        case CAN_TWAI_STATE_UNKNOWN:
        default:
            return lv_palette_main(LV_PALETTE_GREY);
    }
}

static lv_color_t main_screen_can_fd_state_color(
    can_fd_mcp2518fd_state_t state
)
{
    switch (state) {
        case CAN_FD_MCP2518FD_STATE_ERROR_ACTIVE:
            return lv_palette_main(LV_PALETTE_GREEN);

        case CAN_FD_MCP2518FD_STATE_ERROR_WARNING:
            return lv_palette_main(LV_PALETTE_AMBER);

        case CAN_FD_MCP2518FD_STATE_ERROR_PASSIVE:
            return lv_palette_main(LV_PALETTE_ORANGE);

        case CAN_FD_MCP2518FD_STATE_BUS_OFF:
            return lv_palette_main(LV_PALETTE_RED);

        case CAN_FD_MCP2518FD_STATE_STOPPED:
        case CAN_FD_MCP2518FD_STATE_UNKNOWN:
        default:
            return lv_palette_main(LV_PALETTE_GREY);
    }
}

static void main_screen_capture_button_event_cb(
    lv_event_t *event
)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    ESP_LOGI(
        TAG,
        "Capture control is not implemented yet"
    );
}

static void main_screen_monitor_button_event_cb(
    lv_event_t *event
)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    const bool animations_enabled =
        gui_config_get_animations_enabled();

    const esp_err_t result =
        screen_manager_show(
            SCREEN_ID_CAN_MONITOR,
            animations_enabled
                ? LV_SCR_LOAD_ANIM_MOVE_LEFT
                : LV_SCR_LOAD_ANIM_NONE,
            animations_enabled
                ? 200U
                : 0U
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to show CAN monitor screen: %s",
            esp_err_to_name(result)
        );
    }
}

static void main_screen_settings_action(void)
{
    const bool animations_enabled =
        gui_config_get_animations_enabled();

    const esp_err_t result =
        screen_manager_show(
            SCREEN_ID_SETTINGS,
            animations_enabled
                ? LV_SCR_LOAD_ANIM_MOVE_LEFT
                : LV_SCR_LOAD_ANIM_NONE,
            animations_enabled
                ? 200U
                : 0U
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to show settings screen: %s",
            esp_err_to_name(result)
        );
    }
}

static lv_obj_t *main_screen_create_action_button(
    lv_obj_t *parent,
    const char *text,
    lv_event_cb_t event_cb,
    bool primary
)
{
    if ((parent == NULL) ||
        (text == NULL) ||
        (event_cb == NULL)) {

        return NULL;
    }

    lv_obj_t *button =
        lv_button_create(parent);

    if (button == NULL) {
        return NULL;
    }

    gui_feedback_attach(
        button
    );

    lv_obj_add_style(
        button,
        gui_styles_button_base(),
        LV_PART_MAIN
    );

    lv_obj_add_style(
        button,
        primary
            ? gui_styles_button_primary()
            : gui_styles_button_secondary(),
        LV_PART_MAIN
    );

    lv_obj_add_style(
        button,
        primary
            ? gui_styles_button_primary_pressed()
            : gui_styles_button_secondary_pressed(),
        LV_PART_MAIN | LV_STATE_PRESSED
    );

    lv_obj_add_style(
        button,
        gui_styles_button_disabled(),
        LV_PART_MAIN | LV_STATE_DISABLED
    );

    lv_obj_set_height(
        button,
        LV_PCT(100)
    );

    lv_obj_set_flex_grow(
        button,
        1
    );

    lv_obj_add_event_cb(
        button,
        event_cb,
        LV_EVENT_CLICKED,
        NULL
    );

    lv_obj_t *label =
        lv_label_create(button);

    if (label == NULL) {
        lv_obj_delete(button);
        return NULL;
    }

    lv_obj_add_style(
        label,
        gui_styles_text_small(),
        LV_PART_MAIN
    );

    const gui_theme_t *theme =
        gui_theme_get();

    if (theme == NULL) {
        lv_obj_delete(button);
        return NULL;
    }

    lv_obj_set_style_text_color(
        label,
        primary
            ? theme->colors.text_on_primary
            : theme->colors.primary,
        LV_PART_MAIN
    );

    lv_label_set_text(
        label,
        text
    );

    lv_obj_center(
        label
    );

    return button;
}

static esp_err_t main_screen_create_action_row(
    lv_obj_t *parent
)
{
    if (parent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lv_obj_t *row =
        lv_obj_create(parent);

    if (row == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_size(
        row,
        LV_PCT(100),
        MAIN_ACTION_ROW_HEIGHT
    );

    lv_obj_set_style_bg_opa(
        row,
        LV_OPA_TRANSP,
        LV_PART_MAIN
    );

    lv_obj_set_style_border_width(
        row,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        row,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_column(
        row,
        8,
        LV_PART_MAIN
    );

    lv_obj_remove_flag(
        row,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_flex_flow(
        row,
        LV_FLEX_FLOW_ROW
    );

    s_context.capture_button =
        main_screen_create_action_button(
            row,
            "Start capture",
            main_screen_capture_button_event_cb,
            true
        );

    if (s_context.capture_button == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_context.capture_button_label =
        lv_obj_get_child(
            s_context.capture_button,
            0
        );

    s_context.monitor_button =
        main_screen_create_action_button(
            row,
            "Monitor",
            main_screen_monitor_button_event_cb,
            false
        );

    if (s_context.monitor_button == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /*
     * Enable these controls after their services are implemented.
     */
    lv_obj_add_state(
        s_context.capture_button,
        LV_STATE_DISABLED
    );

    return ESP_OK;
}

static const char *main_screen_can_state_to_string(
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

static const char *main_screen_can_mode_to_string(
    can_twai_mode_t mode
)
{
    switch (mode) {
        case CAN_TWAI_MODE_NORMAL:
            return "Normal";

        case CAN_TWAI_MODE_LISTEN_ONLY:
            return "Listen only";

        case CAN_TWAI_MODE_SELF_TEST:
            return "Self test";

        default:
            return "Unknown";
    }
}

static void main_screen_update_primary_can(
    const can_monitor_service_statistics_t *statistics
)
{
    const size_t channel = (size_t)CAN_BUS_PRIMARY;

    if ((channel >= MAIN_CAN_CHANNEL_COUNT) ||
        (s_context.can_state_label[channel] == NULL) ||
        (s_context.can_bitrate_label[channel] == NULL) ||
        (s_context.can_mode_label[channel] == NULL) ||
        (s_context.can_frames_label[channel] == NULL) ||
        (s_context.can_errors_label[channel] == NULL)) {

        return;
    }

    can_twai_driver_info_t info = {0};

    if (!can_service_is_running() ||
        (can_service_get_info(&info) != ESP_OK)) {

        lv_label_set_text(
            s_context.can_state_label[channel],
            "Disabled"
        );

        lv_label_set_text(
            s_context.can_bitrate_label[channel],
            "N/A"
        );

        lv_label_set_text(
            s_context.can_mode_label[channel],
            "Not configured"
        );

        lv_label_set_text(
            s_context.can_frames_label[channel],
            "RX 0/s   TX 0/s"
        );

        lv_label_set_text(
            s_context.can_errors_label[channel],
            "Errors 0"
        );

        main_screen_set_can_state_indicator(
            channel,
            lv_palette_main(LV_PALETTE_GREY)
        );

        main_screen_reset_frame_rate(channel);
        return;
    }

    lv_label_set_text(
        s_context.can_state_label[channel],
        main_screen_can_state_to_string(
            info.state
        )
    );

    main_screen_set_can_state_indicator(
        channel,
        main_screen_twai_state_color(
            info.state
        )
    );

    lv_label_set_text_fmt(
        s_context.can_bitrate_label[channel],
        "Bitrate: %lu kbit/s",
        (unsigned long)(info.bitrate / 1000U)
    );

    lv_label_set_text_fmt(
        s_context.can_mode_label[channel],
        "Mode: %s",
        main_screen_can_mode_to_string(info.mode)
    );

    main_screen_can_rate_t rate = {0};

    if (statistics != NULL) {
        const can_monitor_bus_statistics_t *bus_statistics =
            &statistics->buses[CAN_BUS_PRIMARY];

        rate =
            main_screen_update_frame_rate(
                channel,
                bus_statistics->received_frames,
                bus_statistics->completed_transmissions
            );
    } else {
        main_screen_reset_frame_rate(channel);
    }

    lv_label_set_text_fmt(
        s_context.can_frames_label[channel],
        "RX %lu/s   TX %lu/s",
        (unsigned long)rate.rx,
        (unsigned long)rate.tx
    );

    const uint32_t error_count =
        info.dropped_rx_frames +
        info.arbitration_lost_count +
        info.bit_error_count +
        info.form_error_count +
        info.stuff_error_count +
        info.bus_error_count +
        info.acknowledgement_error_count;

    lv_label_set_text_fmt(
        s_context.can_errors_label[channel],
        "Errors: %lu",
        (unsigned long)error_count
    );
}

static void main_screen_update_secondary_can(
    const can_monitor_service_statistics_t *statistics
)
{
    const size_t channel = (size_t)CAN_BUS_SECONDARY;

    if ((channel >= MAIN_CAN_CHANNEL_COUNT) ||
        (s_context.can_state_label[channel] == NULL) ||
        (s_context.can_bitrate_label[channel] == NULL) ||
        (s_context.can_mode_label[channel] == NULL) ||
        (s_context.can_frames_label[channel] == NULL) ||
        (s_context.can_errors_label[channel] == NULL)) {

        return;
    }

    can_fd_mcp2518fd_info_t info = {0};

    if (!can_fd_service_is_running() ||
        (can_fd_service_get_info(&info) != ESP_OK)) {

        lv_label_set_text(
            s_context.can_state_label[channel],
            "Disabled"
        );

        lv_label_set_text(
            s_context.can_bitrate_label[channel],
            "N/A"
        );

        lv_label_set_text(
            s_context.can_mode_label[channel],
            "Not configured"
        );

        lv_label_set_text(
            s_context.can_frames_label[channel],
            "RX 0/s   TX 0/s"
        );

        lv_label_set_text(
            s_context.can_errors_label[channel],
            "Errors 0"
        );

        main_screen_set_can_state_indicator(
            channel,
            lv_palette_main(LV_PALETTE_GREY)
        );

        main_screen_reset_frame_rate(channel);
        return;
    }

    lv_label_set_text(
        s_context.can_state_label[channel],
        main_screen_can_fd_state_to_string(
            info.state
        )
    );

    main_screen_set_can_state_indicator(
        channel,
        main_screen_can_fd_state_color(
            info.state
        )
    );

    if (info.fd_enabled && info.brs_enabled) {
        lv_label_set_text_fmt(
            s_context.can_bitrate_label[channel],
            "Bitrate: %lu / %lu kbit/s",
            (unsigned long)(info.nominal_bitrate / 1000U),
            (unsigned long)(info.data_bitrate / 1000U)
        );
    } else if (info.fd_enabled) {
        lv_label_set_text_fmt(
            s_context.can_bitrate_label[channel],
            "Bitrate: %lu kbit/s (FD)",
            (unsigned long)(info.nominal_bitrate / 1000U)
        );
    } else {
        lv_label_set_text_fmt(
            s_context.can_bitrate_label[channel],
            "Bitrate: %lu kbit/s",
            (unsigned long)(info.nominal_bitrate / 1000U)
        );
    }

    lv_label_set_text_fmt(
        s_context.can_mode_label[channel],
        "Mode: %s",
        main_screen_can_fd_mode_to_string(info.mode)
    );

    main_screen_can_rate_t rate = {0};

    if (statistics != NULL) {
        const can_monitor_bus_statistics_t *bus_statistics =
            &statistics->buses[CAN_BUS_SECONDARY];

        rate =
            main_screen_update_frame_rate(
                channel,
                bus_statistics->received_frames,
                bus_statistics->completed_transmissions
            );
    } else {
        main_screen_reset_frame_rate(channel);
    }

    lv_label_set_text_fmt(
        s_context.can_frames_label[channel],
        "RX %lu/s   TX %lu/s",
        (unsigned long)rate.rx,
        (unsigned long)rate.tx
    );

    const uint32_t error_count =
        info.dropped_rx_frames +
        info.transmit_failures +
        info.receive_overflow_count +
        info.transmit_event_overflow_count +
        info.bus_error_count;

    lv_label_set_text_fmt(
        s_context.can_errors_label[channel],
        "Errors: %lu",
        (unsigned long)error_count
    );
}

static void main_screen_update(void)
{
    if (s_context.root == NULL) {
        return;
    }

    system_model_t model;

    const esp_err_t result =
        system_model_get_snapshot(
            &model
        );

    if (result != ESP_OK) {
        return;
    }

    toolbar_set_sd_mounted(
        &s_context.toolbar,
        model.sd_card_mounted
    );

    toolbar_set_ota_available(
        &s_context.toolbar,
        model.ota_available
    );

    toolbar_set_cpu_usage(
        &s_context.toolbar,
        model.cpu_usage
    );

    usb_network_service_info_t usb_info = {0};

    if (usb_network_service_get_info(
            &usb_info
        ) == ESP_OK) {

        toolbar_set_usb_status(
            &s_context.toolbar,
            usb_info.started,
            usb_info.host_connected
        );
    } else {
        toolbar_set_usb_status(
            &s_context.toolbar,
            false,
            false
        );
    }

    wifi_service_info_t wifi_info = {0};

    if (wifi_service_get_info(
            &wifi_info
        ) == ESP_OK) {

        toolbar_set_wifi_status(
            &s_context.toolbar,
            wifi_info.started,
            wifi_info.client_count
        );

        toolbar_set_internet_available(
            &s_context.toolbar,
            model.internet_available
        );
    } else {
        toolbar_set_wifi_status(
            &s_context.toolbar,
            false,
            0U
        );

        toolbar_set_internet_available(
            &s_context.toolbar,
            false
        );
    }

    can_monitor_service_statistics_t can_statistics = {0};

    const bool can_statistics_valid =
        can_monitor_service_is_running() &&
        (can_monitor_service_get_statistics(
            &can_statistics
        ) == ESP_OK);

    main_screen_update_primary_can(
        can_statistics_valid
            ? &can_statistics
            : NULL
    );

    main_screen_update_secondary_can(
        can_statistics_valid
            ? &can_statistics
            : NULL
    );
}

static void main_screen_update_timer_cb(
    lv_timer_t *timer
)
{
    (void)timer;

    main_screen_update();
}

static void main_screen_delete_event_cb(
    lv_event_t *event
)
{
    (void)event;

    if (s_context.update_timer != NULL) {
        lv_timer_delete(
            s_context.update_timer
        );

        s_context.update_timer = NULL;
    }

    s_context =
        (main_screen_context_t){0};

    s_update_dialog =
        (modal_dialog_t){0};
}

static void sd_button_action(void)
{
    if (!sd_card_modal_open(
            lv_screen_active()
        )) {

        ESP_LOGW(
            TAG,
            "Failed to open SD-card dialog"
        );
    }
}

static void ota_button_action(void)
{
    const modal_dialog_config_t config = {
        .title = "Software Update",
        .message = "Checking for available updates...",

        .icon = NULL,

        .primary_button_text = "Close",
        .primary_action = NULL,
        .close_on_primary_action = true,

        .secondary_button_text = NULL,
        .secondary_action = NULL,
        .close_on_secondary_action = false,

        .show_progress_bar = true,
        .initial_progress = 0U,
        .progress_text = "Connecting to server...",

        .animate_open =
            gui_config_get_animations_enabled(),
        .close_on_overlay_click = false,
    };

    if (!modal_dialog_create(
            &s_update_dialog,
            lv_screen_active(),
            &config
        )) {

        ESP_LOGW(
            TAG,
            "Failed to create software update dialog"
        );

        return;
    }

    modal_dialog_set_progress_text(
        &s_update_dialog,
        "Downloading update..."
    );

    modal_dialog_set_progress(
        &s_update_dialog,
        45U,
        true
    );

    modal_dialog_set_title(
        &s_update_dialog,
        "Update Ready"
    );

    modal_dialog_set_message(
        &s_update_dialog,
        "The update was downloaded successfully."
    );

    modal_dialog_set_progress(
        &s_update_dialog,
        100U,
        true
    );
}

static void main_screen_style_card(
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

    lv_obj_set_style_pad_all(
        card,
        GUI_THEME_SPACE_SM,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_row(
        card,
        GUI_THEME_SPACE_XS,
        LV_PART_MAIN
    );

    lv_obj_remove_flag(
        card,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_flex_flow(
        card,
        LV_FLEX_FLOW_COLUMN
    );

    lv_obj_set_flex_align(
        card,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START
    );
}

static lv_obj_t *main_screen_create_card_label(
    lv_obj_t *parent,
    const char *text,
    const lv_style_t *style
)
{
    if ((parent == NULL) ||
        (text == NULL) ||
        (style == NULL)) {

        return NULL;
    }

    lv_obj_t *label =
        lv_label_create(parent);

    if (label == NULL) {
        return NULL;
    }

    lv_label_set_text(
        label,
        text
    );

    lv_obj_add_style(
        label,
        style,
        LV_PART_MAIN
    );

    return label;
}

static esp_err_t main_screen_create_can_card(
    lv_obj_t *parent,
    size_t channel_index,
    const char *title
)
{
    if ((parent == NULL) ||
        (title == NULL) ||
        (channel_index >= MAIN_CAN_CHANNEL_COUNT)) {

        return ESP_ERR_INVALID_ARG;
    }

    lv_obj_t *card =
        lv_obj_create(parent);

    if (card == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_height(
        card,
        LV_PCT(100)
    );

    lv_obj_set_flex_grow(
        card,
        1
    );

    main_screen_style_card(
        card
    );

    lv_obj_t *header =
        lv_obj_create(card);

    if (header == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_size(
        header,
        LV_PCT(100),
        22
    );

    lv_obj_set_style_bg_opa(
        header,
        LV_OPA_TRANSP,
        LV_PART_MAIN
    );

    lv_obj_set_style_border_width(
        header,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        header,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_column(
        header,
        6,
        LV_PART_MAIN
    );

    lv_obj_remove_flag(
        header,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_flex_flow(
        header,
        LV_FLEX_FLOW_ROW
    );

    lv_obj_set_flex_align(
        header,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    lv_obj_t *title_label =
        main_screen_create_card_label(
            header,
            title,
            gui_styles_text_body()
        );

    if (title_label == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_flex_grow(
        title_label,
        1
    );

    s_context.can_state_indicator[channel_index] =
        lv_obj_create(header);

    if (s_context.can_state_indicator[channel_index] == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_size(
        s_context.can_state_indicator[channel_index],
        8,
        8
    );

    lv_obj_set_style_radius(
        s_context.can_state_indicator[channel_index],
        LV_RADIUS_CIRCLE,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        s_context.can_state_indicator[channel_index],
        lv_palette_main(LV_PALETTE_GREY),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        s_context.can_state_indicator[channel_index],
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_set_style_border_width(
        s_context.can_state_indicator[channel_index],
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        s_context.can_state_indicator[channel_index],
        0,
        LV_PART_MAIN
    );

    lv_obj_remove_flag(
        s_context.can_state_indicator[channel_index],
        LV_OBJ_FLAG_SCROLLABLE
    );

    s_context.can_state_label[channel_index] =
        main_screen_create_card_label(
            header,
            "Disabled",
            gui_styles_text_muted()
        );

    if (s_context.can_state_label[channel_index] == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const gui_theme_t *theme =
        gui_theme_get();

    if (theme == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    lv_obj_set_style_text_color(
        title_label,
        theme->colors.text,
        LV_PART_MAIN
    );

    s_context.can_bitrate_label[channel_index] =
        main_screen_create_card_label(
            card,
            "N/A",
            gui_styles_text_small()
        );

    s_context.can_mode_label[channel_index] =
        main_screen_create_card_label(
            card,
            "Classical",
            gui_styles_text_small()
        );

    s_context.can_frames_label[channel_index] =
        main_screen_create_card_label(
            card,
            "RX 0/s   TX 0/s",
            gui_styles_text_small()
        );

    s_context.can_errors_label[channel_index] =
        main_screen_create_card_label(
            card,
            "Errors 0",
            gui_styles_text_small()
        );

    if ((s_context.can_state_label[channel_index] == NULL) ||
        (s_context.can_bitrate_label[channel_index] == NULL) ||
        (s_context.can_mode_label[channel_index] == NULL) ||
        (s_context.can_frames_label[channel_index] == NULL) ||
        (s_context.can_errors_label[channel_index] == NULL)) {

        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t main_screen_create_recording_row(
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

    lv_obj_t *row =
        lv_obj_create(parent);

    if (row == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_size(
        row,
        LV_PCT(100),
        MAIN_RECORDING_ROW_HEIGHT
    );

    main_screen_style_card(
        row
    );

    lv_obj_set_style_pad_all(
        row,
        10,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_column(
        row,
        8,
        LV_PART_MAIN
    );

    lv_obj_set_flex_flow(
        row,
        LV_FLEX_FLOW_ROW
    );

    lv_obj_set_flex_align(
        row,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    s_context.recording_indicator =
        lv_obj_create(row);

    if (s_context.recording_indicator == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_size(
        s_context.recording_indicator,
        10,
        10
    );

    lv_obj_set_style_radius(
        s_context.recording_indicator,
        LV_RADIUS_CIRCLE,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        s_context.recording_indicator,
        theme->colors.text_disabled,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        s_context.recording_indicator,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_set_style_border_width(
        s_context.recording_indicator,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        s_context.recording_indicator,
        0,
        LV_PART_MAIN
    );

    lv_obj_remove_flag(
        s_context.recording_indicator,
        LV_OBJ_FLAG_SCROLLABLE
    );

    s_context.recording_status_label =
        main_screen_create_card_label(
            row,
            "Idle",
            gui_styles_text_small()
        );

    if (s_context.recording_status_label == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_context.recording_details_label =
        main_screen_create_card_label(
            row,
            "00:00:00 | 0 B | Dropped: 0",
            gui_styles_text_muted()
        );

    if (s_context.recording_details_label == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_flex_grow(
        s_context.recording_details_label,
        1
    );

    lv_obj_set_style_text_align(
        s_context.recording_details_label,
        LV_TEXT_ALIGN_RIGHT,
        LV_PART_MAIN
    );

    return ESP_OK;
}

static esp_err_t main_screen_create_can_row(
    lv_obj_t *parent
)
{
    if (parent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lv_obj_t *row =
        lv_obj_create(parent);

    if (row == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_size(
        row,
        LV_PCT(100),
        MAIN_CAN_ROW_HEIGHT
    );

    lv_obj_set_style_bg_opa(
        row,
        LV_OPA_TRANSP,
        LV_PART_MAIN
    );

    lv_obj_set_style_border_width(
        row,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        row,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_column(
        row,
        8,
        LV_PART_MAIN
    );

    lv_obj_remove_flag(
        row,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_flex_flow(
        row,
        LV_FLEX_FLOW_ROW
    );

    lv_obj_set_flex_align(
        row,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START
    );

    esp_err_t result =
        main_screen_create_can_card(
            row,
            0U,
            "Primary CAN"
        );

    if (result != ESP_OK) {
        return result;
    }

    return main_screen_create_can_card(
        row,
        1U,
        "Secondary CAN"
    );
}

lv_obj_t *main_screen_create(void)
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
            "Failed to create main screen"
        );

        return NULL;
    }

    s_context.root = screen;

    lv_obj_add_event_cb(
        screen,
        main_screen_delete_event_cb,
        LV_EVENT_DELETE,
        NULL
    );

    /*
     * The root screen itself must not scroll.
     */
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
        .title = "Spectra",

        .left_icon = NULL,
        .left_action = NULL,

        .right_icon = &icons8_settings_32,
        .right_action = main_screen_settings_action,

        .show_usb_status = true,
        .usb_action = NULL,

        .show_wifi_status = true,
        .wifi_action = NULL,

        .show_cpu_status = true,
        .cpu_action = NULL,

        .show_sd_status = true,
        .sd_action = sd_button_action,

        .show_ota_status = true,
        .ota_action = ota_button_action,

        .show_internet_status = true,
    };

    s_context.toolbar =
        toolbar_create(
            screen,
            &toolbar_config
        );

    if (s_context.toolbar.root == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create main-screen toolbar"
        );

        lv_obj_delete(
            screen
        );

        return NULL;
    }

    /*
     * Main fixed dashboard container.
     */
    lv_obj_t *content =
        lv_obj_create(screen);

    if (content == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create main-screen content"
        );

        lv_obj_delete(
            screen
        );

        return NULL;
    }

    lv_obj_set_style_bg_color(
        content,
        theme->colors.background,
        LV_PART_MAIN
    );

    lv_obj_set_size(
        content,
        LV_PCT(100),
        LCD_V_RES - MAIN_TOOLBAR_HEIGHT
    );

    lv_obj_align(
        content,
        LV_ALIGN_BOTTOM_MID,
        0,
        0
    );

    lv_obj_set_style_pad_all(
        content,
        GUI_THEME_SPACE_MD,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_row(
        content,
        GUI_THEME_SPACE_SM,
        LV_PART_MAIN
    );

    /*
     * Fixed dashboard content container.
     */
    lv_obj_remove_flag(
        content,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_scrollbar_mode(
        content,
        LV_SCROLLBAR_MODE_OFF
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

    lv_obj_set_style_bg_opa(
        content,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    /*
     * Arrange dashboard sections vertically.
     */
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

    esp_err_t dashboard_result =
        main_screen_create_recording_row(
            content
        );

    if (dashboard_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to create recording status row: %s",
            esp_err_to_name(dashboard_result)
        );

        lv_obj_delete(
            screen
        );

        return NULL;
    }

    dashboard_result =
        main_screen_create_can_row(
            content
        );

    if (dashboard_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to create CAN dashboard: %s",
            esp_err_to_name(dashboard_result)
        );

        lv_obj_delete(
            screen
        );

        return NULL;
    }

    dashboard_result =
        main_screen_create_action_row(
            content
        );

    if (dashboard_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to create dashboard actions: %s",
            esp_err_to_name(dashboard_result)
        );

        lv_obj_delete(
            screen
        );

        return NULL;
    }

    main_screen_update();

    s_context.update_timer =
        lv_timer_create(
            main_screen_update_timer_cb,
            MAIN_SCREEN_UPDATE_MS,
            NULL
        );

    if (s_context.update_timer == NULL) {
        ESP_LOGW(
            TAG,
            "Failed to create main-screen update timer"
        );
    }

    lv_obj_update_layout(
        s_context.toolbar.root
    );

    lv_obj_move_foreground(
        s_context.toolbar.root
    );

    return screen;
}

void main_screen_on_show(
    lv_obj_t *screen
)
{
    if ((screen == NULL) ||
        (screen != s_context.root)) {

        return;
    }

    main_screen_update();

    if (s_context.update_timer == NULL) {
        s_context.update_timer =
            lv_timer_create(
                main_screen_update_timer_cb,
                MAIN_SCREEN_UPDATE_MS,
                NULL
            );

        if (s_context.update_timer == NULL) {
            ESP_LOGW(
                TAG,
                "Failed to create main-screen update timer"
            );
        }
    }
}

void main_screen_on_hide(
    lv_obj_t *screen
)
{
    if ((screen == NULL) ||
        (screen != s_context.root)) {

        return;
    }

    if (s_context.update_timer != NULL) {
        lv_timer_delete(
            s_context.update_timer
        );

        s_context.update_timer = NULL;
    }
}

void main_screen_destroy(
    lv_obj_t *screen
)
{
    if ((screen == NULL) ||
        (screen != s_context.root)) {

        return;
    }

    main_screen_on_hide(
        screen
    );

    lv_obj_delete(
        screen
    );

    /*
     * The LV_EVENT_DELETE callback performs the main cleanup. This
     * additional reset keeps the context deterministic.
     */
    memset(
        &s_context,
        0,
        sizeof(s_context)
    );
}
