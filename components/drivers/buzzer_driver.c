#include "buzzer_driver.h"

#include "driver/ledc.h"
#include "esp_log.h"

#include "board_config.h"

#define BUZZER_LEDC_TIMER              LEDC_TIMER_1
#define BUZZER_LEDC_CHANNEL            LEDC_CHANNEL_1
#define BUZZER_LEDC_SPEED_MODE         LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_DUTY_RESOLUTION    LEDC_TIMER_10_BIT

#define BUZZER_DEFAULT_FREQUENCY_HZ    (2000U)
#define BUZZER_DUTY_50_PERCENT         (512U)

_Static_assert(
    BUZZER_ACTIVE_LEVEL !=
    BUZZER_INACTIVE_LEVEL,
    "Buzzer active and inactive levels must differ"
);

_Static_assert(
    BUZZER_DUTY_50_PERCENT <
    (1U << 10U),
    "Buzzer duty exceeds LEDC resolution"
);

static const char *TAG =
    "buzzer_driver";

/*
 * TODO: Serialize driver operations if the buzzer driver becomes
 * accessible from multiple application tasks.
 */

static bool s_initialized = false;
static bool s_active = false;

esp_err_t buzzer_driver_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    const ledc_timer_config_t timer_config = {
        .speed_mode =
            BUZZER_LEDC_SPEED_MODE,

        .duty_resolution =
            BUZZER_LEDC_DUTY_RESOLUTION,

        .timer_num =
            BUZZER_LEDC_TIMER,

        .freq_hz =
            BUZZER_DEFAULT_FREQUENCY_HZ,

        .clk_cfg =
            LEDC_AUTO_CLK,

        .deconfigure =
            false,
    };

    esp_err_t result =
        ledc_timer_config(
            &timer_config
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to configure buzzer timer: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    /*
     * The external buzzer module is active low. PWM polarity does not
     * affect the sound of a passive buzzer, so output inversion is not
     * required. The channel is stopped at a high level when inactive.
     */
    const ledc_channel_config_t channel_config = {
        .gpio_num =
            BUZZER_PIN_SIGNAL,

        .speed_mode =
            BUZZER_LEDC_SPEED_MODE,

        .channel =
            BUZZER_LEDC_CHANNEL,

        .intr_type =
            LEDC_INTR_DISABLE,

        .timer_sel =
            BUZZER_LEDC_TIMER,

        .duty =
            0U,

        .hpoint =
            0U,

        .sleep_mode =
            LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,

        .flags = {
            .output_invert = 0U,
        },
    };

    result =
        ledc_channel_config(
            &channel_config
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to configure buzzer channel: %s",
            esp_err_to_name(result)
        );

        const ledc_timer_config_t deconfigure = {
            .speed_mode =
                BUZZER_LEDC_SPEED_MODE,

            .timer_num =
                BUZZER_LEDC_TIMER,

            .deconfigure =
                true,
        };

        (void)ledc_timer_config(
            &deconfigure
        );

        return result;
    }

    /*
     * Keep the low-level-trigger module disabled.
     */
    result =
        ledc_stop(
            BUZZER_LEDC_SPEED_MODE,
            BUZZER_LEDC_CHANNEL,
            BUZZER_INACTIVE_LEVEL
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to stop buzzer output: %s",
            esp_err_to_name(result)
        );

        const ledc_timer_config_t deconfigure = {
            .speed_mode =
                BUZZER_LEDC_SPEED_MODE,

            .timer_num =
                BUZZER_LEDC_TIMER,

            .deconfigure =
                true,
        };

        (void)ledc_timer_config(
            &deconfigure
        );

        return result;
    }

    s_initialized = true;
    s_active = false;

    ESP_LOGI(
        TAG,
        "Passive buzzer initialized on GPIO %d",
        (int)BUZZER_PIN_SIGNAL
    );

    return ESP_OK;
}

esp_err_t buzzer_driver_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    esp_err_t result =
        buzzer_driver_stop();

    if (result != ESP_OK) {
        return result;
    }

    const ledc_timer_config_t timer_config = {
        .speed_mode =
            BUZZER_LEDC_SPEED_MODE,

        .timer_num =
            BUZZER_LEDC_TIMER,

        .deconfigure =
            true,
    };

    result =
        ledc_timer_config(
            &timer_config
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to deconfigure buzzer timer: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    s_initialized = false;
    s_active = false;

    ESP_LOGI(
        TAG,
        "Passive buzzer deinitialized"
    );

    return ESP_OK;
}

esp_err_t buzzer_driver_start_tone(
    uint32_t frequency_hz
)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if ((frequency_hz < BUZZER_FREQUENCY_MIN_HZ) ||
        (frequency_hz > BUZZER_FREQUENCY_MAX_HZ)) {

        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        ledc_set_freq(
            BUZZER_LEDC_SPEED_MODE,
            BUZZER_LEDC_TIMER,
            frequency_hz
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to set buzzer frequency to %lu Hz: %s",
            (unsigned long)frequency_hz,
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        ledc_set_duty(
            BUZZER_LEDC_SPEED_MODE,
            BUZZER_LEDC_CHANNEL,
            BUZZER_DUTY_50_PERCENT
        );

    if (result != ESP_OK) {
        return result;
    }

    result =
        ledc_update_duty(
            BUZZER_LEDC_SPEED_MODE,
            BUZZER_LEDC_CHANNEL
        );

    if (result != ESP_OK) {
        return result;
    }

    s_active = true;

    return ESP_OK;
}

esp_err_t buzzer_driver_stop(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result =
        ledc_stop(
            BUZZER_LEDC_SPEED_MODE,
            BUZZER_LEDC_CHANNEL,
            BUZZER_INACTIVE_LEVEL
        );

    if (result == ESP_OK) {
        s_active = false;
    }

    return result;
}

bool buzzer_driver_is_initialized(void)
{
    return s_initialized;
}

bool buzzer_driver_is_active(void)
{
    return s_initialized &&
           s_active;
}
