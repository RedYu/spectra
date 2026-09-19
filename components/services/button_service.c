/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "button_service.h"

#include <stdatomic.h>
#include <stddef.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#include "board_config.h"

#define BUTTON_SERVICE_POLL_INTERVAL_US      (10U * 1000U)
#define BUTTON_SERVICE_DEBOUNCE_US           (40U * 1000U)
#define BUTTON_SERVICE_STARTUP_GUARD_US      (1000U * 1000U)
#define BUTTON_SERVICE_DOUBLE_PRESS_US       (350U * 1000U)
#define BUTTON_SERVICE_LONG_PRESS_US         (2000U * 1000U)

typedef struct
{
    gpio_num_t pin;
    int active_level;
    const char *name;

} button_descriptor_t;

typedef struct
{
    bool initialized;
    bool raw_pressed;
    bool stable_pressed;
    bool long_press_sent;
    bool short_press_pending;
    bool double_press_candidate;

    int64_t raw_changed_at_us;
    int64_t pressed_at_us;
    int64_t first_short_release_at_us;

} button_state_t;

typedef struct
{
    button_event_callback_t callback;
    void *context;

} button_callback_t;

static const char *TAG =
    "button_service";

static const button_descriptor_t s_descriptors[BUTTON_ID_COUNT] = {
    [BUTTON_ID_SERVICE] = {
        .pin =
            SERVICE_BUTTON_PIN,

        .active_level =
            SERVICE_BUTTON_ACTIVE_LEVEL,

        .name =
            "service",
    },

    [BUTTON_ID_POWER] = {
        .pin =
            POWER_BUTTON_PIN,

        .active_level =
            POWER_BUTTON_ACTIVE_LEVEL,

        .name =
            "power",
    },
};

static esp_timer_handle_t s_timer = NULL;

static portMUX_TYPE s_lock =
    portMUX_INITIALIZER_UNLOCKED;

static button_callback_t s_callback = {0};
static button_state_t s_states[BUTTON_ID_COUNT] = {0};

static int64_t s_started_at_us = 0;

static atomic_bool s_running =
    ATOMIC_VAR_INIT(false);

static atomic_bool s_pressed[BUTTON_ID_COUNT];

static atomic_uint_fast32_t
    s_short_press_count[BUTTON_ID_COUNT];

static atomic_uint_fast32_t
    s_double_press_count[BUTTON_ID_COUNT];

static atomic_uint_fast32_t
    s_long_press_count[BUTTON_ID_COUNT];

static bool button_service_id_valid(
    button_id_t button
)
{
    return
        (button >= BUTTON_ID_SERVICE) &&
        (button < BUTTON_ID_COUNT);
}

const char *button_service_button_name(
    button_id_t button
)
{
    if (!button_service_id_valid(button)) {
        return "unknown";
    }

    return s_descriptors[button].name;
}

const char *button_service_event_name(
    button_event_t event
)
{
    switch (event) {
        case BUTTON_EVENT_SHORT_PRESS:
            return "short";

        case BUTTON_EVENT_DOUBLE_PRESS:
            return "double";

        case BUTTON_EVENT_LONG_PRESS:
            return "long";

        default:
            return "unknown";
    }
}

static bool button_service_read_pressed(
    button_id_t button
)
{
    const button_descriptor_t *descriptor =
        &s_descriptors[button];

    return
        gpio_get_level(descriptor->pin) ==
        descriptor->active_level;
}

static void button_service_dispatch(
    button_id_t button,
    button_event_t event
)
{
    button_callback_t callback;

    taskENTER_CRITICAL(&s_lock);
    callback = s_callback;
    taskEXIT_CRITICAL(&s_lock);

    switch (event) {
        case BUTTON_EVENT_SHORT_PRESS:
            atomic_fetch_add(
                &s_short_press_count[button],
                1U
            );
            break;

        case BUTTON_EVENT_DOUBLE_PRESS:
            atomic_fetch_add(
                &s_double_press_count[button],
                1U
            );
            break;

        case BUTTON_EVENT_LONG_PRESS:
            atomic_fetch_add(
                &s_long_press_count[button],
                1U
            );
            break;

        default:
            return;
    }

    ESP_LOGI(
        TAG,
        "Button event: button=%s, event=%s",
        button_service_button_name(button),
        button_service_event_name(event)
    );

    if (callback.callback != NULL) {
        callback.callback(
            button,
            event,
            callback.context
        );
    }
}

