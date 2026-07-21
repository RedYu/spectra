#include "widgets/toolbar.h"

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

static lv_obj_t *toolbar_button_create(
    lv_obj_t *parent,
    const char *icon,
    toolbar_action_cb_t callback
)
{
    if (icon == NULL || callback == NULL) {
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

    lv_obj_t *icon_label =
        lv_label_create(button);

    lv_label_set_text(
        icon_label,
        icon
    );

    lv_obj_set_style_text_color(
        icon_label,
        lv_color_hex(0xFFFFFF),
        LV_PART_MAIN
    );

    lv_obj_set_style_text_font(
        icon_label,
        &lv_font_montserrat_20,
        LV_PART_MAIN
    );

    lv_obj_center(icon_label);

    return button;
}

lv_obj_t *toolbar_create(
    lv_obj_t *parent,
    const toolbar_config_t *config
)
{
    if (parent == NULL || config == NULL) {
        return NULL;
    }

    lv_obj_t *toolbar =
        lv_obj_create(parent);

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
     * Left button.
     */
    lv_obj_t *left_button =
        toolbar_button_create(
            toolbar,
            config->left_icon,
            config->left_action
        );

    if (left_button != NULL) {
        lv_obj_align(
            left_button,
            LV_ALIGN_LEFT_MID,
            0,
            0
        );
    }

    /*
     * Title.
     */
    lv_obj_t *title_label =
        lv_label_create(toolbar);

    lv_label_set_text(
        title_label,
        config->title != NULL
            ? config->title
            : ""
    );

    lv_obj_set_style_text_font(
        title_label,
        &lv_font_montserrat_20,
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        title_label,
        lv_color_hex(0xFFFFFF),
        LV_PART_MAIN
    );

    lv_obj_center(title_label);

    /*
     * Right button.
     */
    lv_obj_t *right_button =
        toolbar_button_create(
            toolbar,
            config->right_icon,
            config->right_action
        );

    if (right_button != NULL) {
        lv_obj_align(
            right_button,
            LV_ALIGN_RIGHT_MID,
            0,
            0
        );
    }

    return toolbar;
}
