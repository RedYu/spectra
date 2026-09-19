/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "user_button_service.h"

#include <stdatomic.h>
#include <stddef.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#include "board_config.h"

#define USER_BUTTON_POLL_INTERVAL_US      (10U * 1000U)
#define USER_BUTTON_DEBOUNCE_US           (40U * 1000U)
#define USER_BUTTON_STARTUP_GUARD_US      (1000U * 1000U)
#define USER_BUTTON_DOUBLE_PRESS_US       (350U * 1000U)
#define USER_BUTTON_LONG_PRESS_US         (2000U * 1000U)

typedef struct
{
    user_button_event_callback_t callback;
    void *context;

} user_button_callback_t;

typedef struct
{
    bool initialized;
    bool raw_pressed;
    bool stable_pressed;
    bool long_press_sent;
    bool short_press_pending;

    int64_t started_at_us;
    int64_t raw_changed_at_us;
    int64_t pressed_at_us;
    int64_t first_short_release_at_us;

} user_button_state_t;

static const char *TAG =
    "user_button_service";

static esp_timer_handle_t s_timer = NULL;

static portMUX_TYPE s_lock =
    portMUX_INITIALIZER_UNLOCKED;

static user_button_callback_t s_callback = {0};
static user_button_state_t s_state = {0};

static atomic_bool s_running =
    ATOMIC_VAR_INIT(false);

static atomic_bool s_pressed =
    ATOMIC_VAR_INIT(false);

static atomic_uint_fast32_t s_short_press_count =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast32_t s_double_press_count =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast32_t s_long_press_count =
    ATOMIC_VAR_INIT(0U);

static bool user_button_read_pressed(void)
{
    return
        gpio_get_level(USER_BUTTON_PIN) ==
        USER_BUTTON_ACTIVE_LEVEL;
}

static void user_button_dispatch(
    user_button_event_t event
)
{
    user_button_callback_t callback;
    const char *event_name = NULL;

    taskENTER_CRITICAL(&s_lock);
    callback = s_callback;
    taskEXIT_CRITICAL(&s_lock);

    switch (event) {
        case USER_BUTTON_EVENT_SHORT_PRESS:
            event_name =
                "short";

            atomic_fetch_add(
                &s_short_press_count,
                1U
            );
            break;

        case USER_BUTTON_EVENT_DOUBLE_PRESS:
            event_name =
                "double";

            atomic_fetch_add(
                &s_double_press_count,
                1U
            );
            break;

        case USER_BUTTON_EVENT_LONG_PRESS:
            event_name =
                "long";

            atomic_fetch_add(
                &s_long_press_count,
                1U
            );
            break;

        default:
            return;
    }

    ESP_LOGI(
        TAG,
        "User button event: %s",
        event_name
    );

    if (callback.callback != NULL) {
        callback.callback(
            event,
            callback.context
        );
    }
}

static void user_button_initialize_state(
    int64_t now_us
)
{
    s_state.raw_pressed =
        user_button_read_pressed();

    s_state.stable_pressed =
        s_state.raw_pressed;

    s_state.raw_changed_at_us =
        now_us;

    s_state.pressed_at_us =
        s_state.stable_pressed
            ? now_us
            : 0;

    s_state.initialized =
        true;

    atomic_store(
        &s_pressed,
        s_state.stable_pressed
    );
}

