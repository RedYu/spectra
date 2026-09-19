/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"

#include "button_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Button action called from the GUI task.
 *
 * The callback may access LVGL and screen objects. It must not retain
 * the context after returning.
 */
typedef void (*button_action_callback_t)(
    button_id_t button,
    button_event_t event,
    void *context
);

/**
 * @brief Button-action queue statistics.
 */
typedef struct
{
    uint32_t current;
    uint32_t peak;
    uint32_t capacity;
    uint64_t dropped;

} button_action_dispatcher_statistics_t;

/**
 * @brief Initialize queued delivery from the button service.
 *
 * Creates a small event queue and registers the non-blocking producer
 * callback with button_service. Call before the GUI task starts.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already
 * initialized, or ESP_ERR_NO_MEM if the queue cannot be created.
 */
esp_err_t button_action_dispatcher_init(void);

/**
 * @brief Register an action for one button-event combination.
 *
 * Passing NULL removes the selected action. Registration may be
 * changed from the GUI task when the active screen changes.
 *
 * @return ESP_OK on success or ESP_ERR_INVALID_ARG for invalid IDs.
 */
esp_err_t button_action_dispatcher_set_action(
    button_id_t button,
    button_event_t event,
    button_action_callback_t callback,
    void *context
);

/**
 * @brief Process all queued events from the GUI task.
 *
 * Plays event feedback and invokes registered actions. This function
 * must only be called from the GUI task.
 */
void button_action_dispatcher_process(void);

/**
 * @brief Copy queue occupancy and drop statistics.
 */
esp_err_t button_action_dispatcher_get_statistics(
    button_action_dispatcher_statistics_t *statistics
);

/**
 * @brief Reset queue peak and dropped-event counters.
 */
esp_err_t button_action_dispatcher_reset_statistics(void);

#ifdef __cplusplus
}
#endif
