#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUZZER_FREQUENCY_MIN_HZ    (100U)
#define BUZZER_FREQUENCY_MAX_HZ    (10000U)

/**
 * @brief Initialize the passive buzzer PWM driver.
 *
 * The buzzer output remains inactive after initialization.
 * Repeated calls are accepted.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t buzzer_driver_init(void);

/**
 * @brief Stop the buzzer and release PWM resources.
 *
 * Calling this function when the driver is not initialized has no
 * effect.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t buzzer_driver_deinit(void);

/**
 * @brief Start generating a continuous tone.
 *
 * @param[in] frequency_hz Tone frequency in hertz.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG when the frequency is
 * outside the supported range, ESP_ERR_INVALID_STATE if the driver is
 * not initialized, otherwise an ESP-IDF error code.
 */
esp_err_t buzzer_driver_start_tone(
    uint32_t frequency_hz
);

/**
 * @brief Stop the currently generated tone.
 *
 * The GPIO is returned to the inactive level.
 *
 * @return ESP_OK on success or ESP_ERR_INVALID_STATE if the driver is
 * not initialized.
 */
esp_err_t buzzer_driver_stop(void);

/**
 * @brief Check whether the buzzer driver is initialized.
 */
bool buzzer_driver_is_initialized(void);

/**
 * @brief Check whether a tone is currently active.
 */
bool buzzer_driver_is_active(void);

#ifdef __cplusplus
}
#endif
