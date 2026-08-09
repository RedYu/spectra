#include "widgets/modal_dialog.h"

#include <stddef.h>

#include "gui_styles.h"
#include "gui_theme.h"

#define MODAL_DIALOG_WIDTH   360
#define MODAL_DIALOG_HEIGHT  260

#define MODAL_DIALOG_CONTENT_GAP        10

#define MODAL_DIALOG_BUTTON_HEIGHT      42
#define MODAL_DIALOG_BUTTON_GAP         10

#define MODAL_DIALOG_ANIMATION_TIME_MS  180
#define MODAL_DIALOG_SCALE_START        220
#define MODAL_DIALOG_SCALE_END          256

static void modal_dialog_delete_event_cb(
    lv_event_t *event
);

static void modal_dialog_primary_event_cb(
    lv_event_t *event
);

static void modal_dialog_secondary_event_cb(
    lv_event_t *event
);

static void modal_dialog_overlay_event_cb(
    lv_event_t *event
);

static void modal_dialog_open_animation_cb(
    void *object,
    int32_t value
);

static lv_obj_t *modal_dialog_button_create(
    lv_obj_t *parent,
    const char *text,
    bool primary,
    lv_event_cb_t event_callback,
    modal_dialog_t *dialog,
    lv_obj_t **label_out
);

static void modal_dialog_reset(
    modal_dialog_t *dialog
);

static void modal_dialog_apply_button_enabled(
    lv_obj_t *button,
    bool enabled
);

