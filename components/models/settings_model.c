#include "settings_model.h"

#include <string.h>
#include "app_config.h"

#define SETTINGS_SCHEMA_VERSION  1

static app_settings_t s_settings;

void settings_model_set_defaults(app_settings_t *settings)
{
    if (settings == NULL) {
        return;
    }

    memset(settings, 0, sizeof(*settings));

    settings->schema_version = SETTINGS_SCHEMA_VERSION;

    strlcpy(
        settings->device.target,
        APP_TARGET,
        sizeof(settings->device.target)
    );

    strlcpy(
        settings->device.name,
        APP_NAME,
        sizeof(settings->device.name)
    );

    settings->display.brightness =
        SETTINGS_DISPLAY_BRIGHTNESS_DEFAULT;
}

void settings_model_set(const app_settings_t *settings)
{
    if (settings == NULL) {
        return;
    }

    s_settings = *settings;

    /*
     * Ensure strings are always null-terminated.
     */
    s_settings.device.name[
        SETTINGS_DEVICE_NAME_MAX_LENGTH - 1
    ] = '\0';

    s_settings.device.target[
        SETTINGS_DEVICE_TARGET_MAX_LENGTH - 1
    ] = '\0';

    /*
     * Ensure display brightness is within valid range.
     */
    if (s_settings.display.brightness < SETTINGS_DISPLAY_BRIGHTNESS_MIN) {
        s_settings.display.brightness = SETTINGS_DISPLAY_BRIGHTNESS_MIN;
    }
    if (s_settings.display.brightness > SETTINGS_DISPLAY_BRIGHTNESS_MAX) {
        s_settings.display.brightness = SETTINGS_DISPLAY_BRIGHTNESS_MAX;
    }
}

const app_settings_t *settings_model_get(void)
{
    return &s_settings;
}
