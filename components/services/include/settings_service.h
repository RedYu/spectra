#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "settings_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the settings service.
 *
 * Loads saved settings, applies default values when necessary, updates
 * the settings model and applies the resulting configuration to the
 * application services.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already
 * initialized, ESP_ERR_NO_MEM if the service mutex cannot be created,
 * ESP_ERR_TIMEOUT if a required lock cannot be acquired, otherwise an
 * ESP-IDF error code.
 */
esp_err_t settings_service_init(void);

/**
 * @brief Reload and apply settings from persistent storage.
 *
 * Replaces the current settings model with the stored settings and
 * applies them to the application services. Default settings are used
 * when the configuration file is missing or contains invalid data.
 *
 * Reloading may temporarily interrupt services whose runtime
 * configuration has changed.
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
 * @param[in] password Null-terminated password. An empty string
 * configures an open network; otherwise it must contain from 8 to
 * 63 bytes.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if an argument or
 * password is invalid, ESP_ERR_INVALID_SIZE if a credential is too
 * long, ESP_ERR_INVALID_STATE if a required service is not initialized,
 * ESP_ERR_NO_MEM if synchronization resources cannot be created,
 * ESP_ERR_TIMEOUT if a required lock cannot be acquired, otherwise an
 * ESP-IDF error code.
 */
esp_err_t settings_service_set_wifi_ap_credentials(
    const char *ssid,
    const char *password
);

/**
 * @brief Enable or disable the Wi-Fi Station interface.
 *
 * Updates the settings model and immediately applies the requested
 * Station state.
 *
 * Enabling STA requires a configured SSID and a matching credential
 * record stored in NVS. If credentials are missing or do not match the
 * credential identifier in the settings model, STA is not enabled.
 *
 * The setting is not automatically saved to the configuration file.
 *
 * @param[in] enabled True to enable the Wi-Fi Station interface.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the settings
 * service is not initialized or matching credentials are unavailable,
 * ESP_ERR_TIMEOUT if a required lock cannot be acquired, otherwise an
 * ESP-IDF error code.
 */
esp_err_t settings_service_set_wifi_sta_enabled(
    bool enabled
);

/**
 * @brief Store and apply Wi-Fi Station credentials.
 *
 * Stores the SSID and password in NVS, generates a new credential
 * identifier and updates the public Station settings with the SSID and
 * generated identifier. The password is never stored in the settings
 * model or configuration JSON.
 *
 * When the Station interface is running, it reconnects using the new
 * credentials.
 *
 * NVS credentials are committed immediately. The updated public
 * settings are not automatically saved to the configuration file;
 * call settings_service_save() after this function succeeds.
 *
 * @param[in] ssid Null-terminated Station SSID containing from 1 to
 * 32 bytes.
 * @param[in] password Null-terminated password. An empty string is
 * accepted for an open network; otherwise it must contain from 8 to
 * 63 bytes.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if an argument or
 * password is invalid, ESP_ERR_INVALID_SIZE if a credential is too
 * long, ESP_ERR_INVALID_STATE if a required service is not initialized,
 * ESP_ERR_NO_MEM if required resources cannot be allocated,
 * ESP_ERR_TIMEOUT if a required lock cannot be acquired, otherwise an
 * ESP-IDF error code.
 */
esp_err_t settings_service_set_wifi_sta_credentials(
    const char *ssid,
    const char *password
);

/**
 * @brief Remove stored Wi-Fi Station credentials.
 *
 * Disables the Station interface, removes its credentials from NVS and
 * clears the Station SSID and credential identifier in the settings
 * model.
 *
 * The updated public settings are not automatically saved to the
 * configuration file; call settings_service_save() after this function
 * succeeds.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if a required
 * service is not initialized, ESP_ERR_TIMEOUT if a required lock
 * cannot be acquired, otherwise an ESP-IDF error code.
 */
esp_err_t settings_service_clear_wifi_sta_credentials(void);

/**
 * @brief Check whether matching Wi-Fi Station credentials are stored.
 *
 * Credentials are considered configured only when the SSID and
 * credential identifier in the settings model match the credential
 * record stored in NVS.
 *
 * @param[out] configured Set to true when matching credentials exist.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if configured is NULL,
 * ESP_ERR_INVALID_STATE if a required service is not initialized,
 * ESP_ERR_TIMEOUT if a required lock cannot be acquired, otherwise an
 * ESP-IDF error code.
 */
