/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "button_action_dispatcher.h"

#include <stdatomic.h>
#include <stddef.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "buzzer_service.h"

#define BUTTON_ACTION_QUEUE_LENGTH  (8U)

typedef struct
{
    button_id_t button;
    button_event_t event;

} button_action_message_t;

typedef struct
{
    button_action_callback_t callback;
    void *context;

} button_action_entry_t;

static const char *TAG =
    "button_actions";

static QueueHandle_t s_queue = NULL;

static portMUX_TYPE s_action_lock =
    portMUX_INITIALIZER_UNLOCKED;

static button_action_entry_t
    s_actions[BUTTON_ID_COUNT][BUTTON_EVENT_COUNT] = {0};

static atomic_uint_fast32_t s_queue_peak =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast64_t s_dropped_events =
    ATOMIC_VAR_INIT(0U);

static bool button_action_dispatcher_button_valid(
    button_id_t button
)
{
    return
        (button >= BUTTON_ID_SERVICE) &&
        (button < BUTTON_ID_COUNT);
}

static bool button_action_dispatcher_event_valid(
    button_event_t event
)
{
    return
        (event >= BUTTON_EVENT_SHORT_PRESS) &&
        (event < BUTTON_EVENT_COUNT);
}

static void button_action_dispatcher_update_peak(void)
{
    const uint32_t current =
        (uint32_t)uxQueueMessagesWaiting(s_queue);

    uint_fast32_t peak =
        atomic_load(&s_queue_peak);

    while ((current > peak) &&
           !atomic_compare_exchange_weak(
               &s_queue_peak,
               &peak,
               current)) {
    }
}

static void button_action_dispatcher_button_callback(
    button_id_t button,
    button_event_t event,
    void *context
)
{
    (void)context;

    if (s_queue == NULL) {
        return;
    }

    const button_action_message_t message = {
        .button =
            button,

        .event =
            event,
    };

    if (xQueueSend(
            s_queue,
            &message,
            0U
        ) != pdTRUE) {

        atomic_fetch_add(
            &s_dropped_events,
            1U
        );

        return;
    }

    button_action_dispatcher_update_peak();
}

static void button_action_dispatcher_play_feedback(
    button_event_t event
)
{
    if (!buzzer_service_is_running()) {
        return;
    }

    buzzer_signal_t signal;

    switch (event) {
        case BUTTON_EVENT_SHORT_PRESS:
            signal =
                BUZZER_SIGNAL_CLICK;
            break;

        case BUTTON_EVENT_DOUBLE_PRESS:
            signal =
                BUZZER_SIGNAL_SUCCESS;
            break;

        case BUTTON_EVENT_LONG_PRESS:
            signal =
                BUZZER_SIGNAL_WARNING;
            break;

        default:
            return;
    }

    const esp_err_t result =
        buzzer_service_play(signal);

    if ((result != ESP_OK) &&
        (result != ESP_ERR_INVALID_STATE)) {

        ESP_LOGW(
            TAG,
            "Failed to queue button feedback: %s",
            esp_err_to_name(result)
        );
    }
}

esp_err_t button_action_dispatcher_init(void)
{
    if (s_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_queue =
        xQueueCreate(
            BUTTON_ACTION_QUEUE_LENGTH,
            sizeof(button_action_message_t)
        );

    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    atomic_store(
        &s_queue_peak,
        0U
    );

    atomic_store(
        &s_dropped_events,
        0U
    );

    const esp_err_t result =
        button_service_set_callback(
            button_action_dispatcher_button_callback,
            NULL
        );

    if (result != ESP_OK) {
        vQueueDelete(s_queue);
        s_queue = NULL;

        return result;
    }

    ESP_LOGI(
        TAG,
        "Button action dispatcher initialized: queue=%u",
        (unsigned int)BUTTON_ACTION_QUEUE_LENGTH
    );

    return ESP_OK;
}

esp_err_t button_action_dispatcher_set_action(
    button_id_t button,
    button_event_t event,
    button_action_callback_t callback,
    void *context
)
{
    if (!button_action_dispatcher_button_valid(button) ||
        !button_action_dispatcher_event_valid(event)) {

        return ESP_ERR_INVALID_ARG;
    }

    taskENTER_CRITICAL(&s_action_lock);

    s_actions[button][event].callback =
        callback;

    s_actions[button][event].context =
        callback != NULL
            ? context
            : NULL;

    taskEXIT_CRITICAL(&s_action_lock);

    return ESP_OK;
}

void button_action_dispatcher_process(void)
{
    if (s_queue == NULL) {
        return;
    }

    button_action_message_t message;

    while (xQueueReceive(
               s_queue,
               &message,
               0U
           ) == pdTRUE) {

        if (!button_action_dispatcher_button_valid(
                message.button) ||
            !button_action_dispatcher_event_valid(
                message.event)) {

            continue;
        }

        button_action_dispatcher_play_feedback(
            message.event
        );

        button_action_entry_t action;

        taskENTER_CRITICAL(&s_action_lock);
        action =
            s_actions[message.button][message.event];
        taskEXIT_CRITICAL(&s_action_lock);

        if (action.callback != NULL) {
            action.callback(
                message.button,
                message.event,
                action.context
            );
        }
    }
}

esp_err_t button_action_dispatcher_get_statistics(
    button_action_dispatcher_statistics_t *statistics
)
{
    if (statistics == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    *statistics =
        (button_action_dispatcher_statistics_t) {
            .current =
                (uint32_t)uxQueueMessagesWaiting(
                    s_queue
                ),

            .peak =
                (uint32_t)atomic_load(
                    &s_queue_peak
                ),

            .capacity =
                BUTTON_ACTION_QUEUE_LENGTH,

            .dropped =
                (uint64_t)atomic_load(
                    &s_dropped_events
                ),
        };

    return ESP_OK;
}

esp_err_t button_action_dispatcher_reset_statistics(void)
{
    if (s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    atomic_store(
        &s_queue_peak,
        uxQueueMessagesWaiting(s_queue)
    );

    atomic_store(
        &s_dropped_events,
        0U
    );

    return ESP_OK;
}
