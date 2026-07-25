#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DISPLAY_BRIGHTNESS_MIN  10
#define DISPLAY_BRIGHTNESS_MAX  100

/**
 * @brief Initialize display backlight PWM.
 */
esp_err_t display_backlight_init(void);

/**
 * @brief Set display brightness.
 *
 * @param brightness Brightness from 10 to 100 percent.
 */
esp_err_t display_backlight_set_brightness(
    uint8_t brightness
);

/**
 * @brief Get current display brightness.
 */
uint8_t display_backlight_get_brightness(void);

#ifdef __cplusplus
}
#endif
