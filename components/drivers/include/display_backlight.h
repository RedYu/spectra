#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DISPLAY_BRIGHTNESS_OFF  (0U)
#define DISPLAY_BRIGHTNESS_MIN  (10U)
#define DISPLAY_BRIGHTNESS_MAX  (100U)

/**
 * @brief Initialize the display backlight PWM.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t display_backlight_init(void);

/**
 * @brief Set the display brightness.
 *
 * @param brightness Brightness in percent. Use
 * DISPLAY_BRIGHTNESS_OFF to turn the backlight off, or a value
 * from DISPLAY_BRIGHTNESS_MIN to DISPLAY_BRIGHTNESS_MAX.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if the brightness
 * value is outside the supported range, otherwise an ESP-IDF error code.
 */
esp_err_t display_backlight_set_brightness(
    uint8_t brightness
);

/**
 * @brief Get the current display brightness.
 *
 * @return Current brightness in percent, or DISPLAY_BRIGHTNESS_OFF
 * when the backlight is off.
 */
uint8_t display_backlight_get_brightness(void);

#ifdef __cplusplus
}
#endif
