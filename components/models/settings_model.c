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
        SETTINGS_WIFI_SSID_MAX_LENGTH - 1U
    ] = '\0';

    settings->wifi_ap.password[
        SETTINGS_WIFI_PASSWORD_MAX_LENGTH - 1U
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

    const size_t ssid_length =
        strlen(
            settings->wifi_ap.ssid
        );

    if ((ssid_length == 0U) ||
        (ssid_length >=
         SETTINGS_WIFI_SSID_MAX_LENGTH)) {

        return ESP_ERR_INVALID_ARG;
    }

    const size_t password_length =
        strlen(
            settings->wifi_ap.password
        );

    /*
     * An empty password configures an open network. Otherwise, the
     * password must contain from 8 to 63 bytes.
     */
    if ((password_length != 0U) &&
        (password_length <
        SETTINGS_WIFI_PASSWORD_MIN_LENGTH)) {

        return ESP_ERR_INVALID_ARG;
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

    settings->ui.animations_enabled =
        SETTINGS_UI_ANIMATIONS_ENABLED_DEFAULT;

    settings->wifi_ap.enabled =
        SETTINGS_WIFI_AP_ENABLED_DEFAULT;

    settings->usb_rndis.enabled =
        SETTINGS_USB_RNDIS_ENABLED_DEFAULT;

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
