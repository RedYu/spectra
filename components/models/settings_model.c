#include "settings_model.h"

#include <string.h>
#include "app_config.h"

static app_settings_t s_settings;

void settings_model_set_defaults(app_settings_t *settings)
{
    if (settings == NULL) {
        return;
    }

    memset(settings, 0, sizeof(*settings));

    settings->schema_version = 1;

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
}

void settings_model_set(const app_settings_t *settings)
{
    if (settings == NULL) {
        return;
    }

    s_settings = *settings;
}

const app_settings_t *settings_model_get(void)
{
    return &s_settings;
}