static void button_service_initialize_state(
    button_id_t button,
    int64_t now_us
)
{
    button_state_t *state =
        &s_states[button];

    state->raw_pressed =
        button_service_read_pressed(button);

    state->stable_pressed =
        state->raw_pressed;

    state->raw_changed_at_us =
        now_us;

    state->pressed_at_us =
        state->stable_pressed
            ? now_us
            : 0;

    state->initialized =
        true;

    atomic_store(
        &s_pressed[button],
        state->stable_pressed
    );
}

static void button_service_process(
    button_id_t button,
    int64_t now_us
)
{
    button_state_t *state =
        &s_states[button];

    if (!state->initialized) {
        button_service_initialize_state(
            button,
            now_us
        );

        return;
    }

    const bool current_raw_pressed =
        button_service_read_pressed(button);

    if (state->short_press_pending &&
        !state->stable_pressed &&
        ((now_us -
          state->first_short_release_at_us) >
         BUTTON_SERVICE_DOUBLE_PRESS_US)) {
        state->short_press_pending =
            false;

        button_service_dispatch(
            button,
            BUTTON_EVENT_SHORT_PRESS
        );
    }

    if (current_raw_pressed !=
        state->raw_pressed) {
        state->raw_pressed =
            current_raw_pressed;

        state->raw_changed_at_us =
            now_us;
    }

    if ((state->raw_pressed !=
         state->stable_pressed) &&
        ((now_us - state->raw_changed_at_us) >=
         BUTTON_SERVICE_DEBOUNCE_US)) {
        state->stable_pressed =
            state->raw_pressed;

        atomic_store(
            &s_pressed[button],
            state->stable_pressed
        );

        if (state->stable_pressed) {
            state->pressed_at_us =
                now_us;

            state->long_press_sent =
                false;

            state->double_press_candidate =
                state->short_press_pending &&
                ((now_us -
                  state->first_short_release_at_us) <=
                 BUTTON_SERVICE_DOUBLE_PRESS_US);
        } else if (!state->long_press_sent) {
            if (state->double_press_candidate) {
                state->short_press_pending =
                    false;

                state->double_press_candidate =
                    false;

                button_service_dispatch(
                    button,
                    BUTTON_EVENT_DOUBLE_PRESS
                );
            } else {
                state->short_press_pending =
                    true;

                state->first_short_release_at_us =
                    now_us;
            }
        }
    }

    if (state->stable_pressed &&
        !state->long_press_sent &&
        ((now_us - state->pressed_at_us) >=
         BUTTON_SERVICE_LONG_PRESS_US)) {
        state->long_press_sent =
            true;

        state->short_press_pending =
            false;

        state->double_press_candidate =
            false;

        button_service_dispatch(
            button,
            BUTTON_EVENT_LONG_PRESS
        );
    }

}

static void button_service_timer_callback(
    void *argument
)
{
    (void)argument;

    if (!atomic_load(&s_running)) {
        return;
    }

    const int64_t now_us =
        esp_timer_get_time();

    if ((now_us - s_started_at_us) <
        BUTTON_SERVICE_STARTUP_GUARD_US) {
        return;
    }

    for (button_id_t button = BUTTON_ID_SERVICE;
         button < BUTTON_ID_COUNT;
         button++) {

        button_service_process(
            button,
            now_us
        );
    }
}

static esp_err_t button_service_configure_gpio(void)
{
    uint64_t pin_mask = 0U;

    for (button_id_t button = BUTTON_ID_SERVICE;
         button < BUTTON_ID_COUNT;
         button++) {

        pin_mask |=
            1ULL << s_descriptors[button].pin;
    }

    const gpio_config_t config = {
        .pin_bit_mask =
            pin_mask,

        .mode =
            GPIO_MODE_INPUT,

        .pull_up_en =
            GPIO_PULLUP_ENABLE,

        .pull_down_en =
            GPIO_PULLDOWN_DISABLE,

        .intr_type =
            GPIO_INTR_DISABLE,
    };

    return gpio_config(&config);
}

