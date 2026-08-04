#include "widgets/toolbar.h"

#include <stdlib.h>

#include "assets/gui_images.h"

#define TOOLBAR_HEIGHT       56
#define TOOLBAR_BUTTON_SIZE  44
#define TOOLBAR_STATUS_CONTAINER_WIDTH  (340)

#define TOOLBAR_COLOR_BACKGROUND       0x343B42
#define TOOLBAR_COLOR_PRESSED          0x4B77D1

#define TOOLBAR_ICON_COLOR_DEFAULT      0xFFFFFF
#define TOOLBAR_ICON_COLOR_INACTIVE     0x7B858F
#define TOOLBAR_ICON_COLOR_SD_MOUNTED   0x4B77D1
#define TOOLBAR_ICON_COLOR_SD_ERROR     0xE05252
#define TOOLBAR_ICON_COLOR_OTA_READY    0xF4B400

#define TOOLBAR_STATUS_BUTTON_HEIGHT  (44)
#define TOOLBAR_USB_BUTTON_WIDTH   (46)
#define TOOLBAR_WIFI_BUTTON_WIDTH  (60)
#define TOOLBAR_CPU_BUTTON_WIDTH   (82)

#define TOOLBAR_STATUS_BUTTON_PADDING_HORIZONTAL  (6)
#define TOOLBAR_STATUS_BUTTON_PADDING_VERTICAL    (2)

#define TOOLBAR_STATUS_COLOR_ACTIVE    0x4B77D1
#define TOOLBAR_STATUS_COLOR_WARNING   0xF4B400
#define TOOLBAR_STATUS_COLOR_CRITICAL  0xE05252

typedef struct
{
    lv_obj_t *image;
    lv_obj_t *label;

    toolbar_action_cb_t action;

} toolbar_button_context_t;

static void toolbar_button_event_cb(
    lv_event_t *event
)
{
    lv_obj_t *button =
        lv_event_get_current_target_obj(event);

    if (button == NULL) {
        return;
    }

    toolbar_button_context_t *context =
        lv_obj_get_user_data(button);

    if ((context != NULL) &&
        (context->action != NULL)) {

        context->action();
    }
}

static void toolbar_button_delete_event_cb(
    lv_event_t *event
)
{
    lv_obj_t *button =
        lv_event_get_current_target_obj(event);

    if (button == NULL) {
        return;
    }

    toolbar_button_context_t *context =
        lv_obj_get_user_data(button);

    lv_obj_set_user_data(
        button,
        NULL
    );

    free(
        context
    );
}

