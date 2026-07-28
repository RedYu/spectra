#include "display_backlight.h"

#include <inttypes.h>
#include <stdbool.h>

#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"

#include "board_config.h"

#define BACKLIGHT_LEDC_MODE       (LEDC_LOW_SPEED_MODE)
#define BACKLIGHT_LEDC_TIMER      (LEDC_TIMER_0)
#define BACKLIGHT_LEDC_CHANNEL    (LEDC_CHANNEL_0)

#define BACKLIGHT_PWM_FREQUENCY   (20000U)
#define BACKLIGHT_PWM_RESOLUTION  (LEDC_TIMER_10_BIT)
#define BACKLIGHT_PWM_BITS        (10U)

#define BACKLIGHT_DUTY_MAX \
    ((1UL << BACKLIGHT_PWM_BITS) - 1UL)

static const char *TAG = "display_backlight";

static uint8_t s_brightness = DISPLAY_BRIGHTNESS_OFF;
static bool s_initialized = false;

static bool display_backlight_brightness_is_valid(
    uint8_t brightness
)
{
    return
        (brightness == DISPLAY_BRIGHTNESS_OFF) ||
        ((brightness >= DISPLAY_BRIGHTNESS_MIN) &&
         (brightness <= DISPLAY_BRIGHTNESS_MAX));
}

esp_err_t display_backlight_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    const ledc_timer_config_t timer_config = {
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .duty_resolution = BACKLIGHT_PWM_RESOLUTION,
        .timer_num = BACKLIGHT_LEDC_TIMER,
        .freq_hz = BACKLIGHT_PWM_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };

    ESP_RETURN_ON_ERROR(
        ledc_timer_config(&timer_config),
        TAG,
        "Failed to configure LEDC timer"
    );

    /*
     * Start with the backlight switched off to avoid a visible
     * flash while the rest of the board is being initialized.
     */
    const ledc_channel_config_t channel_config = {
        .gpio_num = LCD_PIN_BACKLIGHT,
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .channel = BACKLIGHT_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BACKLIGHT_LEDC_TIMER,
        .duty = 0U,
        .hpoint = 0,
        .flags.output_invert = 0U,
    };

    ESP_RETURN_ON_ERROR(
        ledc_channel_config(&channel_config),
        TAG,
        "Failed to configure LEDC channel"
    );

    s_brightness = DISPLAY_BRIGHTNESS_OFF;
    s_initialized = true;

    ESP_LOGI(
        TAG,
        "Backlight initialized on GPIO %d",
        LCD_PIN_BACKLIGHT
    );

    return ESP_OK;
}

esp_err_t display_backlight_set_brightness(
    uint8_t brightness
)
{
    if (!s_initialized) {
        ESP_LOGE(
            TAG,
            "Backlight is not initialized"
        );

        return ESP_ERR_INVALID_STATE;
    }

    if (!display_backlight_brightness_is_valid(brightness)) {
        ESP_LOGE(
            TAG,
            "Invalid brightness: %u",
            (unsigned int)brightness
        );

        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t duty =
        ((uint32_t)brightness * BACKLIGHT_DUTY_MAX) /
        DISPLAY_BRIGHTNESS_MAX;

    ESP_RETURN_ON_ERROR(
        ledc_set_duty(
            BACKLIGHT_LEDC_MODE,
            BACKLIGHT_LEDC_CHANNEL,
            duty
        ),
        TAG,
        "Failed to set backlight duty"
    );

    ESP_RETURN_ON_ERROR(
        ledc_update_duty(
            BACKLIGHT_LEDC_MODE,
            BACKLIGHT_LEDC_CHANNEL
        ),
        TAG,
        "Failed to update backlight duty"
    );

    s_brightness = brightness;

    ESP_LOGD(
        TAG,
        "Brightness: %u%%, duty: %" PRIu32,
        (unsigned int)brightness,
        duty
    );

    return ESP_OK;
}

uint8_t display_backlight_get_brightness(void)
{
    return s_brightness;
}
