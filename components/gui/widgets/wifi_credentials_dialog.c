/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "widgets/wifi_credentials_dialog.h"

#include <stdio.h>
#include <string.h>

#include "gui_styles.h"
#include "widgets/modal_dialog.h"
#include "gui_theme.h"

#include "wifi_service.h"

#define WIFI_CREDENTIALS_DIALOG_WIDTH          (430)
#define WIFI_CREDENTIALS_DIALOG_HEIGHT         (310)
#define WIFI_CREDENTIALS_OPEN_DIALOG_HEIGHT    (180)

#define WIFI_CREDENTIALS_TEXTAREA_HEIGHT       (38)
#define WIFI_CREDENTIALS_KEYBOARD_HEIGHT       (92)
#define WIFI_CREDENTIALS_MESSAGE_HEIGHT        (36)

typedef struct
{
    modal_dialog_t dialog;

    lv_obj_t *password_textarea;
    lv_obj_t *keyboard;

    wifi_credentials_dialog_submit_cb_t
        submit_callback;

    void *callback_context;

    char ssid[
        WIFI_SERVICE_STA_SSID_MAX_LENGTH
    ];

    bool password_required;

} wifi_credentials_dialog_context_t;

static wifi_credentials_dialog_context_t
    s_context = {0};

static void wifi_credentials_dialog_primary_action(
    modal_dialog_t *dialog
);

static void wifi_credentials_dialog_secondary_action(
    modal_dialog_t *dialog
);

static void wifi_credentials_dialog_keyboard_event_cb(
    lv_event_t *event
);

static void wifi_credentials_dialog_delete_event_cb(
    lv_event_t *event
);

static void wifi_credentials_dialog_reset(void)
{
    memset(
        &s_context,
        0,
        sizeof(s_context)
    );
}

static void wifi_credentials_dialog_set_message(
    const char *status
)
{
    char message[160];

    if ((status != NULL) &&
        (status[0] != '\0')) {

        (void)snprintf(
            message,
            sizeof(message),
            "Network: %s\n%s",
            s_context.ssid,
            status
        );
    } else {
        (void)snprintf(
            message,
            sizeof(message),
            "Network: %s",
            s_context.ssid
        );
    }

    modal_dialog_set_message(
        &s_context.dialog,
        message
    );
}

static void wifi_credentials_dialog_submit(void)
{
    if (!modal_dialog_is_open(
            &s_context.dialog
        )) {

        return;
    }

    const char *password = "";

    if (s_context.password_required) {
        if (s_context.password_textarea == NULL) {
            return;
        }

        password =
            lv_textarea_get_text(
                s_context.password_textarea
            );

        if (password == NULL) {
            password = "";
        }

        const size_t password_length =
            strlen(password);

        if (password_length <
            WIFI_SERVICE_STA_PASSWORD_MIN_LENGTH) {

            wifi_credentials_dialog_set_message(
                "Password must contain at least 8 characters"
            );

            return;
        }

        if (password_length >=
            WIFI_SERVICE_STA_PASSWORD_MAX_LENGTH) {

            wifi_credentials_dialog_set_message(
                "Password is too long"
            );

            return;
        }
    }

    if (s_context.submit_callback == NULL) {
        wifi_credentials_dialog_set_message(
            "Connection callback is unavailable"
        );

        return;
    }

    modal_dialog_set_primary_enabled(
        &s_context.dialog,
        false
    );

    modal_dialog_set_secondary_enabled(
        &s_context.dialog,
        false
    );

    wifi_credentials_dialog_set_message(
        "Applying network settings..."
    );

    const esp_err_t result =
        s_context.submit_callback(
            s_context.ssid,
            password,
            s_context.callback_context
        );

    if (result == ESP_OK) {
        wifi_credentials_dialog_close();
        return;
    }

    char error_text[96];

    (void)snprintf(
        error_text,
        sizeof(error_text),
        "Failed to apply settings: %s",
        esp_err_to_name(result)
    );

    wifi_credentials_dialog_set_message(
        error_text
    );

    modal_dialog_set_primary_enabled(
        &s_context.dialog,
        true
    );

    modal_dialog_set_secondary_enabled(
        &s_context.dialog,
        true
    );
}

