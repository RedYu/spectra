#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct modal_dialog_t modal_dialog_t;

/**
 * @brief Modal dialog button callback.
 *
 * @param dialog Dialog instance that triggered the callback.
 */
typedef void (*modal_dialog_action_cb_t)(
    modal_dialog_t *dialog
);

typedef struct
{
    const char *title;
    const char *message;

    /**
     * Optional icon displayed above the message.
     * Set to NULL to create a dialog without an icon.
     */
    const lv_image_dsc_t *icon;

    /**
     * Optional primary button.
     *
     * If primary_button_text is NULL, the button is not created.
     */
    const char *primary_button_text;
    modal_dialog_action_cb_t primary_action;
    bool close_on_primary_action;

    /**
     * Optional secondary button.
     *
     * If secondary_button_text is NULL, the button is not created.
     */
    const char *secondary_button_text;
    modal_dialog_action_cb_t secondary_action;
    bool close_on_secondary_action;

    /**
     * Show an optional progress bar.
     */
    bool show_progress_bar;

    /**
     * Initial progress value from 0 to 100.
     */
    uint8_t initial_progress;

    /**
     * Optional text displayed above the progress bar.
     */
    const char *progress_text;

    /**
     * Animate the dialog when it appears.
     */
    bool animate_open;

    /**
     * Allow closing the dialog by pressing the dark overlay.
     */
    bool close_on_overlay_click;

} modal_dialog_config_t;

struct modal_dialog_t
{
    lv_obj_t *overlay;
    lv_obj_t *dialog;

    lv_obj_t *title_label;
    lv_obj_t *content_container;
    lv_obj_t *icon;
    lv_obj_t *message_label;

    lv_obj_t *progress_container;
    lv_obj_t *progress_label;
    lv_obj_t *progress_bar;

    lv_obj_t *button_container;

    lv_obj_t *primary_button;
    lv_obj_t *primary_button_label;

    lv_obj_t *secondary_button;
    lv_obj_t *secondary_button_label;

    modal_dialog_action_cb_t primary_action;
    modal_dialog_action_cb_t secondary_action;

    bool close_on_primary_action;
    bool close_on_secondary_action;
    bool close_on_overlay_click;
};

/**
 * @brief Create and initialize a modal dialog.
 *
 * The dialog is created in the center of the parent object and blocks
 * interaction with all objects behind it until the dialog is closed.
 *
 * The caller must keep the modal_dialog_t instance valid for the entire
 * lifetime of the dialog.
 *
 * @param dialog Dialog instance to initialize.
 * @param parent Parent object, normally lv_screen_active().
 * @param config Dialog configuration.
 *
 * @return true if the dialog was created successfully, false otherwise.
 */
bool modal_dialog_create(
    modal_dialog_t *dialog,
    lv_obj_t *parent,
    const modal_dialog_config_t *config
);

/**
 * @brief Close and destroy a modal dialog.
 *
 * @param dialog Dialog instance.
 */
void modal_dialog_close(
    modal_dialog_t *dialog
);

/**
 * @brief Check whether the dialog is currently open.
 *
 * @param dialog Dialog instance.
 *
 * @return true when the dialog exists.
 */
bool modal_dialog_is_open(
    const modal_dialog_t *dialog
);

/**
 * @brief Change the dialog title.
 *
 * @param dialog Dialog instance.
 * @param title New title.
 */
void modal_dialog_set_title(
    modal_dialog_t *dialog,
    const char *title
);

/**
 * @brief Change the dialog message.
 *
 * @param dialog Dialog instance.
 * @param message New message.
 */
void modal_dialog_set_message(
    modal_dialog_t *dialog,
    const char *message
);

/**
 * @brief Change the dialog icon.
 *
 * @param dialog Dialog instance.
 * @param icon New icon. Set to NULL to hide it.
 */
void modal_dialog_set_icon(
    modal_dialog_t *dialog,
    const lv_image_dsc_t *icon
);

/**
 * @brief Show or hide the progress section.
 *
 * @param dialog Dialog instance.
 * @param visible true to show the progress section.
 */
void modal_dialog_set_progress_visible(
    modal_dialog_t *dialog,
    bool visible
);

/**
 * @brief Set progress value.
 *
 * Values greater than 100 are clamped to 100.
 *
 * @param dialog Dialog instance.
 * @param value Progress value from 0 to 100.
 * @param animated true to animate the progress change.
 */
void modal_dialog_set_progress(
    modal_dialog_t *dialog,
    uint8_t value,
    bool animated
);

/**
 * @brief Change the progress description text.
 *
 * @param dialog Dialog instance.
 * @param text New progress text.
 */
void modal_dialog_set_progress_text(
    modal_dialog_t *dialog,
    const char *text
);

/**
 * @brief Enable or disable the primary button.
 *
 * @param dialog Dialog instance.
 * @param enabled true to enable the button.
 */
void modal_dialog_set_primary_enabled(
    modal_dialog_t *dialog,
    bool enabled
);

/**
 * @brief Enable or disable the secondary button.
 *
 * @param dialog Dialog instance.
 * @param enabled true to enable the button.
 */
void modal_dialog_set_secondary_enabled(
    modal_dialog_t *dialog,
    bool enabled
);

/**
 * @brief Change the primary button text.
 *
 * @param dialog Dialog instance.
 * @param text New button text.
 */
void modal_dialog_set_primary_text(
    modal_dialog_t *dialog,
    const char *text
);

/**
 * @brief Change the secondary button text.
 *
 * @param dialog Dialog instance.
 * @param text New button text.
 */
void modal_dialog_set_secondary_text(
    modal_dialog_t *dialog,
    const char *text
);

#ifdef __cplusplus
}
#endif