esp_err_t settings_service_get_wifi_sta_credentials_configured(
    bool *configured
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
 * Applies display brightness, sound settings, SD-card logging,
 * per-tag logging levels, GUI animations, Wi-Fi states and the
 * primary CAN configuration.
 *
 * STA credentials are loaded separately from NVS and must match the
 * SSID and credential identifier stored in the settings model.
 *
 * USB RNDIS and the GUI theme are applied only during application
 * startup. Changing either setting may require a device restart.
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
 * from the active USB configuration or when the configured GUI theme
 * differs from the currently applied theme.
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

/**
 * @brief Configure and apply per-tag logging levels.
 *
 * Each argument contains a comma-separated list of ESP-IDF log tags.
 * Empty strings are accepted and clear the corresponding list.
 *
 * Tags previously managed by this setting are restored to the default
 * application log level before the new configuration is applied.
 *
 * If the same tag appears in multiple lists, the last applied level
 * takes precedence. The application order is warning, info, debug and
 * disabled, so disabled has the highest precedence.
 *
 * The settings model is updated and the new levels are applied
 * immediately. The setting is not automatically saved to persistent
 * storage; call settings_service_save() after this function succeeds.
 *
 * @param[in] warning_tags Tags whose level is set to ESP_LOG_WARN.
 * @param[in] info_tags Tags whose level is set to ESP_LOG_INFO.
 * @param[in] debug_tags Tags whose level is set to ESP_LOG_DEBUG.
 * @param[in] disabled_tags Tags whose logging is disabled.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if an argument or
 * tag-list format is invalid, ESP_ERR_INVALID_SIZE if a list or tag
 * is too long, ESP_ERR_INVALID_STATE if a required service is not
 * initialized, ESP_ERR_TIMEOUT if a required lock cannot be acquired,
 * otherwise an ESP-IDF error code.
 */
esp_err_t settings_service_set_log_tag_levels(
    const char *warning_tags,
    const char *info_tags,
    const char *debug_tags,
    const char *disabled_tags
);

/**
 * @brief Select the GUI theme for the next application start.
 *
 * Updates the settings model but does not modify existing LVGL styles.
 * The selected theme takes effect after the device is restarted.
 * The setting is not automatically saved to persistent storage; call
 * settings_service_save() after this function succeeds.
 *
 * @param[in] theme_mode Requested light or dark GUI theme.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if theme_mode is
 * invalid, ESP_ERR_INVALID_STATE if the settings service is not
 * initialized, ESP_ERR_TIMEOUT if the service lock cannot be acquired,
 * otherwise an ESP-IDF error code.
 */
esp_err_t settings_service_set_theme_mode(
    ui_theme_mode_t theme_mode
);

/**
 * @brief Record the GUI theme applied during application startup.
 *
 * Call this function only after the GUI theme has been initialized
 * successfully. The stored value is used to determine whether changing
 * the configured theme requires a device restart.
 *
 * @param[in] theme_mode Theme mode currently used by the GUI.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if theme_mode is
 * invalid, ESP_ERR_INVALID_STATE if the service is not initialized,
 * ESP_ERR_TIMEOUT if the service lock cannot be acquired, otherwise an
 * ESP-IDF error code.
 */
esp_err_t settings_service_mark_theme_applied(
    ui_theme_mode_t theme_mode
);

/**
 * @brief Configure and immediately apply audible feedback.
 *
 * Updates the sound settings in the model and applies them to the
 * buzzer service. The setting is not automatically saved; call
 * settings_service_save() after this function succeeds.
 *
 * @param[in] enabled True to enable audible feedback.
 * @param[in] volume_percent Volume from 0 to
 * SETTINGS_SOUND_VOLUME_MAX percent.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if volume_percent is
 * invalid, ESP_ERR_INVALID_STATE if a required service is not running,
 * ESP_ERR_TIMEOUT if a lock cannot be acquired, otherwise an ESP-IDF
 * error code.
 */
esp_err_t settings_service_set_sound(
    bool enabled,
    uint8_t volume_percent
);

/**
 * @brief Configure and immediately apply the primary CAN interface.
 *
 * Enabling starts the CAN service. Disabling stops it. Changing the
 * bitrate or operating mode recreates the TWAI controller without
 * restarting the device.
 *
 * The settings are updated in memory but are not automatically saved;
 * call settings_service_save() after this function succeeds.
 *
 * @param[in] enabled True to enable the primary CAN interface.
 * @param[in] bitrate Classical CAN bitrate in bits per second.
 * @param[in] listen_only True for passive monitoring.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if the configuration
 * is invalid, ESP_ERR_INVALID_STATE if a required service is not
 * initialized, ESP_ERR_TIMEOUT if a lock cannot be acquired, otherwise
 * an ESP-IDF error code.
 */
esp_err_t settings_service_set_can_primary(
    bool enabled,
    uint32_t bitrate,
    bool listen_only
);

/**
 * @brief Configure and immediately apply the secondary CAN interface.
 *
 * The secondary interface uses the MCP2518FD controller and supports
 * both Classical CAN and CAN FD. Enabling starts the service,
 * disabling stops it, and changing an active configuration performs a
 * runtime reconfiguration.
 *
 * Without BRS, data_bitrate must equal nominal_bitrate. BRS requires
 * CAN FD to be enabled and data_bitrate to be higher than
 * nominal_bitrate.
 *
 * The settings are not automatically saved. Call
 * settings_service_save() after this function succeeds.
 */
esp_err_t settings_service_set_can_secondary(
    bool enabled,
    uint32_t nominal_bitrate,
    uint32_t data_bitrate,
    bool fd_enabled,
    bool brs_enabled,
    bool listen_only
);

#ifdef __cplusplus
}
#endif