static void button_service_reset_gpio(void)
{
    for (button_id_t button = BUTTON_ID_SERVICE;
         button < BUTTON_ID_COUNT;
         button++) {

        (void)gpio_reset_pin(
            s_descriptors[button].pin
        );
    }
}

esp_err_t button_service_start(void)
{
    bool expected = false;

    if (!atomic_compare_exchange_strong(
            &s_running,
            &expected,
            true)) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result =
        button_service_configure_gpio();

    if (result != ESP_OK) {
        atomic_store(
            &s_running,
            false
        );

        return result;
    }

    s_started_at_us =
        esp_timer_get_time();

    for (button_id_t button = BUTTON_ID_SERVICE;
         button < BUTTON_ID_COUNT;
         button++) {

        s_states[button] =
            (button_state_t) {0};

        atomic_store(
            &s_pressed[button],
            false
        );

        atomic_store(
            &s_short_press_count[button],
            0U
        );

        atomic_store(
            &s_double_press_count[button],
            0U
        );

        atomic_store(
            &s_long_press_count[button],
            0U
        );
    }

    const esp_timer_create_args_t timer_config = {
        .callback =
            button_service_timer_callback,

        .arg =
            NULL,

        .dispatch_method =
            ESP_TIMER_TASK,

        .name =
            "buttons",

        .skip_unhandled_events =
            true,
    };

    result =
        esp_timer_create(
            &timer_config,
            &s_timer
        );

    if (result == ESP_OK) {
        result =
            esp_timer_start_periodic(
                s_timer,
                BUTTON_SERVICE_POLL_INTERVAL_US
            );
    }

    if (result != ESP_OK) {
        if (s_timer != NULL) {
            (void)esp_timer_delete(s_timer);
            s_timer = NULL;
        }

        button_service_reset_gpio();

        atomic_store(
            &s_running,
            false
        );

        return result;
    }

    ESP_LOGI(
        TAG,
        "Button monitoring started: service GPIO=%d, power GPIO=%d",
        (int)s_descriptors[BUTTON_ID_SERVICE].pin,
        (int)s_descriptors[BUTTON_ID_POWER].pin
    );

    return ESP_OK;
}

esp_err_t button_service_stop(void)
{
    if (!atomic_exchange(
            &s_running,
            false)) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result =
        esp_timer_stop(s_timer);

    if (result == ESP_OK) {
        result =
            esp_timer_delete(s_timer);
    }

    if (result != ESP_OK) {
        atomic_store(
            &s_running,
            true
        );

        return result;
    }

    s_timer = NULL;

    for (button_id_t button = BUTTON_ID_SERVICE;
         button < BUTTON_ID_COUNT;
         button++) {

        atomic_store(
            &s_pressed[button],
            false
        );
    }

    button_service_reset_gpio();

    ESP_LOGI(
        TAG,
        "Button monitoring stopped"
    );

    return ESP_OK;
}

esp_err_t button_service_set_callback(
    button_event_callback_t callback,
    void *context
)
{
    taskENTER_CRITICAL(&s_lock);

    s_callback.callback =
        callback;

    s_callback.context =
        callback != NULL
            ? context
            : NULL;

    taskEXIT_CRITICAL(&s_lock);

    return ESP_OK;
}

esp_err_t button_service_get_info(
    button_service_info_t *info
)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *info = (button_service_info_t) {
        .running =
            atomic_load(&s_running),
    };

    for (button_id_t button = BUTTON_ID_SERVICE;
         button < BUTTON_ID_COUNT;
         button++) {

        button_service_button_info_t *button_info =
            &info->buttons[button];

        button_info->pressed =
            atomic_load(
                &s_pressed[button]
            );

        button_info->short_press_count =
            (uint32_t)atomic_load(
                &s_short_press_count[button]
            );

        button_info->double_press_count =
            (uint32_t)atomic_load(
                &s_double_press_count[button]
            );

        button_info->long_press_count =
            (uint32_t)atomic_load(
                &s_long_press_count[button]
            );
    }

    return ESP_OK;
}

bool button_service_is_running(void)
{
    return atomic_load(&s_running);
}
