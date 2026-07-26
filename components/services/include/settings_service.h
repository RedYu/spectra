#pragma once

#include "esp_err.h"

esp_err_t settings_service_init(void);

esp_err_t settings_service_reload(void);

esp_err_t settings_service_set_brightness(
    uint8_t brightness
);

/**
 * @brief Enable or disable SD card logging.
 */
esp_err_t settings_service_set_sd_logging_enabled(
    bool enabled
);

/**
 * @brief Enable or disable GUI animations.
 */
esp_err_t settings_service_set_animations_enabled(
    bool enabled
);

esp_err_t settings_service_save(void);

esp_err_t settings_service_apply(void);
