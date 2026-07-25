#pragma once

#include "esp_err.h"

esp_err_t settings_service_init(void);

esp_err_t settings_service_reload(void);

esp_err_t settings_service_set_brightness(
    uint8_t brightness
);

