#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_SCHEMA_VERSION                    (1U)

#define SETTINGS_DEVICE_NAME_MAX_LENGTH            (32U)
#define SETTINGS_DEVICE_TARGET_MAX_LENGTH          (32U)

#define SETTINGS_DISPLAY_BRIGHTNESS_MIN            (10U)
#define SETTINGS_DISPLAY_BRIGHTNESS_MAX            (100U)
#define SETTINGS_DISPLAY_BRIGHTNESS_DEFAULT        (80U)

#define SETTINGS_LOGGING_SD_ENABLED_DEFAULT        (false)
#define SETTINGS_UI_ANIMATIONS_ENABLED_DEFAULT     (false)

/*
 * Password limits used by SoftAP settings and STA credentials stored
 * separately in NVS. The maximum length includes the null terminator.
 */
#define SETTINGS_WIFI_PASSWORD_MIN_LENGTH          (8U)
#define SETTINGS_WIFI_PASSWORD_MAX_LENGTH          (64U)

/*
 * The SoftAP base SSID reserves seven characters for the hyphen and
 * six-character MAC suffix.
 */
#define SETTINGS_WIFI_AP_SSID_MAX_LENGTH           (26U)

#define SETTINGS_WIFI_AP_ENABLED_DEFAULT           (true)
#define SETTINGS_WIFI_AP_SSID_DEFAULT              ("Spectra")
#define SETTINGS_WIFI_AP_PASSWORD_DEFAULT          ("spectra123")

#define SETTINGS_WIFI_STA_SSID_MAX_LENGTH          (33U)
/*
 * A credential identifier contains 16 hexadecimal characters and a
 * terminating null character.
 */
#define SETTINGS_WIFI_CREDENTIAL_ID_LENGTH         (17U)
#define SETTINGS_WIFI_STA_ENABLED_DEFAULT          (false)

#define SETTINGS_USB_RNDIS_ENABLED_DEFAULT         (true)

#define SETTINGS_LOG_TAG_LIST_MAX_LENGTH  (256U)

#define SETTINGS_LOG_WARNING_TAGS_DEFAULT \
    ("tusb_desc,dns_redirect_server,wifi,wifi_init," \
     "settings_service,esp-x509-crt-bundle")

#define SETTINGS_LOG_INFO_TAGS_DEFAULT      ("")
#define SETTINGS_LOG_DEBUG_TAGS_DEFAULT     ("")
#define SETTINGS_LOG_DISABLED_TAGS_DEFAULT  ("")

#define SETTINGS_UI_THEME_MODE_DEFAULT \
    UI_THEME_MODE_LIGHT

typedef struct
{
    char target[SETTINGS_DEVICE_TARGET_MAX_LENGTH];
    char name[SETTINGS_DEVICE_NAME_MAX_LENGTH];

} device_settings_t;

typedef struct
{
    uint8_t brightness;

} display_settings_t;

typedef struct
{
    bool sd_enabled;

    char warning_tags[
        SETTINGS_LOG_TAG_LIST_MAX_LENGTH
    ];

    char info_tags[
        SETTINGS_LOG_TAG_LIST_MAX_LENGTH
    ];

    char debug_tags[
        SETTINGS_LOG_TAG_LIST_MAX_LENGTH
    ];

    char disabled_tags[
        SETTINGS_LOG_TAG_LIST_MAX_LENGTH
    ];

} logging_settings_t;

typedef enum
{
    UI_THEME_MODE_LIGHT = 0,
    UI_THEME_MODE_DARK,

} ui_theme_mode_t;

typedef struct
{
    bool animations_enabled;
    ui_theme_mode_t theme_mode;

} ui_settings_t;

/**
 * @brief Wi-Fi SoftAP settings.
 *
 * The SSID field contains the base name. The Wi-Fi service appends a
 * hyphen and the final six hexadecimal characters of the SoftAP MAC
 * address to produce the advertised SSID.
 *
 * An empty password configures an open network. Otherwise, the
 * password must contain from 8 to 63 bytes.
 */
typedef struct
{
    bool enabled;

    char ssid[
        SETTINGS_WIFI_AP_SSID_MAX_LENGTH
    ];

    char password[
        SETTINGS_WIFI_PASSWORD_MAX_LENGTH
    ];

} wifi_ap_settings_t;

/**
 * @brief Wi-Fi station settings.
 *
 * The SSID identifies the last configured station network.
 * The credential identifier associates these public settings with
 * credentials stored separately in NVS.
 *
 * The credential identifier is not secret. It changes whenever the
 * stored STA credentials are replaced.
 */
typedef struct
{
    bool enabled;

    char ssid[
        SETTINGS_WIFI_STA_SSID_MAX_LENGTH
    ];

    char credential_id[
        SETTINGS_WIFI_CREDENTIAL_ID_LENGTH
    ];

} wifi_sta_settings_t;

/**
 * @brief USB RNDIS settings.
 */
typedef struct
{
    /*
     * Changes to this setting are applied after restart.
     */
    bool enabled;

} usb_rndis_settings_t;

typedef struct
{
    uint32_t schema_version;

    device_settings_t device;
    display_settings_t display;
    logging_settings_t logging;
    ui_settings_t ui;

    wifi_ap_settings_t wifi_ap;
    wifi_sta_settings_t wifi_sta;
    usb_rndis_settings_t usb_rndis;

} app_settings_t;

/**
 * @brief Initialize the settings model and its synchronization resources.
 *
 * @return ESP_OK on success, ESP_ERR_NO_MEM if synchronization
 * resources cannot be created, otherwise an ESP-IDF error code.
 */
esp_err_t settings_model_init(void);

/**
 * @brief Fill a settings structure with default values.
 *
 * @param[out] settings Settings structure to initialize.
 *
 * @return ESP_OK on success or ESP_ERR_INVALID_ARG if settings is NULL.
 */
esp_err_t settings_model_set_defaults(
    app_settings_t *settings
);

/**
 * @brief Validate and store the current application settings.
 *
 * @param[in] settings Settings to store.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if settings is NULL,
 * ESP_ERR_INVALID_STATE if the model is not initialized, or
 * ESP_ERR_TIMEOUT if the model lock cannot be acquired.
 */
esp_err_t settings_model_set(
    const app_settings_t *settings
);

/**
 * @brief Copy the current application settings.
 *
 * @param[out] settings Destination settings structure.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if settings is NULL,
 * ESP_ERR_INVALID_STATE if the model is not initialized, or
 * ESP_ERR_TIMEOUT if the model lock cannot be acquired.
 */
esp_err_t settings_model_get(
    app_settings_t *settings
);

#ifdef __cplusplus
}
#endif
