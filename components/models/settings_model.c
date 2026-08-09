#include "settings_model.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "app_config.h"

#define SETTINGS_LOCK_TIMEOUT_MS (100U)

static app_settings_t s_settings;
static SemaphoreHandle_t s_mutex = NULL;

static esp_err_t settings_model_validate(
    app_settings_t *settings
)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (settings->schema_version !=
        SETTINGS_SCHEMA_VERSION) {

        return ESP_ERR_INVALID_VERSION;
    }

    settings->device.name[
        SETTINGS_DEVICE_NAME_MAX_LENGTH - 1U
    ] = '\0';

    settings->device.target[
        SETTINGS_DEVICE_TARGET_MAX_LENGTH - 1U
    ] = '\0';

    settings->wifi_ap.ssid[
        SETTINGS_WIFI_AP_SSID_MAX_LENGTH - 1U
    ] = '\0';

    settings->wifi_ap.password[
        SETTINGS_WIFI_PASSWORD_MAX_LENGTH - 1U
    ] = '\0';

    settings->wifi_sta.ssid[
        SETTINGS_WIFI_STA_SSID_MAX_LENGTH - 1U
    ] = '\0';

    settings->wifi_sta.credential_id[
        SETTINGS_WIFI_CREDENTIAL_ID_LENGTH - 1U
    ] = '\0';

    settings->logging.warning_tags[
        SETTINGS_LOG_TAG_LIST_MAX_LENGTH - 1U
    ] = '\0';

    settings->logging.info_tags[
        SETTINGS_LOG_TAG_LIST_MAX_LENGTH - 1U
    ] = '\0';

    settings->logging.debug_tags[
        SETTINGS_LOG_TAG_LIST_MAX_LENGTH - 1U
    ] = '\0';

    settings->logging.disabled_tags[
        SETTINGS_LOG_TAG_LIST_MAX_LENGTH - 1U
    ] = '\0';

    if (settings->display.brightness <
        SETTINGS_DISPLAY_BRIGHTNESS_MIN) {

        settings->display.brightness =
            SETTINGS_DISPLAY_BRIGHTNESS_MIN;

    } else if (settings->display.brightness >
               SETTINGS_DISPLAY_BRIGHTNESS_MAX) {

        settings->display.brightness =
            SETTINGS_DISPLAY_BRIGHTNESS_MAX;
    }

    if ((settings->ui.theme_mode != UI_THEME_MODE_LIGHT) &&
        (settings->ui.theme_mode != UI_THEME_MODE_DARK)) {

        settings->ui.theme_mode =
            SETTINGS_UI_THEME_MODE_DEFAULT;
    }

    const size_t ap_ssid_length =
        strlen(
            settings->wifi_ap.ssid
        );

    /*
     * A valid base SSID is required even when SoftAP is disabled so
     * the interface can be enabled later without additional setup.
     */
    if (ap_ssid_length == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t ap_password_length =
        strlen(
            settings->wifi_ap.password
        );

    /*
     * An empty password configures an open network. Otherwise, the
     * password must contain from 8 to 63 bytes.
     */
    if ((ap_password_length != 0U) &&
        (ap_password_length <
         SETTINGS_WIFI_PASSWORD_MIN_LENGTH)) {

        return ESP_ERR_INVALID_ARG;
    }

    const size_t sta_ssid_length =
        strlen(
            settings->wifi_sta.ssid
        );

    const size_t credential_id_length =
        strlen(
            settings->wifi_sta.credential_id
        );

    /*
     * STA may remain unconfigured while disabled. Enabling it requires
     * both a selected SSID and a reference to credentials stored in
     * NVS.
     */
    if (settings->wifi_sta.enabled &&
        ((sta_ssid_length == 0U) ||
         (credential_id_length == 0U))) {

        return ESP_ERR_INVALID_ARG;
    }

    /*
     * A configured credential identifier must contain exactly
     * 16 hexadecimal characters.
     */
    if ((credential_id_length != 0U) &&
        (credential_id_length !=
         (SETTINGS_WIFI_CREDENTIAL_ID_LENGTH - 1U))) {

        return ESP_ERR_INVALID_SIZE;
    }

    for (size_t index = 0U;
         index < credential_id_length;
         ++index) {

        const char character =
            settings->wifi_sta.credential_id[index];

        const bool is_digit =
            (character >= '0') &&
            (character <= '9');

        const bool is_lower_hex =
            (character >= 'a') &&
            (character <= 'f');

        const bool is_upper_hex =
            (character >= 'A') &&
            (character <= 'F');

        if (!is_digit &&
            !is_lower_hex &&
            !is_upper_hex) {

            return ESP_ERR_INVALID_ARG;
        }
    }

    return ESP_OK;
}

