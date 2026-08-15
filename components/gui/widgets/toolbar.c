#include "widgets/toolbar.h"

#include <stdlib.h>

#include "assets/gui_images.h"
#include "gui_feedback.h"
#include "gui_styles.h"
#include "gui_theme.h"

#define TOOLBAR_HEIGHT \
    GUI_THEME_TOOLBAR_HEIGHT
#define TOOLBAR_BUTTON_SIZE \
    GUI_THEME_TOOLBAR_BUTTON_SIZE
#define TOOLBAR_STATUS_BUTTON_HEIGHT TOOLBAR_BUTTON_SIZE
#define TOOLBAR_STATUS_CONTAINER_WIDTH  (340)

#define TOOLBAR_USB_BUTTON_WIDTH   (46)
#define TOOLBAR_WIFI_BUTTON_WIDTH  (60)
#define TOOLBAR_CPU_BUTTON_WIDTH   (82)

#define TOOLBAR_STATUS_BUTTON_PADDING_HORIZONTAL \
    GUI_THEME_SPACE_SM
#define TOOLBAR_STATUS_BUTTON_PADDING_VERTICAL \
    GUI_THEME_SPACE_XS

#define TOOLBAR_INTERNET_INDICATOR_SIZE   (10)
#define TOOLBAR_INTERNET_INDICATOR_GAP \
    GUI_THEME_SPACE_SM

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

