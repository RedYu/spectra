#include "settings_model.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "app_config.h"

#define SETTINGS_SCHEMA_VERSION  (1U)
#define SETTINGS_LOCK_TIMEOUT_MS (100U)

static app_settings_t s_settings;
static SemaphoreHandle_t s_mutex = NULL;

static void settings_model_validate(
    app_settings_t *settings
)
{
    settings->device.name[
        SETTINGS_DEVICE_NAME_MAX_LENGTH - 1U
    ] = '\0';

    settings->device.target[
        SETTINGS_DEVICE_TARGET_MAX_LENGTH - 1U
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

    settings_model_validate(
        &validated
    );

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
