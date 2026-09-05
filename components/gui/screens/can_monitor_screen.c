/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "screens/can_monitor_screen.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "can_frame.h"
#include "can_monitor_service.h"

#include "assets/gui_images.h"
#include "board_config.h"
#include "gui_config.h"
#include "gui_feedback.h"
#include "gui_styles.h"
#include "gui_theme.h"
#include "screen_manager.h"
#include "widgets/toolbar.h"
#include "widgets/modal_dialog.h"

#define CAN_MONITOR_TOOLBAR_HEIGHT \
    GUI_THEME_TOOLBAR_HEIGHT

#define CAN_MONITOR_UPDATE_PERIOD_MS       (1000U)

#define CAN_MONITOR_PAYLOAD_PREVIEW_BYTES  (8U)
#define CAN_MONITOR_PAYLOAD_TEXT_SIZE      (32U)

#define CAN_MONITOR_IDENTIFIER_SNAPSHOT_CAPACITY (16U)

#define CAN_MONITOR_DETAILS_TEXT_SIZE (512U)

typedef enum
{
    CAN_MONITOR_FILTER_ALL = 0,
    CAN_MONITOR_FILTER_PRIMARY,
    CAN_MONITOR_FILTER_SECONDARY,

} can_monitor_filter_t;

typedef struct
{
    lv_obj_t *root;

    toolbar_t toolbar;

    lv_obj_t *filter_dropdown;
    lv_obj_t *pause_button;
    lv_obj_t *pause_button_label;
    lv_obj_t *clear_button;

    lv_obj_t *statistics_label;
    lv_obj_t *table;

    lv_timer_t *update_timer;

    bool paused;
    can_monitor_filter_t filter;

    can_monitor_identifier_info_t *identifiers;

    size_t row_identifier_index[
        CAN_MONITOR_IDENTIFIER_SNAPSHOT_CAPACITY
    ];

    size_t displayed_identifier_count;

} can_monitor_screen_context_t;

static const char *TAG = "can_monitor_screen";

static can_monitor_screen_context_t s_context = {0};

static modal_dialog_t s_frame_dialog = {0};

static void can_monitor_screen_update(void);

static const char *can_monitor_screen_bus_name(
    can_bus_id_t bus
);

static void can_monitor_screen_back_action(void)
{
    const bool animations_enabled =
        gui_config_get_animations_enabled();

    const esp_err_t result =
        screen_manager_back(
            animations_enabled
                ? LV_SCR_LOAD_ANIM_MOVE_RIGHT
                : LV_SCR_LOAD_ANIM_NONE,
            animations_enabled
                ? 200U
                : 0U
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to return from CAN monitor: %s",
            esp_err_to_name(result)
        );
    }
}

static const char *can_monitor_screen_direction_name(
    can_monitor_direction_t direction
)
{
    switch (direction) {
        case CAN_MONITOR_DIRECTION_RX:
            return "RX";

        case CAN_MONITOR_DIRECTION_TX:
            return "TX";

        case CAN_MONITOR_DIRECTION_NONE:
        default:
            return "None";
    }
}

static const char *can_monitor_screen_timestamp_name(
    can_timestamp_source_t source
)
{
    switch (source) {
        case CAN_TIMESTAMP_SOURCE_SOFTWARE:
            return "Software";

        case CAN_TIMESTAMP_SOURCE_HARDWARE:
            return "Hardware";

        case CAN_TIMESTAMP_SOURCE_NONE:
        default:
            return "None";
    }
}

static const char *can_monitor_screen_frame_type(
    const can_frame_t *frame
)
{
    if (frame == NULL) {
        return "Unknown";
    }

    if ((frame->flags &
         CAN_FRAME_FLAG_REMOTE) != 0U) {

        return "Classical RTR";
    }

    if ((frame->flags &
         CAN_FRAME_FLAG_FD) == 0U) {

        return "Classical";
    }

    if ((frame->flags &
         CAN_FRAME_FLAG_BRS) != 0U) {

        return "CAN FD + BRS";
    }

    return "CAN FD";
}