bool modal_dialog_create(
    modal_dialog_t *dialog,
    lv_obj_t *parent,
    const modal_dialog_config_t *config
)
{
    if ((dialog == NULL) ||
        (parent == NULL) ||
        (config == NULL)) {

        return false;
    }

    if (!gui_styles_is_initialized()) {
        return false;
    }

    const gui_theme_t *theme =
        gui_theme_get();

    if (theme == NULL) {
        return false;
    }

    /*
     * The dialog instance must be initialized to zero before its
     * first use. Do not replace an existing open dialog.
     */
    if (dialog->overlay != NULL) {
        return false;
    }

    modal_dialog_reset(
        dialog
    );

    dialog->primary_action =
        config->primary_action;

    dialog->secondary_action =
        config->secondary_action;

    dialog->close_on_primary_action =
        config->close_on_primary_action;

    dialog->close_on_secondary_action =
        config->close_on_secondary_action;

    dialog->close_on_overlay_click =
        config->close_on_overlay_click;

    dialog->overlay =
        lv_obj_create(parent);

    if (dialog->overlay == NULL) {
        modal_dialog_reset(
            dialog
        );

        return false;
    }

    lv_obj_set_size(
        dialog->overlay,
        LV_PCT(100),
        LV_PCT(100)
    );

    lv_obj_align(
        dialog->overlay,
        LV_ALIGN_CENTER,
        0,
        0
    );

    lv_obj_remove_flag(
        dialog->overlay,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_style_border_width(
        dialog->overlay,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_radius(
        dialog->overlay,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        dialog->overlay,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        dialog->overlay,
        theme->modal.overlay,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        dialog->overlay,
        LV_OPA_60,
        LV_PART_MAIN
    );

    lv_obj_add_flag(
        dialog->overlay,
        LV_OBJ_FLAG_CLICKABLE
    );

    lv_obj_move_foreground(
        dialog->overlay
    );

    lv_obj_add_event_cb(
        dialog->overlay,
        modal_dialog_overlay_event_cb,
        LV_EVENT_CLICKED,
        dialog
    );

    /*
     * Clear the context when the overlay is deleted together with its
     * parent screen or directly through modal_dialog_close().
     */
    lv_obj_add_event_cb(
        dialog->overlay,
        modal_dialog_delete_event_cb,
        LV_EVENT_DELETE,
        dialog
    );

    /*
     * Dialog window.
     */
    dialog->dialog =
        lv_obj_create(
            dialog->overlay
        );

    if (dialog->dialog == NULL) {
        lv_obj_delete(
            dialog->overlay
        );

        modal_dialog_reset(
            dialog
        );

        return false;
    }

    lv_obj_add_style(
        dialog->dialog,
        gui_styles_card(),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        dialog->dialog,
        theme->modal.background,
        LV_PART_MAIN
    );

    lv_obj_set_style_border_color(
        dialog->dialog,
        theme->modal.border,
        LV_PART_MAIN
    );

    lv_obj_set_size(
        dialog->dialog,
        MODAL_DIALOG_WIDTH,
        MODAL_DIALOG_HEIGHT
    );

    lv_obj_center(
        dialog->dialog
    );

    lv_obj_remove_flag(
        dialog->dialog,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_add_flag(
        dialog->dialog,
        LV_OBJ_FLAG_EVENT_BUBBLE
    );

    lv_obj_set_style_pad_row(
        dialog->dialog,
        MODAL_DIALOG_CONTENT_GAP,
        LV_PART_MAIN
    );

    lv_obj_set_flex_flow(
        dialog->dialog,
        LV_FLEX_FLOW_COLUMN
    );

    lv_obj_set_flex_align(
        dialog->dialog,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    /*
     * Title.
     */
    dialog->title_label =
        lv_label_create(
            dialog->dialog
        );

    lv_obj_add_style(
        dialog->title_label,
        gui_styles_text_title(),
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        dialog->title_label,
        theme->modal.title,
        LV_PART_MAIN
    );

    lv_label_set_text(
        dialog->title_label,
        config->title != NULL
            ? config->title
            : ""
    );

    lv_label_set_long_mode(
        dialog->title_label,
        LV_LABEL_LONG_DOT
    );

    lv_obj_set_width(
        dialog->title_label,
        LV_PCT(100)
    );

    lv_obj_set_style_text_align(
        dialog->title_label,
        LV_TEXT_ALIGN_CENTER,
        LV_PART_MAIN
    );

    /*
     * Main content area.
     *
     * It expands to fill all remaining space between the title
     * and the button section.
     */
    dialog->content_container =
        lv_obj_create(
            dialog->dialog
        );

    lv_obj_set_width(
        dialog->content_container,
        LV_PCT(100)
    );

    lv_obj_set_flex_grow(
        dialog->content_container,
        1
    );

    lv_obj_remove_flag(
        dialog->content_container,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_style_border_width(
        dialog->content_container,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        dialog->content_container,
        LV_OPA_TRANSP,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        dialog->content_container,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_row(
        dialog->content_container,
        MODAL_DIALOG_CONTENT_GAP,
        LV_PART_MAIN
    );

    lv_obj_set_flex_flow(
        dialog->content_container,
        LV_FLEX_FLOW_COLUMN
    );

    lv_obj_set_flex_align(
        dialog->content_container,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    /*
     * Optional icon.
     */
    dialog->icon =
        lv_image_create(
            dialog->content_container
        );

    if (config->icon != NULL) {
        lv_image_set_src(
            dialog->icon,
            config->icon
        );

        lv_obj_set_style_image_recolor(
            dialog->icon,
            theme->modal.title,
            LV_PART_MAIN
        );

        lv_obj_set_style_image_recolor_opa(
            dialog->icon,
            LV_OPA_COVER,
            LV_PART_MAIN
        );
    } else {
        lv_obj_add_flag(
            dialog->icon,
            LV_OBJ_FLAG_HIDDEN
        );
    }

    /*
     * Message text.
     */
    dialog->message_label =
        lv_label_create(
            dialog->content_container
        );

    lv_obj_add_style(
        dialog->message_label,
        gui_styles_text_muted(),
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        dialog->message_label,
        theme->modal.text,
        LV_PART_MAIN
    );

    lv_label_set_text(
        dialog->message_label,
        config->message != NULL
            ? config->message
            : ""
    );

    lv_label_set_long_mode(
        dialog->message_label,
        LV_LABEL_LONG_WRAP
    );

    lv_obj_set_width(
        dialog->message_label,
        LV_PCT(100)
    );

    lv_obj_set_style_text_align(
        dialog->message_label,
        LV_TEXT_ALIGN_CENTER,
        LV_PART_MAIN
    );

    /*
     * Progress section.
     */
    dialog->progress_container =
        lv_obj_create(
            dialog->dialog
        );

    lv_obj_set_width(
        dialog->progress_container,
        LV_PCT(100)
    );

    lv_obj_set_height(
        dialog->progress_container,
        LV_SIZE_CONTENT
    );

    lv_obj_remove_flag(
        dialog->progress_container,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_style_border_width(
        dialog->progress_container,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        dialog->progress_container,
        LV_OPA_TRANSP,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        dialog->progress_container,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_row(
        dialog->progress_container,
        6,
        LV_PART_MAIN
    );

    lv_obj_set_flex_flow(
        dialog->progress_container,
        LV_FLEX_FLOW_COLUMN
    );

    dialog->progress_label =
        lv_label_create(
            dialog->progress_container
        );

    lv_obj_add_style(
        dialog->progress_label,
        gui_styles_text_muted(),
        LV_PART_MAIN
    );

    lv_obj_set_style_text_color(
        dialog->progress_label,
        theme->modal.progress_text,
        LV_PART_MAIN
    );

    lv_label_set_text(
        dialog->progress_label,
        config->progress_text != NULL
            ? config->progress_text
            : ""
    );

    lv_label_set_long_mode(
        dialog->progress_label,
        LV_LABEL_LONG_DOT
    );

    lv_obj_set_width(
        dialog->progress_label,
        LV_PCT(100)
    );

    lv_obj_set_style_text_align(
        dialog->progress_label,
        LV_TEXT_ALIGN_CENTER,
        LV_PART_MAIN
    );

    dialog->progress_bar =
        lv_bar_create(
            dialog->progress_container
        );

    lv_obj_set_size(
        dialog->progress_bar,
        LV_PCT(100),
        12
    );

    lv_bar_set_range(
        dialog->progress_bar,
        0,
        100
    );

    lv_bar_set_value(
        dialog->progress_bar,
        config->initial_progress > 100
            ? 100
            : config->initial_progress,
        LV_ANIM_OFF
    );

    lv_obj_set_style_radius(
        dialog->progress_bar,
        6,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        dialog->progress_bar,
        theme->modal.progress_background,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        dialog->progress_bar,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_set_style_radius(
        dialog->progress_bar,
        6,
        LV_PART_INDICATOR
    );

    lv_obj_set_style_bg_color(
        dialog->progress_bar,
        theme->modal.progress_indicator,
        LV_PART_INDICATOR
    );

    lv_obj_set_style_bg_opa(
        dialog->progress_bar,
        LV_OPA_COVER,
        LV_PART_INDICATOR
    );

    if (!config->show_progress_bar) {
        lv_obj_add_flag(
            dialog->progress_container,
            LV_OBJ_FLAG_HIDDEN
        );
    }

    /*
     * Button container.
     */
    dialog->button_container =
        lv_obj_create(
            dialog->dialog
        );

    lv_obj_set_width(
        dialog->button_container,
        LV_PCT(100)
    );

    lv_obj_set_height(
        dialog->button_container,
        LV_SIZE_CONTENT
    );

    lv_obj_remove_flag(
        dialog->button_container,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_style_border_width(
        dialog->button_container,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        dialog->button_container,
        LV_OPA_TRANSP,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        dialog->button_container,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_column(
        dialog->button_container,
        MODAL_DIALOG_BUTTON_GAP,
        LV_PART_MAIN
    );

    lv_obj_set_flex_flow(
        dialog->button_container,
        LV_FLEX_FLOW_ROW
    );

    lv_obj_set_flex_align(
        dialog->button_container,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    if (config->secondary_button_text != NULL) {
        dialog->secondary_button =
            modal_dialog_button_create(
                dialog->button_container,
                config->secondary_button_text,
                false,
                modal_dialog_secondary_event_cb,
                dialog,
                &dialog->secondary_button_label
            );
    }

    if (config->primary_button_text != NULL) {
        dialog->primary_button =
            modal_dialog_button_create(
                dialog->button_container,
                config->primary_button_text,
                true,
                modal_dialog_primary_event_cb,
                dialog,
                &dialog->primary_button_label
            );
    }

    if (dialog->primary_button == NULL &&
        dialog->secondary_button == NULL) {

        lv_obj_add_flag(
            dialog->button_container,
            LV_OBJ_FLAG_HIDDEN
        );
    }

    lv_obj_set_style_transform_scale(
        dialog->dialog,
        256,
        LV_PART_MAIN
    );

    lv_obj_set_style_opa(
        dialog->dialog,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    if (config->animate_open) {
        lv_obj_set_style_transform_scale(
            dialog->dialog,
            MODAL_DIALOG_SCALE_START,
            LV_PART_MAIN
        );

        lv_obj_set_style_opa(
            dialog->dialog,
            LV_OPA_0,
            LV_PART_MAIN
        );

        lv_anim_t animation;

        lv_anim_init(
            &animation
        );

        lv_anim_set_var(
            &animation,
            dialog->dialog
        );

        lv_anim_set_values(
            &animation,
            MODAL_DIALOG_SCALE_START,
            MODAL_DIALOG_SCALE_END
        );

        lv_anim_set_time(
            &animation,
            MODAL_DIALOG_ANIMATION_TIME_MS
        );

        lv_anim_set_exec_cb(
            &animation,
            modal_dialog_open_animation_cb
        );

        lv_anim_set_path_cb(
            &animation,
            lv_anim_path_ease_out
        );

        lv_anim_start(
            &animation
        );
    }

    return true;
}

void modal_dialog_close(
    modal_dialog_t *dialog
)
{
    if ((dialog == NULL) ||
        (dialog->overlay == NULL)) {

        return;
    }

    lv_obj_t *overlay =
        dialog->overlay;

    /*
     * Mark the dialog as closed before LVGL starts dispatching delete
     * events. This also prevents recursive closing.
     */
    dialog->overlay = NULL;

    lv_obj_delete(
        overlay
    );

    /*
     * The delete callback normally resets the structure. Reset it
     * again to keep this function safe if callback dispatch changes.
     */
    modal_dialog_reset(
        dialog
    );
}

bool modal_dialog_is_open(
    const modal_dialog_t *dialog
)
{
    return (dialog != NULL) &&
           (dialog->overlay != NULL);
}

void modal_dialog_set_title(
    modal_dialog_t *dialog,
    const char *title
)
{
    if (dialog == NULL ||
        dialog->title_label == NULL) {

        return;
    }

    lv_label_set_text(
        dialog->title_label,
        title != NULL
            ? title
            : ""
    );
}

void modal_dialog_set_message(
    modal_dialog_t *dialog,
    const char *message
)
{
    if (dialog == NULL ||
        dialog->message_label == NULL) {

        return;
    }

    lv_label_set_text(
        dialog->message_label,
        message != NULL
            ? message
            : ""
    );
}

void modal_dialog_set_icon(
    modal_dialog_t *dialog,
    const lv_image_dsc_t *icon
)
{
    if (dialog == NULL ||
        dialog->icon == NULL) {

        return;
    }

    if (icon == NULL) {
        lv_obj_add_flag(
            dialog->icon,
            LV_OBJ_FLAG_HIDDEN
        );

        return;
    }

    lv_image_set_src(
        dialog->icon,
        icon
    );

    lv_obj_remove_flag(
        dialog->icon,
        LV_OBJ_FLAG_HIDDEN
    );
}

void modal_dialog_set_progress_visible(
    modal_dialog_t *dialog,
    bool visible
)
{
    if (dialog == NULL ||
        dialog->progress_container == NULL) {

        return;
    }

    if (visible) {
        lv_obj_remove_flag(
            dialog->progress_container,
            LV_OBJ_FLAG_HIDDEN
        );
    } else {
        lv_obj_add_flag(
            dialog->progress_container,
            LV_OBJ_FLAG_HIDDEN
        );
    }
}

void modal_dialog_set_progress(
    modal_dialog_t *dialog,
    uint8_t value,
    bool animated
)
{
    if (dialog == NULL ||
        dialog->progress_bar == NULL) {

        return;
    }

    if (value > 100) {
        value = 100;
    }

    lv_bar_set_value(
        dialog->progress_bar,
        value,
        animated
            ? LV_ANIM_ON
            : LV_ANIM_OFF
    );
}

void modal_dialog_set_progress_text(
    modal_dialog_t *dialog,
    const char *text
)
{
    if (dialog == NULL ||
        dialog->progress_label == NULL) {

        return;
    }

    lv_label_set_text(
        dialog->progress_label,
        text != NULL
            ? text
            : ""
    );
}

void modal_dialog_set_primary_enabled(
    modal_dialog_t *dialog,
    bool enabled
)
{
    if (dialog == NULL) {
        return;
    }

    modal_dialog_apply_button_enabled(
        dialog->primary_button,
        enabled
    );
}

void modal_dialog_set_secondary_enabled(
    modal_dialog_t *dialog,
    bool enabled
)
{
    if (dialog == NULL) {
        return;
    }

    modal_dialog_apply_button_enabled(
        dialog->secondary_button,
        enabled
    );
}

static void modal_dialog_delete_event_cb(
    lv_event_t *event
)
{
    modal_dialog_t *dialog =
        lv_event_get_user_data(event);

    if (dialog == NULL) {
        return;
    }

    modal_dialog_reset(
        dialog
    );
}

static lv_obj_t *modal_dialog_button_create(
    lv_obj_t *parent,
    const char *text,
    bool primary,
    lv_event_cb_t event_callback,
    modal_dialog_t *dialog,
    lv_obj_t **label_out
)
{
    if ((parent == NULL) ||
        (text == NULL) ||
        (event_callback == NULL) ||
        !gui_styles_is_initialized()) {

        return NULL;
    }

    const gui_theme_t *theme =
        gui_theme_get();

    if (theme == NULL) {
        return NULL;
    }

    lv_obj_t *button =
        lv_button_create(
            parent
        );

    if (button == NULL) {
        return NULL;
    }

    lv_obj_add_style(
        button,
        gui_styles_button_base(),
        LV_PART_MAIN
    );

    const lv_color_t background =
        primary
            ? theme->modal.primary
            : theme->modal.secondary;

    const lv_color_t pressed_background =
        primary
            ? theme->modal.primary_pressed
            : theme->modal.secondary_pressed;

    lv_obj_set_style_bg_color(
        button,
        background,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        button,
        pressed_background,
        LV_PART_MAIN | LV_STATE_PRESSED
    );

    lv_obj_set_style_bg_color(
        button,
        theme->modal.disabled,
        LV_PART_MAIN | LV_STATE_DISABLED
    );

    lv_obj_set_style_border_width(
        button,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        button,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        button,
        LV_OPA_COVER,
        LV_PART_MAIN | LV_STATE_PRESSED
    );

    lv_obj_set_style_bg_opa(
        button,
        LV_OPA_70,
        LV_PART_MAIN | LV_STATE_DISABLED
    );

    lv_obj_set_height(
        button,
        MODAL_DIALOG_BUTTON_HEIGHT
    );

    lv_obj_set_flex_grow(
        button,
        1
    );

    lv_obj_set_style_shadow_width(
        button,
        0,
        LV_PART_MAIN
    );

    lv_obj_add_event_cb(
        button,
        event_callback,
        LV_EVENT_CLICKED,
        dialog
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
        text
    );

    lv_obj_set_style_text_color(
        label,
        theme->modal.title,
        LV_PART_MAIN
    );

    /*
     * The font is inherited from the base button style. The text color is
     * assigned explicitly to preserve the original modal palette.
     */
    lv_obj_center(
        label
    );

    if (label_out != NULL) {
        *label_out = label;
    }

    return button;
}

static void modal_dialog_primary_event_cb(
    lv_event_t *event
)
{
    modal_dialog_t *dialog =
        lv_event_get_user_data(event);

    if ((dialog == NULL) ||
        !modal_dialog_is_open(dialog)) {

        return;
    }

    const modal_dialog_action_cb_t action =
        dialog->primary_action;

    const bool close_after_action =
        dialog->close_on_primary_action;

    /*
     * The user action is intentionally called before automatic close.
     * It may close the dialog itself.
     */
    if (action != NULL) {
        action(
            dialog
        );
    }

    if (close_after_action &&
        modal_dialog_is_open(dialog)) {

        modal_dialog_close(
            dialog
        );
    }
}

static void modal_dialog_secondary_event_cb(
    lv_event_t *event
)
{
    modal_dialog_t *dialog =
        lv_event_get_user_data(event);

    if ((dialog == NULL) ||
        !modal_dialog_is_open(dialog)) {

        return;
    }

    const modal_dialog_action_cb_t action =
        dialog->secondary_action;

    const bool close_after_action =
        dialog->close_on_secondary_action;

    if (action != NULL) {
        action(
            dialog
        );
    }

    if (close_after_action &&
        modal_dialog_is_open(dialog)) {

        modal_dialog_close(
            dialog
        );
    }
}

static void modal_dialog_overlay_event_cb(
    lv_event_t *event
)
{
    modal_dialog_t *dialog =
        lv_event_get_user_data(event);

    if ((dialog == NULL) ||
        !modal_dialog_is_open(dialog) ||
        !dialog->close_on_overlay_click) {

        return;
    }

    lv_obj_t *target =
        lv_event_get_target_obj(event);

    lv_obj_t *current_target =
        lv_event_get_current_target_obj(event);

    /*
     * Events from child controls may bubble to the overlay. Close only
     * when the overlay itself was clicked.
     */
    if (target == current_target) {
        modal_dialog_close(
            dialog
        );
    }
}

static void modal_dialog_open_animation_cb(
    void *object,
    int32_t value
)
{
    lv_obj_t *dialog =
        (lv_obj_t *)object;

    if (dialog == NULL) {
        return;
    }

    lv_obj_set_style_transform_scale(
        dialog,
        value,
        LV_PART_MAIN
    );

    const int32_t opacity =
        lv_map(
            value,
            MODAL_DIALOG_SCALE_START,
            MODAL_DIALOG_SCALE_END,
            LV_OPA_0,
            LV_OPA_COVER
        );

    lv_obj_set_style_opa(
        dialog,
        (lv_opa_t)opacity,
        LV_PART_MAIN
    );
}

static void modal_dialog_reset(
    modal_dialog_t *dialog
)
{
    if (dialog == NULL) {
        return;
    }

    *dialog = (modal_dialog_t){0};
}

static void modal_dialog_apply_button_enabled(
    lv_obj_t *button,
    bool enabled
)
{
    if (button == NULL) {
        return;
    }

    if (enabled) {
        lv_obj_remove_state(
            button,
            LV_STATE_DISABLED
        );

    } else {
        lv_obj_add_state(
            button,
            LV_STATE_DISABLED
        );
    }
}

void modal_dialog_set_primary_text(
    modal_dialog_t *dialog,
    const char *text
)
{
    if (dialog == NULL ||
        dialog->primary_button_label == NULL) {

        return;
    }

    lv_label_set_text(
        dialog->primary_button_label,
        text != NULL
            ? text
            : ""
    );
}

void modal_dialog_set_secondary_text(
    modal_dialog_t *dialog,
    const char *text
)
{
    if (dialog == NULL ||
        dialog->secondary_button_label == NULL) {

        return;
    }

    lv_label_set_text(
        dialog->secondary_button_label,
        text != NULL
            ? text
            : ""
    );
}

