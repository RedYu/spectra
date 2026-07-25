#include "display_backlight.h"

#include <inttypes.h>

#include "board_config.h"

#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "display_backlight";

#define BACKLIGHT_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define BACKLIGHT_LEDC_TIMER      LEDC_TIMER_0
#define BACKLIGHT_LEDC_CHANNEL    LEDC_CHANNEL_0

#define BACKLIGHT_PWM_FREQUENCY   20000
#define BACKLIGHT_PWM_RESOLUTION  LEDC_TIMER_10_BIT

#define BACKLIGHT_DUTY_MAX        ((1U << 10) - 1U)

static uint8_t s_brightness = 100;

esp_err_t display_backlight_init(void)
{
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

    const ledc_channel_config_t channel_config = {
        .gpio_num = LCD_PIN_BACKLIGHT,
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .channel = BACKLIGHT_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BACKLIGHT_LEDC_TIMER,
        .duty = BACKLIGHT_DUTY_MAX,
        .hpoint = 0,
        .flags.output_invert = 0,
    };

    ESP_RETURN_ON_ERROR(
        ledc_channel_config(&channel_config),
        TAG,
        "Failed to configure LEDC channel"
    );

    s_brightness = 100;

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
    if (brightness > DISPLAY_BRIGHTNESS_MAX) {
        brightness = DISPLAY_BRIGHTNESS_MAX;
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
        brightness,
        duty
    );

    return ESP_OK;
}

uint8_t display_backlight_get_brightness(void)
{
    return s_brightness;
}