static void user_button_timer_callback(
    void *argument
)
{
    (void)argument;

    if (!atomic_load(&s_running)) {
        return;
    }

    const int64_t now_us =
        esp_timer_get_time();

    if ((now_us - s_state.started_at_us) <
        USER_BUTTON_STARTUP_GUARD_US) {
        return;
    }

    if (!s_state.initialized) {
        user_button_initialize_state(
            now_us
        );

        return;
    }

    const bool current_raw_pressed =
        user_button_read_pressed();

    if (current_raw_pressed !=
        s_state.raw_pressed) {
        s_state.raw_pressed =
            current_raw_pressed;

        s_state.raw_changed_at_us =
            now_us;
    }

    if ((s_state.raw_pressed !=
         s_state.stable_pressed) &&
        ((now_us - s_state.raw_changed_at_us) >=
         USER_BUTTON_DEBOUNCE_US)) {
        s_state.stable_pressed =
            s_state.raw_pressed;

        atomic_store(
            &s_pressed,
            s_state.stable_pressed
        );

        if (s_state.stable_pressed) {
            s_state.pressed_at_us =
                now_us;

            s_state.long_press_sent =
                false;
        } else if (!s_state.long_press_sent) {
            if (s_state.short_press_pending &&
                ((now_us -
                  s_state.first_short_release_at_us) <=
                 USER_BUTTON_DOUBLE_PRESS_US)) {
                s_state.short_press_pending =
                    false;

                user_button_dispatch(
                    USER_BUTTON_EVENT_DOUBLE_PRESS
                );
            } else {
                s_state.short_press_pending =
                    true;

                s_state.first_short_release_at_us =
                    now_us;
            }
        }
    }

    if (s_state.stable_pressed &&
        !s_state.long_press_sent &&
        ((now_us - s_state.pressed_at_us) >=
         USER_BUTTON_LONG_PRESS_US)) {
        s_state.long_press_sent =
            true;

        s_state.short_press_pending =
            false;

        user_button_dispatch(
            USER_BUTTON_EVENT_LONG_PRESS
        );
    }

    if (s_state.short_press_pending &&
        !s_state.stable_pressed &&
        ((now_us -
          s_state.first_short_release_at_us) >
         USER_BUTTON_DOUBLE_PRESS_US)) {
        s_state.short_press_pending =
            false;

        user_button_dispatch(
            USER_BUTTON_EVENT_SHORT_PRESS
        );
    }
}

esp_err_t user_button_service_start(void)
{
    bool expected = false;

    if (!atomic_compare_exchange_strong(
            &s_running,
            &expected,
            true)) {
        return ESP_ERR_INVALID_STATE;
    }

    const gpio_config_t config = {
        .pin_bit_mask =
            1ULL << USER_BUTTON_PIN,

        .mode =
            GPIO_MODE_INPUT,

        .pull_up_en =
            GPIO_PULLUP_ENABLE,

        .pull_down_en =
            GPIO_PULLDOWN_DISABLE,

        .intr_type =
            GPIO_INTR_DISABLE,
    };

    esp_err_t result =
        gpio_config(&config);

    if (result != ESP_OK) {
        atomic_store(
            &s_running,
            false
        );

        return result;
    }

    s_state = (user_button_state_t) {
        .started_at_us =
            esp_timer_get_time(),
    };

    atomic_store(
        &s_pressed,
        false
    );

    atomic_store(
        &s_short_press_count,
        0U
    );

    atomic_store(
        &s_double_press_count,
        0U
    );

    atomic_store(
        &s_long_press_count,
        0U
    );

    const esp_timer_create_args_t timer_config = {
        .callback =
            user_button_timer_callback,

        .arg =
            NULL,

        .dispatch_method =
            ESP_TIMER_TASK,

        .name =
            "user_button",

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
                USER_BUTTON_POLL_INTERVAL_US
            );
    }

    if (result != ESP_OK) {
        if (s_timer != NULL) {
            (void)esp_timer_delete(s_timer);
            s_timer = NULL;
        }

        (void)gpio_reset_pin(USER_BUTTON_PIN);

        atomic_store(
            &s_running,
            false
        );

        return result;
    }

    ESP_LOGI(
        TAG,
        "User button monitoring started: GPIO=%d",
        (int)USER_BUTTON_PIN
    );

    return ESP_OK;
}

esp_err_t user_button_service_stop(void)
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

    atomic_store(
        &s_pressed,
        false
    );

    (void)gpio_reset_pin(USER_BUTTON_PIN);

    ESP_LOGI(
        TAG,
        "User button monitoring stopped"
    );

    return ESP_OK;
}

esp_err_t user_button_service_set_callback(
    user_button_event_callback_t callback,
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

esp_err_t user_button_service_get_info(
    user_button_service_info_t *info
)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *info = (user_button_service_info_t) {
        .running =
            atomic_load(&s_running),

        .pressed =
            atomic_load(&s_pressed),

        .short_press_count =
            (uint32_t)atomic_load(
                &s_short_press_count
            ),

        .double_press_count =
            (uint32_t)atomic_load(
                &s_double_press_count
            ),

        .long_press_count =
            (uint32_t)atomic_load(
                &s_long_press_count
            ),
    };

    return ESP_OK;
}

bool user_button_service_is_running(void)
{
    return atomic_load(&s_running);
}
