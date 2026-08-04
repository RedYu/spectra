#include "screens/main_screen.h"

#include <string.h>

#include "esp_log.h"

#include "system_model.h"
#include "widgets/toolbar.h"
#include "widgets/modal_dialog.h"
#include "widgets/sd_card_modal.h"
#include "assets/gui_images.h"

#include "gui_config.h"
#include "board_config.h"
#include "screen_manager.h"
#include "usb_network_service.h"
#include "wifi_service.h"

#define TOOLBAR_HEIGHT          (56U)
#define MAIN_SCREEN_UPDATE_MS   (1000U)

#define MAIN_CAN_CHANNEL_COUNT  (2U)
#define MAIN_CAN_ROW_HEIGHT     (132)
#define MAIN_CAN_CARD_PADDING   (8)
#define MAIN_CAN_CARD_RADIUS    (12)
#define MAIN_RECORDING_ROW_HEIGHT  (44)
#define MAIN_ACTION_ROW_HEIGHT  (44)

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

    lv_obj_t *can_load_label[
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

    lv_timer_t *update_timer;

} main_screen_context_t;

static const char *TAG = "main_screen";

static main_screen_context_t s_context = {0};

static modal_dialog_t s_update_dialog = {0};

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

    ESP_LOGI(
        TAG,
        "CAN monitor screen is not implemented yet"
    );
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

    lv_obj_set_height(
        button,
        LV_PCT(100)
    );

    lv_obj_set_flex_grow(
        button,
        1
    );

    lv_obj_set_style_radius(
        button,
        10,
        LV_PART_MAIN
    );

    lv_obj_set_style_border_width(
        button,
        primary ? 0 : 1,
        LV_PART_MAIN
    );

    lv_obj_set_style_border_color(
        button,
        lv_color_hex(0x2563EBU),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        button,
        primary
            ? lv_color_hex(0x2563EBU)
            : lv_color_hex(0xFFFFFFU),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        button,
        LV_OPA_COVER,
        LV_PART_MAIN
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
        primary
            ? lv_color_hex(0xFFFFFFU)
            : lv_color_hex(0x2563EBU),
        LV_PART_MAIN
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

    lv_obj_add_state(
        s_context.monitor_button,
        LV_STATE_DISABLED
    );

    return ESP_OK;
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
    } else {
        toolbar_set_wifi_status(
            &s_context.toolbar,
            false,
            0U
        );
    }

    /*
     * CAN and recording information will be updated here after the
     * dashboard model is implemented.
     */
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

    lv_obj_set_style_bg_color(
        card,
        lv_color_hex(0xF5F7FAU),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        card,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_set_style_border_width(
        card,
        1,
        LV_PART_MAIN
    );

    lv_obj_set_style_border_color(
        card,
        lv_color_hex(0xE2E8F0U),
        LV_PART_MAIN
    );

    lv_obj_set_style_radius(
        card,
        MAIN_CAN_CARD_RADIUS,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        card,
        MAIN_CAN_CARD_PADDING,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_row(
        card,
        2,
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
    const lv_font_t *font,
    lv_color_t color
)
{
    if ((parent == NULL) ||
        (text == NULL) ||
        (font == NULL)) {

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

    lv_obj_set_style_text_font(
        label,
        font,
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        label,
        color,
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

    lv_obj_t *title_label =
        main_screen_create_card_label(
            card,
            title,
            &lv_font_montserrat_16,
            lv_color_hex(0x111827U)
        );

    if (title_label == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_context.can_state_label[channel_index] =
        main_screen_create_card_label(
            card,
            "State: Not configured",
            &lv_font_montserrat_14,
            lv_color_hex(0x6B7280U)
        );

    s_context.can_bitrate_label[channel_index] =
        main_screen_create_card_label(
            card,
            "Bitrate: N/A",
            &lv_font_montserrat_14,
            lv_color_hex(0x374151U)
        );

    s_context.can_load_label[channel_index] =
        main_screen_create_card_label(
            card,
            "Load: 0%",
            &lv_font_montserrat_14,
            lv_color_hex(0x374151U)
        );

    s_context.can_frames_label[channel_index] =
        main_screen_create_card_label(
            card,
            "Frames/s: 0",
            &lv_font_montserrat_14,
            lv_color_hex(0x374151U)
        );

    s_context.can_errors_label[channel_index] =
        main_screen_create_card_label(
            card,
            "Errors: 0",
            &lv_font_montserrat_14,
            lv_color_hex(0x374151U)
        );

    if ((s_context.can_state_label[channel_index] == NULL) ||
        (s_context.can_bitrate_label[channel_index] == NULL) ||
        (s_context.can_load_label[channel_index] == NULL) ||
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
        lv_color_hex(0x9CA3AFU),
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
            &lv_font_montserrat_14,
            lv_color_hex(0x374151U)
        );

    if (s_context.recording_status_label == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_context.recording_details_label =
        main_screen_create_card_label(
            row,
            "00:00:00 | 0 B | Dropped: 0",
            &lv_font_montserrat_14,
            lv_color_hex(0x6B7280U)
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
            "CAN 1"
        );

    if (result != ESP_OK) {
        return result;
    }

    return main_screen_create_can_card(
        row,
        1U,
        "CAN 2"
    );
}

lv_obj_t *main_screen_create(void)
{
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

    lv_obj_set_style_pad_all(
        content,
        10,
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

    lv_obj_set_style_pad_row(
        content,
        8,
        LV_PART_MAIN
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
