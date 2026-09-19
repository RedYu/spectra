/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "gui_feedback.h"

#include <stdbool.h>
#include <stddef.h>

#include "buzzer_service.h"

static bool s_initialized = false;

static bool gui_feedback_is_control(
    const lv_obj_t *object
)
{
    return
        lv_obj_has_class(object, &lv_button_class) ||
        lv_obj_has_class(object, &lv_buttonmatrix_class) ||
        lv_obj_has_class(object, &lv_checkbox_class) ||
        lv_obj_has_class(object, &lv_dropdown_class) ||
        lv_obj_has_class(object, &lv_slider_class) ||
        lv_obj_has_class(object, &lv_switch_class);
}

static void gui_feedback_play_click(void)
{
    if (buzzer_service_is_running()) {
        (void)buzzer_service_play(
            BUZZER_SIGNAL_CLICK
        );
    }
}

static void gui_feedback_pressed_event(
    lv_event_t *event
)
{
    if (event == NULL) {
        return;
    }

    gui_feedback_play_click();
}

static void gui_feedback_input_pressed_event(
    lv_event_t *event
)
{
    if (event == NULL) {
        return;
    }

    lv_obj_t *object =
        (lv_obj_t *)lv_event_get_param(event);

    if ((object == NULL) ||
        lv_obj_has_state(object, LV_STATE_DISABLED) ||
        !gui_feedback_is_control(object)) {

        return;
    }

    gui_feedback_play_click();
}

esp_err_t gui_feedback_init(
    lv_indev_t *input
)
{
    if (input == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    lv_indev_add_event_cb(
        input,
        gui_feedback_input_pressed_event,
        LV_EVENT_PRESSED,
        NULL
    );

    s_initialized = true;

    return ESP_OK;
}

void gui_feedback_attach(
    lv_obj_t *object
)
{
    if (object == NULL) {
        return;
    }

    if (s_initialized) {
        return;
    }

    lv_obj_add_event_cb(
        object,
        gui_feedback_pressed_event,
        LV_EVENT_PRESSED,
        NULL
    );
}
