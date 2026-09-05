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

#define BUZZER_SERVICE_VOLUME_MAX_PERCENT      (100U)
#define BUZZER_SERVICE_VOLUME_DEFAULT_PERCENT  (70U)

/**
 * @brief Predefined application buzzer signals.
 */
typedef enum
{
    /**
     * Short UI interaction feedback.
     */
    BUZZER_SIGNAL_CLICK = 0,

    /**
     * Operation completed successfully.
     */
    BUZZER_SIGNAL_SUCCESS,

    /**
     * Non-critical warning.
     */
    BUZZER_SIGNAL_WARNING,

    /**
     * Operation failed.
     */
    BUZZER_SIGNAL_ERROR,

    /**
     * Application startup completed.
     */
    BUZZER_SIGNAL_STARTUP,

    /**
     * Device restart or shutdown requested.
     */
    BUZZER_SIGNAL_SHUTDOWN,

    BUZZER_SIGNAL_COUNT,

} buzzer_signal_t;

/**
 * @brief Start the asynchronous buzzer service.
 *
 * Initializes the buzzer driver, creates the signal queue and starts
 * the playback task. Repeated calls are rejected.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already running,
 * ESP_ERR_NO_MEM if required resources cannot be created, otherwise an
 * ESP-IDF error code.
 */
esp_err_t buzzer_service_start(void);

/**
 * @brief Stop the buzzer service and release its resources.
 *
 * Pending signals are discarded. Current playback is interrupted as
 * soon as possible and the function waits for the service task to
 * terminate.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not running, or ESP_ERR_TIMEOUT if the task does not terminate in
 * time.
 */
esp_err_t buzzer_service_stop(void);

/**
 * @brief Queue a predefined buzzer signal for asynchronous playback.
 *
 * The function returns immediately after placing the signal into the
 * service queue.
 *
 * @param[in] signal Signal to play.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if signal is invalid,
 * ESP_ERR_INVALID_STATE if the service is not running, or
 * ESP_ERR_TIMEOUT if the signal queue is full.
 */
esp_err_t buzzer_service_play(
    buzzer_signal_t signal
);

/**
 * @brief Remove all pending signals and stop current playback.
 *
 * The buzzer service remains running and can accept new signals.
 *
 * @return ESP_OK on success or ESP_ERR_INVALID_STATE if the service is
 * not running.
 */
esp_err_t buzzer_service_cancel(void);

/**
 * @brief Enable or disable buzzer output.
 *
 * Disabling the service removes pending signals and stops current
 * playback. The service task remains running.
 *
 * @param[in] enabled True to enable audible signals.
 *
 * @return ESP_OK on success or ESP_ERR_INVALID_STATE if the service is
 * not running.
 */
esp_err_t buzzer_service_set_enabled(
    bool enabled
);

/**
 * @brief Get the current buzzer-output preference.
 *
 * @param[out] enabled Destination for the enabled state.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if enabled is NULL,
 * or ESP_ERR_INVALID_STATE if the service is not running.
 */
esp_err_t buzzer_service_get_enabled(
    bool *enabled
);

/**
 * @brief Check whether the buzzer service is running.
 */
bool buzzer_service_is_running(void);

/**
 * @brief Set the buzzer volume.
 *
 * A zero value mutes and cancels current playback. Other values are
 * applied when the next note starts.
 *
 * @param[in] volume_percent Volume from 0 to 100 percent.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if the value exceeds
 * 100, or ESP_ERR_INVALID_STATE if the service is not running.
 */
esp_err_t buzzer_service_set_volume(
    uint8_t volume_percent
);

/**
 * @brief Get the configured buzzer volume.
 *
 * @param[out] volume_percent Destination for the volume percentage.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if volume_percent is
 * NULL, or ESP_ERR_INVALID_STATE if the service is not running.
 */
esp_err_t buzzer_service_get_volume(
    uint8_t *volume_percent
);

#ifdef __cplusplus
}
#endif