static void can_monitor_screen_format_details(
    const can_monitor_identifier_info_t *identifier,
    char *text,
    size_t text_size
)
{
    if ((identifier == NULL) ||
        (text == NULL) ||
        (text_size == 0U)) {

        return;
    }

    const can_frame_t *frame =
        &identifier->last_frame;

    const uint64_t now_us =
        (uint64_t)esp_timer_get_time();

    uint64_t age_ms = 0U;

    if ((identifier->last_activity_timestamp_us != 0U) &&
        (now_us >= identifier->last_activity_timestamp_us)) {

        age_ms =
            (now_us -
             identifier->last_activity_timestamp_us) /
            1000U;
    }

    const int written =
        snprintf(
            text,
            text_size,
            "Channel: %s\n"
            "Direction: %s\n"
            "Format: %s\n"
            "Type: %s\n"
            "DLC: %u   Length: %u bytes\n"
            "RX: %llu   TX: %llu\n"
            "Last seen: %llu ms ago\n"
            "Timestamp: %llu us (%s)\n"
            "Data:",
            identifier->bus == CAN_BUS_PRIMARY
                ? "Primary"
                : "Secondary",

            can_monitor_screen_direction_name(
                identifier->last_direction
            ),

            identifier->extended
                ? "Extended 29-bit"
                : "Standard 11-bit",

            can_monitor_screen_frame_type(frame),

            (unsigned int)frame->dlc,
            (unsigned int)frame->data_length,

            (unsigned long long)
                identifier->received_frames,

            (unsigned long long)
                identifier->transmitted_frames,

            (unsigned long long)age_ms,

            (unsigned long long)
                frame->timestamp_us,

            can_monitor_screen_timestamp_name(
                frame->timestamp_source
            )
        );

    if (written < 0) {
        text[0] = '\0';
        return;
    }

    size_t offset =
        (size_t)written < text_size
            ? (size_t)written
            : text_size - 1U;

    if ((frame->flags &
         CAN_FRAME_FLAG_REMOTE) != 0U) {

        (void)snprintf(
            &text[offset],
            text_size - offset,
            " RTR"
        );

        return;
    }

    if (frame->data_length == 0U) {
        (void)snprintf(
            &text[offset],
            text_size - offset,
            " empty"
        );

        return;
    }

    for (size_t index = 0U;
         index < frame->data_length;
         index++) {

        const char *format =
            (index % 16U) == 0U
                ? "\n%02X"
                : " %02X";

        const int data_written =
            snprintf(
                &text[offset],
                text_size - offset,
                format,
                frame->data[index]
            );

        if ((data_written < 0) ||
            ((size_t)data_written >=
             (text_size - offset))) {

            text[text_size - 1U] = '\0';
            return;
        }

        offset +=
            (size_t)data_written;
    }
}

