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
 * @brief Physical application buttons.
 */
typedef enum
{
    /**
     * GPIO0 service button, also used by the ROM bootloader.
     */
    BUTTON_ID_SERVICE = 0,

    /**
     * GPIO47 user button, also connected to AXP313A PWRON.
     */
    BUTTON_ID_POWER,

    BUTTON_ID_COUNT,

} button_id_t;

/**
 * @brief Recognized button actions.
 */
typedef enum
{
    BUTTON_EVENT_SHORT_PRESS = 0,
    BUTTON_EVENT_DOUBLE_PRESS,
    BUTTON_EVENT_LONG_PRESS,

    BUTTON_EVENT_COUNT,

} button_event_t;

/**
 * @brief Button event callback.
 *
 * The callback runs in the ESP timer task and must not block. Work
 * should be forwarded to the appropriate application service.
 */
typedef void (*button_event_callback_t)(
    button_id_t button,
    button_event_t event,
    void *context
);

/**
 * @brief State and counters for one physical button.
 */
typedef struct
{
    bool pressed;

    uint32_t short_press_count;
    uint32_t double_press_count;
    uint32_t long_press_count;

} button_service_button_info_t;

/**
 * @brief Complete button-service information.
 */
typedef struct
{
    bool running;

    button_service_button_info_t buttons[BUTTON_ID_COUNT];

} button_service_info_t;

/**
 * @brief Start monitoring the service and power buttons.
 *
 * GPIO0 is ignored briefly after application startup so its ROM
 * download-mode function remains separate from application actions.
 * Both buttons share one periodic ESP timer and require no dedicated
 * FreeRTOS task.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already running,
 * otherwise an ESP-IDF timer or GPIO error code.
 */
esp_err_t button_service_start(void);

/**
 * @brief Stop button monitoring and release GPIO resources.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not running,
 * otherwise an ESP-IDF timer or GPIO error code.
 */
esp_err_t button_service_stop(void);

/**
 * @brief Register or remove the application event callback.
 *
 * Passing NULL removes the current callback.
 *
 * @param[in] callback Callback or NULL.
 * @param[in] context User context passed to the callback.
 *
 * @return ESP_OK on success.
 */
esp_err_t button_service_set_callback(
    button_event_callback_t callback,
    void *context
);

/**
 * @brief Copy current button states and event counters.
 */
esp_err_t button_service_get_info(
    button_service_info_t *info
);

/**
 * @brief Check whether button monitoring is running.
 */
bool button_service_is_running(void);

/**
 * @brief Return the stable display name of a button.
 */
const char *button_service_button_name(
    button_id_t button
);

/**
 * @brief Return the stable display name of a button event.
 */
const char *button_service_event_name(
    button_event_t event
);

#ifdef __cplusplus
}
#endif
