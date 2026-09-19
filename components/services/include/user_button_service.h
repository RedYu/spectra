/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Recognized user-button actions.
 */
typedef enum
{
    USER_BUTTON_EVENT_SHORT_PRESS = 0,
    USER_BUTTON_EVENT_DOUBLE_PRESS,
    USER_BUTTON_EVENT_LONG_PRESS,

    USER_BUTTON_EVENT_COUNT,

} user_button_event_t;

/**
 * @brief User-button event callback.
 *
 * The callback runs in the ESP timer task. It must not block. Work
 * should be forwarded to the appropriate application service.
 */
typedef void (*user_button_event_callback_t)(
    user_button_event_t event,
    void *context
);

/**
 * @brief Current user-button service information.
 */
typedef struct
{
    bool running;
    bool pressed;

    uint32_t short_press_count;
    uint32_t double_press_count;
    uint32_t long_press_count;

} user_button_service_info_t;

/**
 * @brief Start monitoring the active-low BOOT button on GPIO0.
 *
 * Button input is ignored briefly after application startup so that
 * the ROM download-mode strapping function remains separate from
 * application button handling.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already running,
 * otherwise an ESP-IDF error code.
 */
esp_err_t user_button_service_start(void);

/**
 * @brief Stop monitoring the user button and release GPIO resources.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not running,
 * otherwise an ESP-IDF timer or GPIO error code.
 */
esp_err_t user_button_service_stop(void);

/**
 * @brief Register or remove the application event callback.
 *
 * Passing NULL removes the current callback. Registration is atomic
 * with respect to event dispatch.
 *
 * @param[in] callback Callback or NULL.
 * @param[in] context User context passed to the callback.
 *
 * @return ESP_OK on success.
 */
esp_err_t user_button_service_set_callback(
    user_button_event_callback_t callback,
    void *context
);

/**
 * @brief Copy current button state and event counters.
 */
esp_err_t user_button_service_get_info(
    user_button_service_info_t *info
);

/**
 * @brief Check whether user-button monitoring is running.
 */
bool user_button_service_is_running(void);

#ifdef __cplusplus
}
#endif
