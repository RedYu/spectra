#include "battery_adc_driver.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

#include "esp_log.h"

#include "board_config.h"

#define BATTERY_ADC_ATTENUATION        ADC_ATTEN_DB_12
#define BATTERY_ADC_BITWIDTH           ADC_BITWIDTH_DEFAULT
#define BATTERY_ADC_SAMPLE_COUNT       (16U)

_Static_assert(
    BATTERY_ADC_SAMPLE_COUNT > 0U,
    "Battery ADC sample count must be greater than zero"
);

_Static_assert(
    BATTERY_DIVIDER_LOW_OHM > 0U,
    "Battery divider low resistance must be greater than zero"
);

static const char *TAG =
    "battery_adc_driver";

/*
 * TODO: Protect initialization, deinitialization and measurements with
 * a mutex if the driver becomes accessible from multiple tasks.
 */

static adc_oneshot_unit_handle_t s_adc_unit = NULL;
static adc_cali_handle_t s_calibration = NULL;

static adc_unit_t s_adc_unit_id;
static adc_channel_t s_adc_channel;

static bool s_initialized = false;

static esp_err_t battery_adc_driver_create_calibration(void)
{
    const adc_cali_curve_fitting_config_t config = {
        .unit_id = s_adc_unit_id,
        .chan = s_adc_channel,
        .atten = BATTERY_ADC_ATTENUATION,
        .bitwidth = BATTERY_ADC_BITWIDTH,
    };

    const esp_err_t result =
        adc_cali_create_scheme_curve_fitting(
            &config,
            &s_calibration
        );

    if (result != ESP_OK) {
        s_calibration = NULL;

        ESP_LOGE(
            TAG,
            "Failed to initialize ADC calibration: %s",
            esp_err_to_name(result)
        );
    }

    return result;
}

static uint32_t battery_adc_driver_apply_divider(
    uint32_t adc_voltage_mv
)
{
    const uint64_t denominator =
        (uint64_t)BATTERY_DIVIDER_LOW_OHM;

    const uint64_t numerator =
        (uint64_t)adc_voltage_mv *
        (
            (uint64_t)BATTERY_DIVIDER_HIGH_OHM +
            denominator
        );

    return (uint32_t)(
        (
            numerator +
            (denominator / 2U)
        ) /
        denominator
    );
}

esp_err_t battery_adc_driver_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t result =
        adc_oneshot_io_to_channel(
            BATTERY_PIN_VOLTAGE,
            &s_adc_unit_id,
            &s_adc_channel
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "GPIO %d cannot be used for ADC: %s",
            (int)BATTERY_PIN_VOLTAGE,
            esp_err_to_name(result)
        );

        return result;
    }

    if (s_adc_unit_id != ADC_UNIT_1) {
        ESP_LOGE(
            TAG,
            "Battery voltage must use ADC1"
        );

        return ESP_ERR_INVALID_ARG;
    }

    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = s_adc_unit_id,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    result =
        adc_oneshot_new_unit(
            &unit_config,
            &s_adc_unit
        );

    if (result != ESP_OK) {
        s_adc_unit = NULL;

        ESP_LOGE(
            TAG,
            "Failed to initialize ADC unit: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = BATTERY_ADC_ATTENUATION,
        .bitwidth = BATTERY_ADC_BITWIDTH,
    };

    result =
        adc_oneshot_config_channel(
            s_adc_unit,
            s_adc_channel,
            &channel_config
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to configure battery ADC channel: %s",
            esp_err_to_name(result)
        );

        (void)adc_oneshot_del_unit(
            s_adc_unit
        );

        s_adc_unit = NULL;

        return result;
    }

    result =
        battery_adc_driver_create_calibration();

    if (result != ESP_OK) {
        (void)adc_oneshot_del_unit(
            s_adc_unit
        );

        s_adc_unit = NULL;

        return result;
    }

    s_initialized = true;

    ESP_LOGI(
        TAG,
        "Battery ADC initialized: GPIO=%d, unit=%d, channel=%d",
        (int)BATTERY_PIN_VOLTAGE,
        (int)s_adc_unit_id,
        (int)s_adc_channel
    );

    return ESP_OK;
}

esp_err_t battery_adc_driver_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    esp_err_t first_error = ESP_OK;

    if (s_calibration != NULL) {
        const esp_err_t result =
            adc_cali_delete_scheme_curve_fitting(
                s_calibration
            );

        if (result != ESP_OK) {
            first_error = result;

            ESP_LOGW(
                TAG,
                "Failed to delete ADC calibration: %s",
                esp_err_to_name(result)
            );
        }

        s_calibration = NULL;
    }

    if (s_adc_unit != NULL) {
        const esp_err_t result =
            adc_oneshot_del_unit(
                s_adc_unit
            );

        if ((result != ESP_OK) &&
            (first_error == ESP_OK)) {

            first_error = result;
        }

        if (result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Failed to delete ADC unit: %s",
                esp_err_to_name(result)
            );
        }

        s_adc_unit = NULL;
    }

    s_initialized = false;

    return first_error;
}

esp_err_t battery_adc_driver_get_voltage_mv(
    uint16_t *voltage_mv
)
{
    if (voltage_mv == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *voltage_mv = 0U;

    if (!s_initialized ||
        (s_adc_unit == NULL) ||
        (s_calibration == NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    uint32_t raw_sum = 0U;

    /*
     * Discard the first sample so the ADC sampling capacitor can settle
     * after an extended period without measurements.
     */
    int discarded_raw = 0;

    const esp_err_t discarded_result =
        adc_oneshot_read(
            s_adc_unit,
            s_adc_channel,
            &discarded_raw
        );

    if (discarded_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to perform initial battery ADC read: %s",
            esp_err_to_name(discarded_result)
        );

        return discarded_result;
    }

    for (uint32_t index = 0U;
         index < BATTERY_ADC_SAMPLE_COUNT;
         ++index) {

        int raw_value = 0;

        const esp_err_t result =
            adc_oneshot_read(
                s_adc_unit,
                s_adc_channel,
                &raw_value
            );

        if (result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Failed to read battery ADC: %s",
                esp_err_to_name(result)
            );

            return result;
        }

        if (raw_value < 0) {
            return ESP_FAIL;
        }

        raw_sum +=
            (uint32_t)raw_value;
    }

    const int average_raw =
        (int)(
            raw_sum /
            BATTERY_ADC_SAMPLE_COUNT
        );

    int adc_voltage_mv = 0;

    const esp_err_t result =
        adc_cali_raw_to_voltage(
            s_calibration,
            average_raw,
            &adc_voltage_mv
        );

    if (result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to calibrate battery ADC value: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    if (adc_voltage_mv < 0) {
        return ESP_FAIL;
    }

    const uint32_t battery_voltage_mv =
        battery_adc_driver_apply_divider(
            (uint32_t)adc_voltage_mv
        );

    if (battery_voltage_mv > UINT16_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    *voltage_mv =
        (uint16_t)battery_voltage_mv;

    return ESP_OK;
}

bool battery_adc_driver_is_initialized(void)
{
    return s_initialized;
}