esp_err_t settings_model_init(void)
{
    if (s_mutex != NULL) {
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();

    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    app_settings_t defaults;

    const esp_err_t result =
        settings_model_set_defaults(
            &defaults
        );

    if (result != ESP_OK) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;

        return result;
    }

    s_settings = defaults;

    return ESP_OK;
}

esp_err_t settings_model_set_defaults(
    app_settings_t *settings
)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(
        settings,
        0,
        sizeof(*settings)
    );

    settings->schema_version =
        SETTINGS_SCHEMA_VERSION;

    (void)strlcpy(
        settings->device.target,
        SPECTRA_APP_TARGET,
        sizeof(settings->device.target)
    );

    (void)strlcpy(
        settings->device.name,
        SPECTRA_APP_NAME,
        sizeof(settings->device.name)
    );

    settings->display.brightness =
        SETTINGS_DISPLAY_BRIGHTNESS_DEFAULT;

    settings->logging.sd_enabled =
        SETTINGS_LOGGING_SD_ENABLED_DEFAULT;

    (void)strlcpy(
        settings->logging.warning_tags,
        SETTINGS_LOG_WARNING_TAGS_DEFAULT,
        sizeof(settings->logging.warning_tags)
    );

    (void)strlcpy(
        settings->logging.info_tags,
        SETTINGS_LOG_INFO_TAGS_DEFAULT,
        sizeof(settings->logging.info_tags)
    );

    (void)strlcpy(
        settings->logging.debug_tags,
        SETTINGS_LOG_DEBUG_TAGS_DEFAULT,
        sizeof(settings->logging.debug_tags)
    );

    (void)strlcpy(
        settings->logging.disabled_tags,
        SETTINGS_LOG_DISABLED_TAGS_DEFAULT,
        sizeof(settings->logging.disabled_tags)
    );

    settings->ui.animations_enabled =
        SETTINGS_UI_ANIMATIONS_ENABLED_DEFAULT;

    settings->ui.theme_mode =
        SETTINGS_UI_THEME_MODE_DEFAULT;

    settings->wifi_ap.enabled =
        SETTINGS_WIFI_AP_ENABLED_DEFAULT;

    (void)strlcpy(
        settings->wifi_ap.ssid,
        SETTINGS_WIFI_AP_SSID_DEFAULT,
        sizeof(settings->wifi_ap.ssid)
    );

    (void)strlcpy(
        settings->wifi_ap.password,
        SETTINGS_WIFI_AP_PASSWORD_DEFAULT,
        sizeof(settings->wifi_ap.password)
    );

    settings->wifi_sta.enabled =
        SETTINGS_WIFI_STA_ENABLED_DEFAULT;

    settings->wifi_sta.ssid[0] =
        '\0';

    settings->wifi_sta.credential_id[0] =
        '\0';

    settings->usb_rndis.enabled =
        SETTINGS_USB_RNDIS_ENABLED_DEFAULT;

    return ESP_OK;
}

esp_err_t settings_model_set(
    const app_settings_t *settings
)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Validate the local copy before modifying shared state.
     */
    app_settings_t validated = *settings;

    const esp_err_t validation_result =
        settings_model_validate(
            &validated
        );

    if (validation_result != ESP_OK) {
        return validation_result;
    }

    if (xSemaphoreTake(
            s_mutex,
            pdMS_TO_TICKS(
                SETTINGS_LOCK_TIMEOUT_MS
            )
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    s_settings = validated;

    (void)xSemaphoreGive(s_mutex);

    return ESP_OK;
}

esp_err_t settings_model_get(
    app_settings_t *settings
)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * Provide a safe value if reading the model fails.
     */
    memset(
        settings,
        0,
        sizeof(*settings)
    );

    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_mutex,
            pdMS_TO_TICKS(
                SETTINGS_LOCK_TIMEOUT_MS
            )
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    *settings = s_settings;

    (void)xSemaphoreGive(s_mutex);

    return ESP_OK;
}
