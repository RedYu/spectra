#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the settings service.
 *
 * Loads saved settings, applies default values when necessary and
 * updates the settings model.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already
 * initialized, ESP_ERR_NO_MEM if the service mutex cannot be created,
 * ESP_ERR_TIMEOUT if a required lock cannot be acquired, otherwise an
 * ESP-IDF error code.
 */
esp_err_t settings_service_init(void);

/**
 * @brief Reload settings from persistent storage.
 *
 * Replaces the current settings model with the stored settings.
 * Default settings are used when the configuration file is missing
 * or contains invalid data.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not initialized, ESP_ERR_TIMEOUT if a required lock cannot be
 * acquired, otherwise an ESP-IDF error code.
 */
esp_err_t settings_service_reload(void);

/**
 * @brief Set and apply the display brightness.
 *
 * The value is validated and stored in the settings model, then applied
 * to the display backlight. It is not automatically saved to persistent
 * storage.
 *
 * @param[in] brightness Requested brightness percentage.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not initialized, ESP_ERR_TIMEOUT if a required lock cannot be
 * acquired, otherwise an ESP-IDF error code.
 */
esp_err_t settings_service_set_brightness(
    uint8_t brightness
);

/**
 * @brief Enable or disable SD-card logging.
 *
 * Updates the settings model and applies the requested logging state.
 * Enabling the preference without a mounted SD card is allowed; file
 * logging can be started later when the card becomes available. The
 * setting is not automatically saved to persistent storage.
 *
 * @param[in] enabled True to enable SD-card logging.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if a required
 * service is not initialized, ESP_ERR_TIMEOUT if a required lock
 * cannot be acquired, otherwise an ESP-IDF error code.
 */
esp_err_t settings_service_set_sd_logging_enabled(
    bool enabled
);

/**
 * @brief Enable or disable GUI animations.
 *
 * Updates the settings model and immediately applies the animation
 * state. The setting is not automatically saved to persistent storage.
 *
 * @param[in] enabled True to enable GUI animations.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not initialized, ESP_ERR_TIMEOUT if a required lock cannot be
 * acquired, otherwise an ESP-IDF error code.
 */
esp_err_t settings_service_set_animations_enabled(
    bool enabled
);

/**
 * @brief Save the current settings to persistent storage.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not initialized, ESP_ERR_NO_MEM if JSON serialization memory cannot
 * be allocated, ESP_ERR_TIMEOUT if a required lock cannot be acquired,
 * otherwise an ESP-IDF error code.
 */
esp_err_t settings_service_save(void);

/**
 * @brief Apply the current settings to application services.
 *
 * Applies settings such as display brightness, SD-card logging and
 * GUI-animation state.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if a required
 * service is not initialized, ESP_ERR_TIMEOUT if a required lock
 * cannot be acquired, otherwise an ESP-IDF error code.
 */
esp_err_t settings_service_apply(void);

#ifdef __cplusplus
}
#endif