static void can_monitor_screen_table_event_cb(
    lv_event_t *event
)
{
    if (lv_event_get_code(event) !=
        LV_EVENT_VALUE_CHANGED) {

        return;
    }

    uint32_t row = 0U;
    uint32_t column = 0U;

    lv_table_get_selected_cell(
        s_context.table,
        &row,
        &column
    );

    (void)column;

    /*
     * Row zero contains table headers.
     */
    if (row == 0U) {
        return;
    }

    const size_t displayed_index =
        (size_t)(row - 1U);

    if (displayed_index >=
        s_context.displayed_identifier_count) {

        return;
    }

    const size_t identifier_index =
        s_context.row_identifier_index[
            displayed_index
        ];

    if (identifier_index >=
        CAN_MONITOR_IDENTIFIER_SNAPSHOT_CAPACITY) {

        return;
    }

    const can_monitor_identifier_info_t *identifier =
        &s_context.identifiers[
            identifier_index
        ];

    char title[32] = {0};

    if (identifier->extended) {
        (void)snprintf(
            title,
            sizeof(title),
            "%s  %08lX",
            can_monitor_screen_bus_name(
                identifier->bus
            ),
            (unsigned long)
                identifier->identifier
        );
    } else {
        (void)snprintf(
            title,
            sizeof(title),
            "%s  %03lX",
            can_monitor_screen_bus_name(
                identifier->bus
            ),
            (unsigned long)
                identifier->identifier
        );
    }

    char details[
        CAN_MONITOR_DETAILS_TEXT_SIZE
    ] = {0};

    can_monitor_screen_format_details(
        identifier,
        details,
        sizeof(details)
    );

    if (modal_dialog_is_open(
            &s_frame_dialog
        )) {

        modal_dialog_close(
            &s_frame_dialog
        );
    }

    const modal_dialog_config_t config = {
        .title = title,
        .message = details,

        .icon = NULL,

        .primary_button_text = "Close",
        .primary_action = NULL,
        .close_on_primary_action = true,

        .secondary_button_text = NULL,
        .secondary_action = NULL,
        .close_on_secondary_action = false,

        .show_progress_bar = false,
        .initial_progress = 0U,
        .progress_text = NULL,

        .animate_open =
            gui_config_get_animations_enabled(),

        .close_on_overlay_click = true,
    };

    if (!modal_dialog_create(
            &s_frame_dialog,
            s_context.root,
            &config
        )) {

        ESP_LOGW(
            TAG,
            "Failed to open CAN frame details"
        );

        return;
    }

    /*
     * Full CAN FD payload may not fit into the modal height.
     */
    lv_obj_add_flag(
        s_frame_dialog.content_container,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_scroll_dir(
        s_frame_dialog.content_container,
        LV_DIR_VER
    );

    lv_obj_set_scrollbar_mode(
        s_frame_dialog.content_container,
        LV_SCROLLBAR_MODE_AUTO
    );

    lv_obj_set_flex_align(
        s_frame_dialog.content_container,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START
    );

    lv_obj_set_style_text_align(
        s_frame_dialog.message_label,
        LV_TEXT_ALIGN_LEFT,
        LV_PART_MAIN
    );

    lv_obj_set_style_text_font(
        s_frame_dialog.message_label,
        &lv_font_unscii_8,
        LV_PART_MAIN
    );
}

static bool can_monitor_screen_identifier_visible(
    const can_monitor_identifier_info_t *identifier
)
{
    if (identifier == NULL) {
        return false;
    }

    switch (s_context.filter) {
        case CAN_MONITOR_FILTER_PRIMARY:
            return identifier->bus == CAN_BUS_PRIMARY;

        case CAN_MONITOR_FILTER_SECONDARY:
            return identifier->bus == CAN_BUS_SECONDARY;

        case CAN_MONITOR_FILTER_ALL:
        default:
            return true;
    }
}

static const char *can_monitor_screen_bus_name(
    can_bus_id_t bus
)
{
    switch (bus) {
        case CAN_BUS_PRIMARY:
            return "P";

        case CAN_BUS_SECONDARY:
            return "S";

        default:
            return "?";
    }
}

static void can_monitor_screen_format_identifier(
    const can_monitor_identifier_info_t *identifier,
    char *text,
    size_t text_size
)
{
    if ((identifier == NULL) ||
        (text == NULL) ||
        (text_size == 0U)) {

        return;
    }

    if (identifier->extended) {
        (void)snprintf(
            text,
            text_size,
            "%08lX",
            (unsigned long)identifier->identifier
        );
    } else {
        (void)snprintf(
            text,
            text_size,
            "%03lX",
            (unsigned long)identifier->identifier
        );
    }
}

static void can_monitor_screen_format_dlc(
    const can_frame_t *frame,
    char *text,
    size_t text_size
)
{
    if ((frame == NULL) ||
        (text == NULL) ||
        (text_size == 0U)) {

        return;
    }

    const bool fd =
        (frame->flags & CAN_FRAME_FLAG_FD) != 0U;

    const bool brs =
        (frame->flags & CAN_FRAME_FLAG_BRS) != 0U;

    if (fd && brs) {
        (void)snprintf(
            text,
            text_size,
            "%u FD+B",
            (unsigned int)frame->dlc
        );
    } else if (fd) {
        (void)snprintf(
            text,
            text_size,
            "%u FD",
            (unsigned int)frame->dlc
        );
    } else {
        (void)snprintf(
            text,
            text_size,
            "%u",
            (unsigned int)frame->dlc
        );
    }
}

static void can_monitor_screen_format_payload(
    const can_frame_t *frame,
    char *text,
    size_t text_size
)
{
    if ((frame == NULL) ||
        (text == NULL) ||
        (text_size == 0U)) {

        return;
    }

    text[0] = '\0';

    if ((frame->flags & CAN_FRAME_FLAG_REMOTE) != 0U) {
        (void)snprintf(
            text,
            text_size,
            "RTR"
        );

        return;
    }

    const size_t displayed_length =
        frame->data_length < CAN_MONITOR_PAYLOAD_PREVIEW_BYTES
            ? frame->data_length
            : CAN_MONITOR_PAYLOAD_PREVIEW_BYTES;

    size_t offset = 0U;

    for (size_t index = 0U;
         index < displayed_length;
         index++) {

        const int written =
            snprintf(
                &text[offset],
                text_size - offset,
                index == 0U
                    ? "%02X"
                    : " %02X",
                frame->data[index]
            );

        if ((written < 0) ||
            ((size_t)written >=
             (text_size - offset))) {

            text[text_size - 1U] = '\0';
            return;
        }

        offset += (size_t)written;
    }

    if ((frame->data_length >
         CAN_MONITOR_PAYLOAD_PREVIEW_BYTES) &&
        ((text_size - offset) > 3U)) {

        (void)snprintf(
            &text[offset],
            text_size - offset,
            " ..."
        );
    }
}

static void can_monitor_screen_update_statistics(
    const can_monitor_service_statistics_t *statistics
)
{
    if ((statistics == NULL) ||
        (s_context.statistics_label == NULL)) {

        return;
    }

    uint64_t received = 0U;
    uint64_t transmitted = 0U;

    const char *filter_name = "All";

    switch (s_context.filter) {
        case CAN_MONITOR_FILTER_PRIMARY:
            received =
                statistics
                    ->buses[CAN_BUS_PRIMARY]
                    .received_frames;

            transmitted =
                statistics
                    ->buses[CAN_BUS_PRIMARY]
                    .completed_transmissions;

            filter_name = "P";
            break;

        case CAN_MONITOR_FILTER_SECONDARY:
            received =
                statistics
                    ->buses[CAN_BUS_SECONDARY]
                    .received_frames;

            transmitted =
                statistics
                    ->buses[CAN_BUS_SECONDARY]
                    .completed_transmissions;

            filter_name = "S";
            break;

        case CAN_MONITOR_FILTER_ALL:
        default:
            for (size_t bus = 0U;
                 bus < CAN_BUS_COUNT;
                 bus++) {

                received +=
                    statistics
                        ->buses[bus]
                        .received_frames;

                transmitted +=
                    statistics
                        ->buses[bus]
                        .completed_transmissions;
            }

            break;
    }

    lv_label_set_text_fmt(
        s_context.statistics_label,
        "%s: RX %llu  TX %llu  Dropped* %llu\n"
        "IDs* %lu/%lu  Queue* %lu/%lu",
        filter_name,
        (unsigned long long)received,
        (unsigned long long)transmitted,
        (unsigned long long)
            statistics->dropped_input_events,
        (unsigned long)
            statistics->tracked_identifiers,
        (unsigned long)
            statistics->identifier_capacity,
        (unsigned long)
            statistics->input_queue_current,
        (unsigned long)
            statistics->input_queue_capacity
    );
}

static void can_monitor_screen_update_table(void)
{
    if ((s_context.table == NULL) ||
        (s_context.identifiers == NULL)) {

        return;
    }

    s_context.displayed_identifier_count = 0U;

    size_t identifier_count = 0U;

    const esp_err_t result =
        can_monitor_service_get_identifiers(
            s_context.identifiers,
            CAN_MONITOR_IDENTIFIER_SNAPSHOT_CAPACITY,
            &identifier_count
        );

    if (result != ESP_OK) {
        lv_table_set_row_count(
            s_context.table,
            1U
        );

        return;
    }

    size_t visible_count = 0U;

    for (size_t index = 0U;
         index < identifier_count;
         index++) {

        if (can_monitor_screen_identifier_visible(
                &s_context.identifiers[index]
            )) {

            visible_count++;
        }
    }

    lv_table_set_row_count(
        s_context.table,
        (uint32_t)(visible_count + 1U)
    );

    lv_table_set_cell_value(
        s_context.table,
        0U,
        0U,
        "Ch"
    );

    lv_table_set_cell_value(
        s_context.table,
        0U,
        1U,
        "ID"
    );

    lv_table_set_cell_value(
        s_context.table,
        0U,
        2U,
        "RX"
    );

    lv_table_set_cell_value(
        s_context.table,
        0U,
        3U,
        "TX"
    );

    lv_table_set_cell_value(
        s_context.table,
        0U,
        4U,
        "DLC"
    );

    lv_table_set_cell_value(
        s_context.table,
        0U,
        5U,
        "Latest data"
    );

    uint32_t row = 1U;

    for (size_t index = 0U;
         index < identifier_count;
         index++) {

        const can_monitor_identifier_info_t *identifier =
            &s_context.identifiers[index];

        if (!can_monitor_screen_identifier_visible(
                identifier
            )) {

            continue;
        }

        if (s_context.displayed_identifier_count >=
            CAN_MONITOR_IDENTIFIER_SNAPSHOT_CAPACITY) {

            break;
        }

        s_context.row_identifier_index[
            s_context.displayed_identifier_count
        ] = index;

        s_context.displayed_identifier_count++;

        char identifier_text[12] = {0};
        char rx_text[24] = {0};
        char tx_text[24] = {0};
        char dlc_text[16] = {0};

        char payload_text[
            CAN_MONITOR_PAYLOAD_TEXT_SIZE
        ] = {0};

        can_monitor_screen_format_identifier(
            identifier,
            identifier_text,
            sizeof(identifier_text)
        );

        can_monitor_screen_format_dlc(
            &identifier->last_frame,
            dlc_text,
            sizeof(dlc_text)
        );

        can_monitor_screen_format_payload(
            &identifier->last_frame,
            payload_text,
            sizeof(payload_text)
        );

        (void)snprintf(
            rx_text,
            sizeof(rx_text),
            "%llu",
            (unsigned long long)identifier->received_frames
        );

        (void)snprintf(
            tx_text,
            sizeof(tx_text),
            "%llu",
            (unsigned long long)identifier->transmitted_frames
        );

        lv_table_set_cell_value(
            s_context.table,
            row,
            0U,
            can_monitor_screen_bus_name(
                identifier->bus
            )
        );

        lv_table_set_cell_value(
            s_context.table,
            row,
            1U,
            identifier_text
        );

        lv_table_set_cell_value(
            s_context.table,
            row,
            2U,
            rx_text
        );

        lv_table_set_cell_value(
            s_context.table,
            row,
            3U,
            tx_text
        );

        lv_table_set_cell_value(
            s_context.table,
            row,
            4U,
            dlc_text
        );

        lv_table_set_cell_value(
            s_context.table,
            row,
            5U,
            payload_text
        );

        row++;
    }
}

static void can_monitor_screen_update(void)
{
    if ((s_context.root == NULL) ||
        s_context.paused) {

        return;
    }

    if (!can_monitor_service_is_running()) {
        if (s_context.statistics_label != NULL) {
            lv_label_set_text(
                s_context.statistics_label,
                "CAN monitor service is not running"
            );
        }

        if (s_context.table != NULL) {
            lv_table_set_row_count(
                s_context.table,
                1U
            );
        }

        return;
    }

    can_monitor_service_statistics_t statistics = {0};

    const esp_err_t result =
        can_monitor_service_get_statistics(
            &statistics
        );

    if (result == ESP_OK) {
        can_monitor_screen_update_statistics(
            &statistics
        );
    }

    can_monitor_screen_update_table();
}

static void can_monitor_screen_filter_event_cb(
    lv_event_t *event
)
{
    if (lv_event_get_code(event) !=
        LV_EVENT_VALUE_CHANGED) {

        return;
    }

    const uint32_t selected =
        lv_dropdown_get_selected(
            s_context.filter_dropdown
        );

    switch (selected) {
        case 1U:
            s_context.filter =
                CAN_MONITOR_FILTER_PRIMARY;
            break;

        case 2U:
            s_context.filter =
                CAN_MONITOR_FILTER_SECONDARY;
            break;

        case 0U:
        default:
            s_context.filter =
                CAN_MONITOR_FILTER_ALL;
            break;
    }

    can_monitor_screen_update();
}

static void can_monitor_screen_pause_event_cb(
    lv_event_t *event
)
{
    if (lv_event_get_code(event) !=
        LV_EVENT_CLICKED) {

        return;
    }

    s_context.paused =
        !s_context.paused;

    lv_label_set_text(
        s_context.pause_button_label,
        s_context.paused
            ? "Resume"
            : "Pause"
    );

    if (!s_context.paused) {
        can_monitor_screen_update();
    }
}

static void can_monitor_screen_clear_event_cb(
    lv_event_t *event
)
{
    if (lv_event_get_code(event) !=
        LV_EVENT_CLICKED) {

        return;
    }

    if (!can_monitor_service_is_running()) {
        return;
    }

    const esp_err_t result =
        can_monitor_service_clear();

    if (result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to clear CAN monitor: %s",
            esp_err_to_name(result)
        );

        return;
    }

    s_context.displayed_identifier_count = 0U;

    memset(
        s_context.row_identifier_index,
        0,
        sizeof(s_context.row_identifier_index)
    );

    lv_label_set_text(
        s_context.statistics_label,
        "RX 0  TX 0  Dropped 0\n"
        "IDs 0/0  Queue 0/0"
    );

    lv_table_set_row_count(
        s_context.table,
        1U
    );
}

