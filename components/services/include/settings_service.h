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
 * @brief Enable or disable the Wi-Fi SoftAP.
 *
 * Updates the settings model and immediately starts or stops the
 * SoftAP. The setting is not automatically saved to persistent
 * storage.
 *
 * When disabling Wi-Fi from a client connected through the SoftAP,
 * the current network connection will be closed.
 *
 * @param[in] enabled True to enable the Wi-Fi SoftAP.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if a required
 * service is not initialized, ESP_ERR_TIMEOUT if a required lock
 * cannot be acquired, otherwise an ESP-IDF error code.
 */
esp_err_t settings_service_set_wifi_ap_enabled(
    bool enabled
);

/**
 * @brief Enable or disable USB RNDIS for the next application start.
 *
 * Updates the settings model but does not reconfigure the active USB
 * device. The new value takes effect after the device is restarted.
 * The setting is not automatically saved to persistent storage.
 *
 * @param[in] enabled True to enable USB RNDIS.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the settings
 * service is not initialized, ESP_ERR_TIMEOUT if a required lock
 * cannot be acquired, otherwise an ESP-IDF error code.
 */
esp_err_t settings_service_set_usb_rndis_enabled(
    bool enabled
);

/**
 * @brief Set and apply the Wi-Fi SoftAP credentials.
 *
 * Updates the base SSID and password in the settings model and applies
 * them to the Wi-Fi service. The final SSID contains the configured
 * base name followed by a hyphen and the final six hexadecimal
 * characters of the SoftAP MAC address.
 *
 * For example, the base SSID "Spectra" may be advertised as
 * "Spectra-A1B2C3".
 *
 * When the SoftAP is already running, applying new credentials may
 * disconnect all connected Wi-Fi clients. The settings are not
 * automatically saved to persistent storage.
 *
 * @param[in] ssid Null-terminated base SoftAP SSID containing from
 * 1 to 25 bytes.
 * @param[in] password Null-terminated WPA2 password containing from
 * 8 to 63 bytes.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if an argument or
 * password is invalid, ESP_ERR_INVALID_SIZE if a credential is too
 * long, ESP_ERR_INVALID_STATE if a required service is not initialized,
 * ESP_ERR_NO_MEM if synchronization resources cannot be created,
 * ESP_ERR_TIMEOUT if a required lock cannot be acquired, otherwise an
 * ESP-IDF error code.
 */
esp_err_t settings_service_set_wifi_credentials(
    const char *ssid,
    const char *password
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
 * Applies display brightness, SD-card logging, GUI animations and the
 * Wi-Fi SoftAP state and credentials. The USB RNDIS setting is applied
 * only during application startup because changing USB descriptors
 * requires re-enumeration.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if a required
 * service is not initialized, ESP_ERR_TIMEOUT if a required lock
 * cannot be acquired, otherwise an ESP-IDF error code.
 */
esp_err_t settings_service_apply(void);

/**
 * @brief Check whether applying saved settings requires a restart.
 *
 * A restart is required when the requested USB RNDIS state differs
 * from the currently active USB configuration.
 *
 * @param[out] restart_required Set to true when the device must be
 * restarted to apply all current settings.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if restart_required
 * is NULL, ESP_ERR_INVALID_STATE if the settings service or a required
 * service is not initialized, ESP_ERR_TIMEOUT if a required lock
 * cannot be acquired, otherwise an ESP-IDF error code.
 */
esp_err_t settings_service_get_restart_required(
    bool *restart_required
);

#ifdef __cplusplus
}
#endif
