#include "screens/main_screen.h"

#include <inttypes.h>
#include <stdio.h>

#include "system_model.h"
#include "screens/settings_screen.h"
#include "widgets/toolbar.h"

#define TOOLBAR_HEIGHT          56
#define MAIN_SCREEN_UPDATE_MS   1000

typedef struct
{
    lv_obj_t *screen;

    lv_obj_t *device_name_label;
    lv_obj_t *device_id_label;
    lv_obj_t *firmware_label;
    lv_obj_t *hardware_label;
    lv_obj_t *serial_label;

    lv_obj_t *uptime_label;
    lv_obj_t *heap_label;
    lv_obj_t *minimum_heap_label;
    lv_obj_t *cpu_label;
    lv_obj_t *storage_label;
    lv_obj_t *ota_label;

    lv_timer_t *update_timer;

} main_screen_context_t;

static main_screen_context_t s_context;

static void open_settings_screen(void)
{
    lv_obj_t *settings_screen =
        settings_screen_create();

    if (settings_screen == NULL) {
        return;
    }

    lv_screen_load_anim(
        settings_screen,
        LV_SCR_LOAD_ANIM_NONE,
        0,
        0,
        true
    );
}

static void format_uptime(
    uint32_t uptime_sec,
    char *buffer,
    size_t buffer_size
)
{
    uint32_t days =
        uptime_sec / 86400;

    uint32_t hours =
        (uptime_sec % 86400) / 3600;

    uint32_t minutes =
        (uptime_sec % 3600) / 60;

    uint32_t seconds =
        uptime_sec % 60;

    if (days > 0) {
        snprintf(
            buffer,
            buffer_size,
            "%" PRIu32 "d %02" PRIu32 ":%02" PRIu32 ":%02" PRIu32,
            days,
            hours,
            minutes,
            seconds
        );
    } else {
        snprintf(
            buffer,
            buffer_size,
            "%02" PRIu32 ":%02" PRIu32 ":%02" PRIu32,
            hours,
            minutes,
            seconds
        );
    }
}

static void main_screen_update(void)
{
    if (s_context.screen == NULL) {
        return;
    }

    system_model_t model;

    if (system_model_get_snapshot(&model) != ESP_OK) {
        return;
    }

    char buffer[96];
    char uptime_buffer[32];

    format_uptime(
        model.uptime_sec,
        uptime_buffer,
        sizeof(uptime_buffer)
    );

    snprintf(
        buffer,
        sizeof(buffer),
        "Uptime: %s",
        uptime_buffer
    );

    lv_label_set_text(
        s_context.uptime_label,
        buffer
    );

    snprintf(
        buffer,
        sizeof(buffer),
        "Free heap: %" PRIu32 " KB",
        model.free_heap / 1024
    );

    lv_label_set_text(
        s_context.heap_label,
        buffer
    );

    snprintf(
        buffer,
        sizeof(buffer),
        "Minimum heap: %" PRIu32 " KB",
        model.minimum_free_heap / 1024
    );

    lv_label_set_text(
        s_context.minimum_heap_label,
        buffer
    );

    snprintf(
        buffer,
        sizeof(buffer),
        "CPU usage: %u%%",
        (unsigned int)model.cpu_usage
    );

    lv_label_set_text(
        s_context.cpu_label,
        buffer
    );

    lv_label_set_text(
        s_context.storage_label,
        model.storage_ready
            ? "Storage: Ready"
            : "Storage: Not ready"
    );

    lv_label_set_text(
        s_context.ota_label,
        model.ota_available
            ? "Update available"
            : "Firmware is up to date"
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

    s_context.screen = NULL;

    s_context.device_name_label = NULL;
    s_context.device_id_label = NULL;
    s_context.firmware_label = NULL;
    s_context.hardware_label = NULL;
    s_context.serial_label = NULL;

    s_context.uptime_label = NULL;
    s_context.heap_label = NULL;
    s_context.minimum_heap_label = NULL;
    s_context.cpu_label = NULL;
    s_context.storage_label = NULL;
    s_context.ota_label = NULL;
}

static lv_obj_t *create_info_label(
    lv_obj_t *parent,
    const char *text
)
{
    lv_obj_t *label =
        lv_label_create(parent);

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
        lv_color_hex(0x20252A),
        LV_PART_MAIN
    );

    return label;
}

lv_obj_t *main_screen_create(void)
{
    system_model_t model;

    if (system_model_get_snapshot(&model) != ESP_OK) {
        return NULL;
    }

    lv_obj_t *screen =
        lv_obj_create(NULL);

    s_context.screen = screen;

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
        .title = "SPECTRA",

        .left_icon = NULL,
        .left_action = NULL,

        .right_icon = LV_SYMBOL_SETTINGS,
        .right_action = open_settings_screen,
    };

    toolbar_create(
        screen,
        &toolbar_config
    );

    /*
     * Main scrollable content container.
     */
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

    /*
     * Enable only vertical scrolling.
     */
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

    lv_obj_set_style_pad_all(
        content,
        16,
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
     * Arrange labels vertically.
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

    char buffer[96];

    snprintf(
        buffer,
        sizeof(buffer),
        "Device: %s",
        model.device_name
    );

    s_context.device_name_label =
        create_info_label(
            content,
            buffer
        );

    snprintf(
        buffer,
        sizeof(buffer),
        "Device ID: %s",
        model.device_id
    );

    s_context.device_id_label =
        create_info_label(
            content,
            buffer
        );

    snprintf(
        buffer,
        sizeof(buffer),
        "Firmware: %s",
        model.firmware_version
    );

    s_context.firmware_label =
        create_info_label(
            content,
            buffer
        );

    snprintf(
        buffer,
        sizeof(buffer),
        "Hardware: %s",
        model.hardware_version
    );

    s_context.hardware_label =
        create_info_label(
            content,
            buffer
        );

    snprintf(
        buffer,
        sizeof(buffer),
        "Serial: %s",
        model.serial_number
    );

    s_context.serial_label =
        create_info_label(
            content,
            buffer
        );

    s_context.uptime_label =
        create_info_label(
            content,
            ""
        );

    s_context.heap_label =
        create_info_label(
            content,
            ""
        );

    s_context.minimum_heap_label =
        create_info_label(
            content,
            ""
        );

    s_context.cpu_label =
        create_info_label(
            content,
            ""
        );

    s_context.storage_label =
        create_info_label(
            content,
            ""
        );

    s_context.ota_label =
        create_info_label(
            content,
            ""
        );

    main_screen_update();

    s_context.update_timer =
        lv_timer_create(
            main_screen_update_timer_cb,
            MAIN_SCREEN_UPDATE_MS,
            NULL
        );

    return screen;
}