static void wifi_credentials_dialog_primary_action(
    modal_dialog_t *dialog
)
{
    (void)dialog;

    wifi_credentials_dialog_submit();
}

static void wifi_credentials_dialog_secondary_action(
    modal_dialog_t *dialog
)
{
    (void)dialog;

    /*
     * The base modal dialog closes automatically after this callback.
     */
}

static void wifi_credentials_dialog_keyboard_event_cb(
    lv_event_t *event
)
{
    const lv_event_code_t code =
        lv_event_get_code(event);

    if (code == LV_EVENT_READY) {
        wifi_credentials_dialog_submit();

    } else if (code == LV_EVENT_CANCEL) {
        wifi_credentials_dialog_close();
    }
}

static void wifi_credentials_dialog_delete_event_cb(
    lv_event_t *event
)
{
    (void)event;

    /*
     * The base modal dialog has already started deleting its objects.
     * Clear all non-owning pointers and callback state.
     */
    wifi_credentials_dialog_reset();
}

bool wifi_credentials_dialog_open(
    lv_obj_t *parent,
    const char *ssid,
    bool password_required,
    wifi_credentials_dialog_submit_cb_t submit_callback,
    void *context
)
{
    if ((parent == NULL) ||
        (ssid == NULL) ||
        (ssid[0] == '\0') ||
        (submit_callback == NULL)) {

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

    if (wifi_credentials_dialog_is_open()) {
        return false;
    }

    const size_t ssid_length =
        strlen(ssid);

    if (ssid_length >=
        sizeof(s_context.ssid)) {

        return false;
    }

    wifi_credentials_dialog_reset();

    (void)strlcpy(
        s_context.ssid,
        ssid,
        sizeof(s_context.ssid)
    );

    s_context.password_required =
        password_required;

    s_context.submit_callback =
        submit_callback;

    s_context.callback_context =
        context;

    const modal_dialog_config_t config = {
        .title = password_required
            ? "Connect to Wi-Fi"
            : "Open Wi-Fi network",

        .message = password_required
            ? "Enter the network password"
            : "Connect to this open network?",

        .icon = NULL,

        .primary_button_text = "Connect",
        .primary_action =
            wifi_credentials_dialog_primary_action,
        .close_on_primary_action = false,

        .secondary_button_text = "Cancel",
        .secondary_action =
            wifi_credentials_dialog_secondary_action,
        .close_on_secondary_action = true,

        .show_progress_bar = false,
        .initial_progress = 0U,
        .progress_text = NULL,

        .animate_open = true,
        .close_on_overlay_click = false,
    };

    if (!modal_dialog_create(
            &s_context.dialog,
            parent,
            &config
        )) {

        wifi_credentials_dialog_reset();

        return false;
    }

    lv_obj_set_height(
        s_context.dialog.message_label,
        WIFI_CREDENTIALS_MESSAGE_HEIGHT
    );

    lv_label_set_long_mode(
        s_context.dialog.message_label,
        LV_LABEL_LONG_WRAP
    );

    /*
     * Reset the complete credentials context when the base modal
     * overlay is deleted.
     */
    lv_obj_add_event_cb(
        s_context.dialog.overlay,
        wifi_credentials_dialog_delete_event_cb,
        LV_EVENT_DELETE,
        NULL
    );

    lv_obj_set_width(
        s_context.dialog.dialog,
        WIFI_CREDENTIALS_DIALOG_WIDTH
    );

    if (!password_required) {
        lv_obj_set_height(
            s_context.dialog.dialog,
            WIFI_CREDENTIALS_OPEN_DIALOG_HEIGHT
        );

        wifi_credentials_dialog_set_message(
            "This network does not require a password"
        );

        return true;
    }

    lv_obj_set_height(
        s_context.dialog.dialog,
        WIFI_CREDENTIALS_DIALOG_HEIGHT
    );

    wifi_credentials_dialog_set_message(
        "Enter the network password"
    );

    s_context.password_textarea =
        lv_textarea_create(
            s_context.dialog.content_container
        );

    if (s_context.password_textarea == NULL) {
        wifi_credentials_dialog_close();
        return false;
    }

    lv_obj_add_style(
        s_context.password_textarea,
        gui_styles_input(),
        LV_PART_MAIN
    );

    lv_obj_add_style(
        s_context.password_textarea,
        gui_styles_input_focused(),
        LV_PART_MAIN |
        LV_STATE_FOCUSED
    );

    lv_obj_set_style_text_color(
        s_context.password_textarea,
        theme->colors.text_muted,
        LV_PART_TEXTAREA_PLACEHOLDER
    );

    lv_obj_set_size(
        s_context.password_textarea,
        LV_PCT(100),
        WIFI_CREDENTIALS_TEXTAREA_HEIGHT
    );

    lv_textarea_set_one_line(
        s_context.password_textarea,
        true
    );

    lv_textarea_set_password_mode(
        s_context.password_textarea,
        true
    );

    lv_textarea_set_password_bullet(
        s_context.password_textarea,
        "*"
    );

    lv_textarea_set_max_length(
        s_context.password_textarea,
        WIFI_SERVICE_STA_PASSWORD_MAX_LENGTH - 1U
    );

    lv_textarea_set_placeholder_text(
        s_context.password_textarea,
        "Wi-Fi password"
    );

    s_context.keyboard =
        lv_keyboard_create(
            s_context.dialog.content_container
        );

    if (s_context.keyboard == NULL) {
        wifi_credentials_dialog_close();
        return false;
    }

    lv_obj_add_style(
        s_context.keyboard,
        gui_styles_text_small(),
        LV_PART_ITEMS |
        LV_STATE_DEFAULT
    );

    lv_obj_set_style_bg_color(
        s_context.keyboard,
        theme->modal.secondary,
        LV_PART_ITEMS
    );

    lv_obj_set_style_bg_opa(
        s_context.keyboard,
        LV_OPA_COVER,
        LV_PART_ITEMS
    );

    lv_obj_set_style_text_color(
        s_context.keyboard,
        theme->modal.title,
        LV_PART_ITEMS
    );

    lv_obj_set_style_border_color(
        s_context.keyboard,
        theme->modal.border,
        LV_PART_MAIN
    );

    lv_obj_set_style_border_color(
        s_context.keyboard,
        theme->modal.border,
        LV_PART_ITEMS
    );

    lv_obj_set_style_bg_color(
        s_context.keyboard,
        theme->modal.background,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        s_context.keyboard,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_color(
        s_context.keyboard,
        theme->modal.secondary_pressed,
        LV_PART_ITEMS |
        LV_STATE_PRESSED
    );

    lv_obj_set_style_bg_opa(
        s_context.keyboard,
        LV_OPA_COVER,
        LV_PART_ITEMS |
        LV_STATE_PRESSED
    );

    lv_obj_set_size(
        s_context.keyboard,
        LV_PCT(100),
        WIFI_CREDENTIALS_KEYBOARD_HEIGHT
    );

    lv_keyboard_set_mode(
        s_context.keyboard,
        LV_KEYBOARD_MODE_TEXT_LOWER
    );

    lv_keyboard_set_textarea(
        s_context.keyboard,
        s_context.password_textarea
    );

    lv_obj_add_event_cb(
        s_context.keyboard,
        wifi_credentials_dialog_keyboard_event_cb,
        LV_EVENT_ALL,
        NULL
    );

    lv_obj_add_state(
        s_context.password_textarea,
        LV_STATE_FOCUSED
    );

    return true;
}

void wifi_credentials_dialog_close(void)
{
    if (!wifi_credentials_dialog_is_open()) {
        return;
    }

    modal_dialog_close(
        &s_context.dialog
    );

    /*
     * The overlay delete callback normally performs this reset.
     */
    wifi_credentials_dialog_reset();
}

bool wifi_credentials_dialog_is_open(void)
{
    return modal_dialog_is_open(
        &s_context.dialog
    );
}
