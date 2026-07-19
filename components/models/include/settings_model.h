#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SETTINGS_DEVICE_NAME_MAX_LENGTH  32
#define SETTINGS_DEVICE_TARGET_MAX_LENGTH  32

typedef struct
{
    char target[SETTINGS_DEVICE_TARGET_MAX_LENGTH];
    char name[SETTINGS_DEVICE_NAME_MAX_LENGTH];
} device_settings_t;

typedef struct
{
    uint32_t schema_version;

    device_settings_t device;
} app_settings_t;

void settings_model_set_defaults(app_settings_t *settings);

void settings_model_set(const app_settings_t *settings);

const app_settings_t *settings_model_get(void);