static lv_obj_t *toolbar_button_create(
    lv_obj_t *parent,
    const lv_image_dsc_t *image_source,
    toolbar_action_cb_t callback
)
{
    if (parent == NULL || image_source == NULL) {
        return NULL;
    }
    toolbar_button_context_t *context =
        calloc(
            1U,
            sizeof(*context)
        );

    if (context == NULL) {
        return NULL;
    }

    context->action = callback;

    lv_obj_t *button =
        lv_button_create(parent);

    if (button == NULL) {
        free(context);
        return NULL;
    }

    lv_obj_set_user_data(
        button,
        context
    );

    lv_obj_add_event_cb(
        button,
        toolbar_button_delete_event_cb,
        LV_EVENT_DELETE,
        NULL
    );

    lv_obj_set_size(
        button,
        TOOLBAR_BUTTON_SIZE,
        TOOLBAR_BUTTON_SIZE
    );

    lv_obj_set_style_radius(
        button,
        10,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        button,
        lv_color_hex(
            TOOLBAR_COLOR_BACKGROUND
        ),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        button,
        lv_color_hex(
            TOOLBAR_COLOR_PRESSED
        ),
        LV_PART_MAIN | LV_STATE_PRESSED
    );

    lv_obj_set_style_bg_opa(
        button,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_set_style_shadow_width(
        button,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        button,
        0,
        LV_PART_MAIN
    );

    if (callback != NULL) {
        lv_obj_add_event_cb(
            button,
            toolbar_button_event_cb,
            LV_EVENT_CLICKED,
            NULL
        );
    } else {
        lv_obj_remove_flag(
            button,
            LV_OBJ_FLAG_CLICKABLE
        );
    }

    lv_obj_t *image =
        lv_image_create(button);

    if (image == NULL) {
        lv_obj_delete(button);
        return NULL;
    }

    context->image = image;

    lv_image_set_src(
        image,
        image_source
    );

    lv_obj_center(image);

    lv_obj_set_style_image_recolor(
        image,
        lv_color_hex(
            TOOLBAR_ICON_COLOR_DEFAULT
        ),
        LV_PART_MAIN
    );

    lv_obj_set_style_image_recolor_opa(
        image,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    return button;
}

static lv_obj_t *toolbar_text_button_create(
    lv_obj_t *parent,
    const char *text,
    lv_coord_t width,
    toolbar_action_cb_t callback,
    lv_obj_t **out_label
)
{
    if ((parent == NULL) ||
        (text == NULL) ||
        (out_label == NULL)) {

        return NULL;
    }

    *out_label = NULL;

    toolbar_button_context_t *context =
        calloc(
            1U,
            sizeof(*context)
        );

    if (context == NULL) {
        return NULL;
    }

    context->action = callback;

    lv_obj_t *button =
        lv_button_create(parent);

    if (button == NULL) {
        free(context);
        return NULL;
    }

    lv_obj_set_user_data(
        button,
        context
    );

    lv_obj_add_event_cb(
        button,
        toolbar_button_delete_event_cb,
        LV_EVENT_DELETE,
        NULL
    );

    lv_obj_set_size(
        button,
        width,
        TOOLBAR_STATUS_BUTTON_HEIGHT
    );

    lv_obj_set_style_radius(
        button,
        8,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        button,
        lv_color_hex(
            TOOLBAR_COLOR_BACKGROUND
        ),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        button,
        lv_color_hex(
            TOOLBAR_COLOR_PRESSED
        ),
        LV_PART_MAIN | LV_STATE_PRESSED
    );

    lv_obj_set_style_bg_opa(
        button,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_set_style_shadow_width(
        button,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_hor(
        button,
        TOOLBAR_STATUS_BUTTON_PADDING_HORIZONTAL,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_ver(
        button,
        TOOLBAR_STATUS_BUTTON_PADDING_VERTICAL,
        LV_PART_MAIN
    );

    if (callback != NULL) {
        lv_obj_add_event_cb(
            button,
            toolbar_button_event_cb,
            LV_EVENT_CLICKED,
            NULL
        );
    } else {
        lv_obj_remove_flag(
            button,
            LV_OBJ_FLAG_CLICKABLE
        );
    }

    lv_obj_t *label =
        lv_label_create(button);

    if (label == NULL) {
        lv_obj_delete(button);
        return NULL;
    }

    context->label = label;

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
        lv_color_hex(
            TOOLBAR_ICON_COLOR_INACTIVE
        ),
        LV_PART_MAIN
    );

    lv_obj_center(
        label
    );

    *out_label = label;

    return button;
}

static void toolbar_set_text_color(
    lv_obj_t *label,
    uint32_t color
)
{
    if (label == NULL) {
        return;
    }

    lv_obj_set_style_text_color(
        label,
        lv_color_hex(color),
        LV_PART_MAIN
    );

    lv_obj_invalidate(
        label
    );
}

toolbar_t toolbar_create(
    lv_obj_t *parent,
    const toolbar_config_t *config
)
{
    toolbar_t result = {0};

    if (parent == NULL || config == NULL) {
        return result;
    }

    lv_obj_t *toolbar =
        lv_obj_create(parent);

    if (toolbar == NULL) {
        return result;
    }

    result.root = toolbar;

    lv_obj_set_size(
        toolbar,
        LV_PCT(100),
        TOOLBAR_HEIGHT
    );

    lv_obj_align(
        toolbar,
        LV_ALIGN_TOP_MID,
        0,
        0
    );

    lv_obj_remove_flag(
        toolbar,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_style_border_width(
        toolbar,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_radius(
        toolbar,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_left(
        toolbar,
        8,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_right(
        toolbar,
        8,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_top(
        toolbar,
        4,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_bottom(
        toolbar,
        4,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        toolbar,
        lv_color_hex(0x20252A),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        toolbar,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    /*
     * Left-side container for the back button.
     * This is a fixed-size container that holds the back button.
     */

    lv_obj_t *left_container =
        lv_obj_create(toolbar);

    lv_obj_set_size(
        left_container,
        LV_SIZE_CONTENT,
        LV_PCT(100)
    );

    lv_obj_align(
        left_container,
        LV_ALIGN_LEFT_MID,
        8,
        0
    );

    lv_obj_remove_flag(
        left_container,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_style_border_width(
        left_container,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        left_container,
        LV_OPA_TRANSP,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        left_container,
        0,
        LV_PART_MAIN
    );

    if (config->left_icon != NULL) {
        (void)toolbar_button_create(
            left_container,
            config->left_icon,
            config->left_action
        );
    }

    /*
     * Toolbar title.
     */
    lv_obj_t *title =
        lv_label_create(toolbar);

    lv_label_set_text(
        title,
        config->title != NULL
            ? config->title
            : ""
    );

    lv_obj_set_style_text_font(
        title,
        &lv_font_montserrat_20,
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        title,
        lv_color_hex(0xFFFFFF),
        LV_PART_MAIN
    );

    lv_obj_align(
        title,
        LV_ALIGN_LEFT_MID,
        config->left_icon != NULL
            ? 60
            : 12,
        0
    );

    /*
     * Right-side status container.
     */
    lv_obj_t *status_container =
        lv_obj_create(toolbar);

    lv_obj_set_size(
        status_container,
        TOOLBAR_STATUS_CONTAINER_WIDTH,
        LV_PCT(100)
    );

    lv_obj_remove_flag(
        status_container,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_style_border_width(
        status_container,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_radius(
        status_container,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        status_container,
        LV_OPA_TRANSP,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        status_container,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_column(
        status_container,
        4,
        LV_PART_MAIN
    );

    lv_obj_set_flex_flow(
        status_container,
        LV_FLEX_FLOW_ROW
    );

    lv_obj_set_flex_align(
        status_container,
        LV_FLEX_ALIGN_END,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    if (config->show_usb_status) {
        result.usb_button =
            toolbar_text_button_create(
                status_container,
                "USB",
                TOOLBAR_USB_BUTTON_WIDTH,
                config->usb_action,
                &result.usb_label
            );
    }

    if (config->show_wifi_status) {
        result.wifi_button =
            toolbar_text_button_create(
                status_container,
                "WiFi 0",
                TOOLBAR_WIFI_BUTTON_WIDTH,
                config->wifi_action,
                &result.wifi_label
            );
    }

    if (config->show_cpu_status) {
        result.cpu_button =
            toolbar_text_button_create(
                status_container,
                "CPU 0%",
                TOOLBAR_CPU_BUTTON_WIDTH,
                config->cpu_action,
                &result.cpu_label
            );
    }

    if (config->show_ota_status) {
        result.ota_button =
            toolbar_button_create(
                status_container,
                &icons8_alarm_clock_32,
                config->ota_action
            );
    }

    if (config->show_sd_status) {
        result.sd_button =
            toolbar_button_create(
                status_container,
                &icons8_micro_sd_32,
                config->sd_action
            );
    }

    /*
     * Settings button.
     */
    if (config->right_icon != NULL) {
        (void)toolbar_button_create(
            status_container,
            config->right_icon,
            config->right_action
        );
    }

    /*
     * Complete the status-control layout before aligning the container.
     */
    lv_obj_update_layout(
        status_container
    );

    lv_obj_align(
        status_container,
        LV_ALIGN_RIGHT_MID,
        0,
        0
    );

    lv_obj_move_foreground(
        status_container
    );

    return result;
}

static void toolbar_set_button_icon_color(
    lv_obj_t *button,
    lv_color_t color
)
{
    if (button == NULL) {
        return;
    }

    toolbar_button_context_t *context =
        lv_obj_get_user_data(button);

    if ((context == NULL) ||
        (context->image == NULL)) {

        return;
    }

    lv_obj_set_style_image_recolor(
        context->image,
        color,
        LV_PART_MAIN
    );

    lv_obj_set_style_image_recolor_opa(
        context->image,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_invalidate(
        context->image
    );
}

void toolbar_set_sd_mounted(
    toolbar_t *toolbar,
    bool mounted
)
{
    if (toolbar == NULL) {
        return;
    }

    toolbar_set_button_icon_color(
        toolbar->sd_button,
        lv_color_hex(
            mounted
                ? TOOLBAR_ICON_COLOR_SD_MOUNTED
                : TOOLBAR_ICON_COLOR_INACTIVE
        )
    );
}

void toolbar_set_ota_available(
    toolbar_t *toolbar,
    bool available
)
{
    if (toolbar == NULL) {
        return;
    }

    toolbar_set_button_icon_color(
        toolbar->ota_button,
        lv_color_hex(
            available
                ? TOOLBAR_ICON_COLOR_OTA_READY
                : TOOLBAR_ICON_COLOR_INACTIVE
        )
    );
}

void toolbar_set_usb_status(
    toolbar_t *toolbar,
    bool started,
    bool host_connected
)
{
    if ((toolbar == NULL) ||
        (toolbar->usb_label == NULL)) {

        return;
    }

    lv_label_set_text(
        toolbar->usb_label,
        "USB"
    );

    uint32_t color =
        TOOLBAR_ICON_COLOR_INACTIVE;

    if (started) {
        color =
            host_connected
                ? TOOLBAR_STATUS_COLOR_ACTIVE
                : TOOLBAR_ICON_COLOR_DEFAULT;
    }

    toolbar_set_text_color(
        toolbar->usb_label,
        color
    );
}

void toolbar_set_wifi_status(
    toolbar_t *toolbar,
    bool started,
    size_t client_count
)
{
    if ((toolbar == NULL) ||
        (toolbar->wifi_label == NULL)) {

        return;
    }

    lv_label_set_text_fmt(
        toolbar->wifi_label,
        "WiFi %u",
        (unsigned int)client_count
    );

    uint32_t color =
        TOOLBAR_ICON_COLOR_INACTIVE;

    if (started) {
        color =
            client_count > 0U
                ? TOOLBAR_STATUS_COLOR_ACTIVE
                : TOOLBAR_ICON_COLOR_DEFAULT;
    }

    toolbar_set_text_color(
        toolbar->wifi_label,
        color
    );
}

void toolbar_set_cpu_usage(
    toolbar_t *toolbar,
    uint8_t cpu_usage
)
{
    if ((toolbar == NULL) ||
        (toolbar->cpu_label == NULL)) {

        return;
    }

    if (cpu_usage > 100U) {
        cpu_usage = 100U;
    }

    lv_label_set_text_fmt(
        toolbar->cpu_label,
        "CPU %u%%",
        (unsigned int)cpu_usage
    );

    uint32_t color =
        TOOLBAR_ICON_COLOR_DEFAULT;

    if (cpu_usage >= 90U) {
        color =
            TOOLBAR_STATUS_COLOR_CRITICAL;
    } else if (cpu_usage >= 70U) {
        color =
            TOOLBAR_STATUS_COLOR_WARNING;
    }

    toolbar_set_text_color(
        toolbar->cpu_label,
        color
    );
}
