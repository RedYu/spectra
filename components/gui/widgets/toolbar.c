#include "widgets/toolbar.h"

#include "assets/gui_images.h"
#include "esp_log.h"

#define TOOLBAR_HEIGHT       56
#define TOOLBAR_BUTTON_SIZE  44

static void toolbar_button_event_cb(
    lv_event_t *event
)
{
    toolbar_action_cb_t callback =
        lv_event_get_user_data(event);

    if (callback != NULL) {
        callback();
    }
}

static lv_obj_t *toolbar_status_icon_create(
    lv_obj_t *parent,
    const lv_image_dsc_t *image_source
)
{
    if (parent == NULL || image_source == NULL) {
        return NULL;
    }

    /*
     * Permanent slot participating in flex layout.
     */
    lv_obj_t *slot =
        lv_obj_create(parent);

    lv_obj_set_size(
        slot,
        TOOLBAR_BUTTON_SIZE,
        TOOLBAR_BUTTON_SIZE
    );

    lv_obj_remove_flag(
        slot,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_remove_flag(
        slot,
        LV_OBJ_FLAG_CLICKABLE
    );

    lv_obj_set_style_border_width(
        slot,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_radius(
        slot,
        10,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        slot,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        slot,
        lv_color_hex(0x343B42),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        slot,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    /*
     * Image inside the permanent slot.
     */
    lv_obj_t *image =
        lv_image_create(slot);

    lv_image_set_src(
        image,
        image_source
    );

    lv_obj_center(image);

    lv_obj_set_style_image_recolor(
        image,
        lv_color_hex(0x4B77D1),
        LV_PART_MAIN
    );

    lv_obj_set_style_image_recolor_opa(
        image,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    /*
     * Store the image pointer in the slot.
     */
    lv_obj_set_user_data(
        slot,
        image
    );

    /*
     * Hide only the image, not the flex-layout slot.
     */
    lv_obj_add_flag(
        image,
        LV_OBJ_FLAG_HIDDEN
    );

    return slot;
}

static lv_obj_t *toolbar_button_create(
    lv_obj_t *parent,
    const lv_image_dsc_t *image_source,
    toolbar_action_cb_t callback
)
{
    if (image_source == NULL || callback == NULL) {
        return NULL;
    }

    lv_obj_t *button =
        lv_button_create(parent);

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
        lv_color_hex(0x343B42),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        button,
        lv_color_hex(0x4B77D1),
        LV_PART_MAIN | LV_STATE_PRESSED
    );

    lv_obj_set_style_shadow_width(
        button,
        0,
        LV_PART_MAIN
    );

    lv_obj_add_event_cb(
        button,
        toolbar_button_event_cb,
        LV_EVENT_CLICKED,
        callback
    );

    lv_obj_t *image =
        lv_image_create(button);

    lv_image_set_src(
        image,
        image_source
    );

    lv_obj_center(image);

    lv_obj_set_style_image_recolor(
        image,
        lv_color_hex(0xFFFFFF),
        LV_PART_MAIN
    );

    lv_obj_set_style_image_recolor_opa(
        image,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    return button;
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

    if (config->left_icon != NULL &&
        config->left_action != NULL) {

        toolbar_button_create(
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

    lv_obj_center(title);

    /*
     * Right-side status container.
     */
    lv_obj_t *status_container =
        lv_obj_create(toolbar);

    lv_obj_set_size(
        status_container,
        180,
        TOOLBAR_HEIGHT
    );

    lv_obj_align(
        status_container,
        LV_ALIGN_RIGHT_MID,
        -8,
        0
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
        8,
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

    if (config->show_ota_status) {
        result.ota_icon =
            toolbar_status_icon_create(
                status_container,
                &icons8_alarm_clock_32
            );
    }

    if (config->show_sd_status) {
        result.sd_icon =
            toolbar_status_icon_create(
                status_container,
                &icons8_micro_sd_32
            );
    }
    /*
     * Settings button.
     */
    if (config->right_icon != NULL &&
        config->right_action != NULL) {

            toolbar_button_create(
                status_container,
                config->right_icon,
                config->right_action
        );
    }

    return result;
}

static void toolbar_set_status_visible(
    lv_obj_t *slot,
    bool visible
)
{
    if (slot == NULL) {
        return;
    }

    lv_obj_t *image =
        lv_obj_get_user_data(slot);

    if (image == NULL) {
        return;
    }

    if (visible) {
        lv_obj_remove_flag(
            image,
            LV_OBJ_FLAG_HIDDEN
        );
    } else {
        lv_obj_add_flag(
            image,
            LV_OBJ_FLAG_HIDDEN
        );
    }

    lv_obj_invalidate(slot);
}

void toolbar_set_sd_mounted(
    toolbar_t *toolbar,
    bool mounted
)
{
    if (toolbar == NULL) {
        return;
    }

    toolbar_set_status_visible(
        toolbar->sd_icon,
        mounted
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

    toolbar_set_status_visible(
        toolbar->ota_icon,
        available
    );
}