static void toolbar_apply_button_style(
    lv_obj_t *button,
    const gui_theme_t *theme
)
{
    if ((button == NULL) ||
        (theme == NULL)) {

        return;
    }

    lv_obj_add_style(
        button,
        gui_styles_button_base(),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        button,
        theme->colors.toolbar_control,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        button,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        button,
        theme->colors.toolbar_control_pressed,
        LV_PART_MAIN | LV_STATE_PRESSED
    );

    lv_obj_set_style_bg_opa(
        button,
        LV_OPA_COVER,
        LV_PART_MAIN | LV_STATE_PRESSED
    );

    lv_obj_set_style_border_width(
        button,
        0,
        LV_PART_MAIN
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

    const gui_theme_t *theme =
        gui_theme_get();

    if (theme == NULL) {
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

    toolbar_apply_button_style(
        button,
        theme
    );

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

    lv_obj_set_style_pad_all(
        button,
        0,
        LV_PART_MAIN
    );

    if (callback != NULL) {
        gui_feedback_attach(
            button
        );

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
        theme->colors.toolbar_foreground,
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

    const gui_theme_t *theme =
        gui_theme_get();

    if (theme == NULL) {
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

    toolbar_apply_button_style(
        button,
        theme
    );

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
        gui_feedback_attach(
            button
        );

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

    lv_obj_add_style(
        label,
        gui_styles_text_small(),
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        label,
        theme->colors.toolbar_inactive,
        LV_PART_MAIN
    );

    context->label = label;

    lv_label_set_text(
        label,
        text
    );

    lv_obj_center(
        label
    );

    *out_label = label;

    return button;
}

static void toolbar_set_text_color(
    lv_obj_t *label,
    lv_color_t color
)
{
    if (label == NULL) {
        return;
    }

    lv_obj_set_style_text_color(
        label,
        color,
        LV_PART_MAIN
    );

    lv_obj_invalidate(label);
}

toolbar_t toolbar_create(
    lv_obj_t *parent,
    const toolbar_config_t *config
)
{
    toolbar_t result = {0};

    if ((parent == NULL) ||
        (config == NULL) ||
        !gui_theme_is_initialized() ||
        !gui_styles_is_initialized()) {

        return result;
    }

    const gui_theme_t *theme =
        gui_theme_get();

    if (theme == NULL) {
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
        GUI_THEME_SPACE_SM,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_right(
        toolbar,
        GUI_THEME_SPACE_SM,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_top(
        toolbar,
        GUI_THEME_SPACE_XS,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_bottom(
        toolbar,
        GUI_THEME_SPACE_XS,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        toolbar,
        theme->colors.toolbar_background,
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

    if (left_container == NULL) {
        lv_obj_delete(toolbar);

        toolbar_t empty = {0};
        return empty;
    }

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
     * Title container with an optional Internet connectivity
     * indicator.
     */
    lv_obj_t *title_container =
        lv_obj_create(toolbar);

    if (title_container == NULL) {
        lv_obj_delete(toolbar);

        toolbar_t empty = {0};
        return empty;
    }

    lv_obj_set_size(
        title_container,
        LV_SIZE_CONTENT,
        LV_SIZE_CONTENT
    );

    lv_obj_align(
        title_container,
        LV_ALIGN_LEFT_MID,
        config->left_icon != NULL
            ? 60
            : 12,
        0
    );

    lv_obj_remove_flag(
        title_container,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_style_border_width(
        title_container,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        title_container,
        LV_OPA_TRANSP,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        title_container,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_column(
        title_container,
        TOOLBAR_INTERNET_INDICATOR_GAP,
        LV_PART_MAIN
    );

    lv_obj_set_flex_flow(
        title_container,
        LV_FLEX_FLOW_ROW
    );

    lv_obj_set_flex_align(
        title_container,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    lv_obj_t *title =
        lv_label_create(title_container);

    if (title == NULL) {
        lv_obj_delete(toolbar);

        toolbar_t empty = {0};
        return empty;
    }

    lv_label_set_text(
        title,
        config->title != NULL
            ? config->title
            : ""
    );

    lv_obj_add_style(
        title,
        gui_styles_text_title(),
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        title,
        theme->colors.toolbar_foreground,
        LV_PART_MAIN
    );

    if (config->show_internet_status) {
        result.internet_indicator =
            lv_obj_create(title_container);

        if (result.internet_indicator != NULL) {
            lv_obj_set_size(
                result.internet_indicator,
                TOOLBAR_INTERNET_INDICATOR_SIZE,
                TOOLBAR_INTERNET_INDICATOR_SIZE
            );

            lv_obj_set_style_radius(
                result.internet_indicator,
                LV_RADIUS_CIRCLE,
                LV_PART_MAIN
            );

            lv_obj_set_style_bg_color(
                result.internet_indicator,
                theme->colors.success,
                LV_PART_MAIN
            );

            lv_obj_set_style_bg_opa(
                result.internet_indicator,
                LV_OPA_COVER,
                LV_PART_MAIN
            );

            lv_obj_set_style_border_width(
                result.internet_indicator,
                0,
                LV_PART_MAIN
            );

            lv_obj_set_style_pad_all(
                result.internet_indicator,
                0,
                LV_PART_MAIN
            );

            lv_obj_remove_flag(
                result.internet_indicator,
                LV_OBJ_FLAG_SCROLLABLE
            );

            lv_obj_remove_flag(
                result.internet_indicator,
                LV_OBJ_FLAG_CLICKABLE
            );

            /*
             * Keep the indicator hidden until Internet access has
             * actually been confirmed.
             */
            lv_obj_add_flag(
                result.internet_indicator,
                LV_OBJ_FLAG_HIDDEN
            );
        }
    }

    /*
     * Right-side status container.
     */
    lv_obj_t *status_container =
        lv_obj_create(toolbar);

    if (status_container == NULL) {
        lv_obj_delete(toolbar);

        toolbar_t empty = {0};
        return empty;
    }

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

    const gui_theme_t *theme =
        gui_theme_get();

    if (theme == NULL) {
        return;
    }

    toolbar_set_button_icon_color(
        toolbar->sd_button,
        mounted
            ? theme->colors.control_accent
            : theme->colors.toolbar_inactive
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

    const gui_theme_t *theme =
        gui_theme_get();

    if (theme == NULL) {
        return;
    }

    toolbar_set_button_icon_color(
        toolbar->ota_button,
        available
            ? theme->colors.warning
            : theme->colors.toolbar_inactive
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

    const gui_theme_t *theme =
        gui_theme_get();

    if (theme == NULL) {
        return;
    }

    lv_color_t color =
        theme->colors.toolbar_inactive;

    if (started) {
        color =
            host_connected
                ? theme->colors.control_accent
                : theme->colors.toolbar_foreground;
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

    const gui_theme_t *theme =
        gui_theme_get();

    if (theme == NULL) {
        return;
    }

    lv_color_t color =
        theme->colors.toolbar_inactive;

    if (started) {
        color =
            client_count > 0U
                ? theme->colors.control_accent
                : theme->colors.toolbar_foreground;
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

    const gui_theme_t *theme =
        gui_theme_get();

    if (theme == NULL) {
        return;
    }

    lv_color_t color =
        theme->colors.toolbar_foreground;

    if (cpu_usage >= 90U) {
        color = theme->colors.danger;
    } else if (cpu_usage >= 70U) {
        color = theme->colors.warning;
    }

    toolbar_set_text_color(
        toolbar->cpu_label,
        color
    );
}

void toolbar_set_internet_available(
    toolbar_t *toolbar,
    bool available
)
{
    if ((toolbar == NULL) ||
        (toolbar->internet_indicator == NULL)) {

        return;
    }

    if (available) {
        lv_obj_remove_flag(
            toolbar->internet_indicator,
            LV_OBJ_FLAG_HIDDEN
        );
    } else {
        lv_obj_add_flag(
            toolbar->internet_indicator,
            LV_OBJ_FLAG_HIDDEN
        );
    }
}