static lv_obj_t *can_monitor_screen_create_button(
    lv_obj_t *parent,
    const char *text,
    lv_event_cb_t callback
)
{
    lv_obj_t *button =
        lv_button_create(parent);

    if (button == NULL) {
        return NULL;
    }

    lv_obj_set_height(
        button,
        36
    );

    gui_feedback_attach(
        button
    );

    lv_obj_add_style(
        button,
        gui_styles_button_secondary(),
        LV_PART_MAIN
    );

    lv_obj_add_event_cb(
        button,
        callback,
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

    lv_label_set_text(
        label,
        text
    );

    lv_obj_center(label);

    return button;
}

static esp_err_t can_monitor_screen_create_controls(
    lv_obj_t *parent
)
{
    lv_obj_t *controls =
        lv_obj_create(parent);

    if (controls == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_size(
        controls,
        LV_PCT(100),
        40
    );

    lv_obj_set_style_bg_opa(
        controls,
        LV_OPA_TRANSP,
        LV_PART_MAIN
    );

    lv_obj_set_style_border_width(
        controls,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        controls,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_column(
        controls,
        8,
        LV_PART_MAIN
    );

    lv_obj_remove_flag(
        controls,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_flex_flow(
        controls,
        LV_FLEX_FLOW_ROW
    );

    lv_obj_set_flex_align(
        controls,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    s_context.filter_dropdown =
        lv_dropdown_create(controls);

    if (s_context.filter_dropdown == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_obj_set_size(
        s_context.filter_dropdown,
        150,
        36
    );

    lv_dropdown_set_options(
        s_context.filter_dropdown,
        "All buses\nPrimary\nSecondary"
    );

    lv_obj_add_event_cb(
        s_context.filter_dropdown,
        can_monitor_screen_filter_event_cb,
        LV_EVENT_VALUE_CHANGED,
        NULL
    );

    s_context.pause_button =
        can_monitor_screen_create_button(
            controls,
            "Pause",
            can_monitor_screen_pause_event_cb
        );

    if (s_context.pause_button == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_context.pause_button_label =
        lv_obj_get_child(
            s_context.pause_button,
            0
        );

    s_context.clear_button =
        can_monitor_screen_create_button(
            controls,
            "Clear",
            can_monitor_screen_clear_event_cb
        );

    if (s_context.clear_button == NULL) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static void can_monitor_screen_timer_cb(
    lv_timer_t *timer
)
{
    (void)timer;

    can_monitor_screen_update();
}

static void can_monitor_screen_delete_event_cb(
    lv_event_t *event
)
{
    (void)event;

    if (modal_dialog_is_open(&s_frame_dialog)) {
        modal_dialog_close(&s_frame_dialog);
    }

    s_frame_dialog = (modal_dialog_t){0};

    if (s_context.update_timer != NULL) {
        lv_timer_delete(
            s_context.update_timer
        );

        s_context.update_timer = NULL;
    }

    if (s_context.identifiers != NULL) {
        heap_caps_free(
            s_context.identifiers
        );

        s_context.identifiers = NULL;
    }

    s_context =
        (can_monitor_screen_context_t){0};
}

lv_obj_t *can_monitor_screen_create(void)
{
    if (!gui_theme_is_initialized() ||
        !gui_styles_is_initialized()) {

        return NULL;
    }

    lv_obj_t *screen =
        lv_obj_create(NULL);

    if (screen == NULL) {
        return NULL;
    }

    s_context.root = screen;

    lv_obj_add_event_cb(
        screen,
        can_monitor_screen_delete_event_cb,
        LV_EVENT_DELETE,
        NULL
    );

    s_context.identifiers =
        heap_caps_calloc(
            CAN_MONITOR_IDENTIFIER_SNAPSHOT_CAPACITY,
            sizeof(*s_context.identifiers),
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

    if (s_context.identifiers == NULL) {
        lv_obj_delete(screen);
        return NULL;
    }

    s_context.filter = CAN_MONITOR_FILTER_ALL;
    s_context.paused = false;

    lv_obj_add_style(
        screen,
        gui_styles_screen(),
        LV_PART_MAIN
    );

    lv_obj_remove_flag(
        screen,
        LV_OBJ_FLAG_SCROLLABLE
    );

    const toolbar_config_t toolbar_config = {
        .title = "CAN Monitor",

        .left_icon = &icons8_back_32,
        .left_action = can_monitor_screen_back_action,

        .right_icon = NULL,
        .right_action = NULL,

        .show_usb_status = false,
        .usb_action = NULL,

        .show_wifi_status = false,
        .wifi_action = NULL,

        .show_cpu_status = false,
        .cpu_action = NULL,

        .show_sd_status = false,
        .sd_action = NULL,

        .show_ota_status = false,
        .ota_action = NULL,

        .show_internet_status = false,
    };

    s_context.toolbar =
        toolbar_create(
            screen,
            &toolbar_config
        );

    if (s_context.toolbar.root == NULL) {
        lv_obj_delete(screen);
        return NULL;
    }

    lv_obj_t *content =
        lv_obj_create(screen);

    if (content == NULL) {
        lv_obj_delete(screen);
        return NULL;
    }

    lv_obj_set_size(
        content,
        LV_PCT(100),
        LCD_V_RES - CAN_MONITOR_TOOLBAR_HEIGHT
    );

    lv_obj_align(
        content,
        LV_ALIGN_BOTTOM_MID,
        0,
        0
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

    lv_obj_set_style_pad_all(
        content,
        GUI_THEME_SPACE_SM,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_row(
        content,
        GUI_THEME_SPACE_XS,
        LV_PART_MAIN
    );

    lv_obj_set_flex_flow(
        content,
        LV_FLEX_FLOW_COLUMN
    );

    lv_obj_remove_flag(
        content,
        LV_OBJ_FLAG_SCROLLABLE
    );

    if (can_monitor_screen_create_controls(
            content
        ) != ESP_OK) {

        lv_obj_delete(screen);
        return NULL;
    }

    s_context.statistics_label =
        lv_label_create(content);

    if (s_context.statistics_label == NULL) {
        lv_obj_delete(screen);
        return NULL;
    }

    lv_obj_add_style(
        s_context.statistics_label,
        gui_styles_text_small(),
        LV_PART_MAIN
    );

    lv_label_set_text(
        s_context.statistics_label,
        "RX 0  TX 0  Dropped 0\n"
        "IDs 0/0  Queue 0/0"
    );

    s_context.table =
        lv_table_create(content);

    if (s_context.table == NULL) {
        lv_obj_delete(screen);
        return NULL;
    }

    lv_obj_add_event_cb(
        s_context.table,
        can_monitor_screen_table_event_cb,
        LV_EVENT_VALUE_CHANGED,
        NULL
    );

    lv_obj_add_style(
        s_context.table,
        gui_styles_text_small(),
        LV_PART_ITEMS
    );

    lv_obj_set_style_text_font(
        s_context.table,
        &lv_font_unscii_8,
        LV_PART_ITEMS
    );

    lv_obj_set_style_pad_top(
        s_context.table,
        2,
        LV_PART_ITEMS
    );

    lv_obj_set_style_pad_bottom(
        s_context.table,
        2,
        LV_PART_ITEMS
    );

    lv_obj_set_style_pad_left(
        s_context.table,
        3,
        LV_PART_ITEMS
    );

    lv_obj_set_style_pad_right(
        s_context.table,
        3,
        LV_PART_ITEMS
    );

    lv_obj_set_width(
        s_context.table,
        LV_PCT(100)
    );

    lv_obj_set_flex_grow(
        s_context.table,
        1
    );

    lv_table_set_column_count(
        s_context.table,
        6U
    );

    lv_table_set_column_width(
        s_context.table,
        0U,
        26
    );

    lv_table_set_column_width(
        s_context.table,
        1U,
        72
    );

    lv_table_set_column_width(
        s_context.table,
        2U,
        54
    );

    lv_table_set_column_width(
        s_context.table,
        3U,
        54
    );

    lv_table_set_column_width(
        s_context.table,
        4U,
        52
    );

    lv_table_set_column_width(
        s_context.table,
        5U,
        200
    );

    can_monitor_screen_update();

    s_context.update_timer =
        lv_timer_create(
            can_monitor_screen_timer_cb,
            CAN_MONITOR_UPDATE_PERIOD_MS,
            NULL
        );

    lv_obj_move_foreground(
        s_context.toolbar.root
    );

    return screen;
}

void can_monitor_screen_on_show(
    lv_obj_t *screen
)
{
    if ((screen == NULL) ||
        (screen != s_context.root)) {

        return;
    }

    can_monitor_screen_update();

    if (s_context.update_timer == NULL) {
        s_context.update_timer =
            lv_timer_create(
                can_monitor_screen_timer_cb,
                CAN_MONITOR_UPDATE_PERIOD_MS,
                NULL
            );
    }
}

void can_monitor_screen_on_hide(
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

void can_monitor_screen_destroy(
    lv_obj_t *screen
)
{
    if ((screen == NULL) ||
        (screen != s_context.root)) {

        return;
    }

    can_monitor_screen_on_hide(
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
