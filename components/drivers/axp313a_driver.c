/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "axp313a_driver.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"

#define AXP313A_I2C_FREQUENCY_HZ  (400000U)
#define AXP313A_I2C_TIMEOUT_MS     (100U)

#define AXP313A_REG_POWER_ON_SOURCE       (0x00U)
#define AXP313A_REG_OUTPUT_CONTROL        (0x10U)
#define AXP313A_REG_DCDC_CONTROL          (0x12U)
#define AXP313A_REG_DCDC1_VOLTAGE         (0x13U)
#define AXP313A_REG_DCDC2_VOLTAGE         (0x14U)
#define AXP313A_REG_DCDC3_VOLTAGE         (0x15U)
#define AXP313A_REG_ALDO1_VOLTAGE         (0x16U)
#define AXP313A_REG_DLDO1_VOLTAGE         (0x17U)
#define AXP313A_REG_POWER_CONTROL         (0x1AU)
#define AXP313A_REG_SHUTDOWN_CONTROL      (0x1BU)
#define AXP313A_REG_WAKEUP_CONTROL        (0x1CU)
#define AXP313A_REG_OUTPUT_MONITOR        (0x1DU)
#define AXP313A_REG_IRQ_CONTROL           (0x20U)
#define AXP313A_REG_IRQ_STATUS            (0x21U)

#define AXP313A_OUTPUT_ALDO1_MASK   (1U << 3U)
#define AXP313A_OUTPUT_DLDO1_MASK   (1U << 4U)

static const char *TAG =
    "axp313a_driver";

/*
 * TODO:
 * Protect read-modify-write operations with a driver mutex before
 * allowing register changes from multiple tasks.
 */

static i2c_master_dev_handle_t s_device = NULL;

static esp_err_t axp313a_driver_find_address(
    i2c_master_bus_handle_t bus,
    uint8_t *address
)
{
    if ((bus == NULL) ||
        (address == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *address = 0U;

    const uint8_t addresses[] = {
        AXP313A_I2C_ADDRESS_PRIMARY,
        AXP313A_I2C_ADDRESS_SECONDARY,
    };

    const size_t address_count =
        sizeof(addresses) /
        sizeof(addresses[0]);

    for (size_t i = 0U;
         i < address_count;
         ++i) {

        const esp_err_t result =
            i2c_master_probe(
                bus,
                addresses[i],
                AXP313A_I2C_TIMEOUT_MS
            );

        if (result == ESP_OK) {
            *address = addresses[i];
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t axp313a_driver_init(
    i2c_master_bus_handle_t bus
)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_device != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t address = 0U;

    esp_err_t result =
        axp313a_driver_find_address(
            bus,
            &address
        );

    if (result != ESP_OK) {
        return result;
    }

    const i2c_device_config_t config = {
        .dev_addr_length =
            I2C_ADDR_BIT_LEN_7,

        .device_address =
            address,

        .scl_speed_hz =
            AXP313A_I2C_FREQUENCY_HZ,
    };

    result =
        i2c_master_bus_add_device(
            bus,
            &config,
            &s_device
        );

    if (result != ESP_OK) {
        s_device = NULL;
        return result;
    }

    uint8_t output_control = 0U;

    result =
        axp313a_driver_read_register(
            AXP313A_REG_OUTPUT_CONTROL,
            &output_control
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to read AXP313A status: %s",
            esp_err_to_name(result)
        );

        const esp_err_t remove_result =
            i2c_master_bus_rm_device(
                s_device
            );

        if (remove_result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Failed to remove incomplete AXP313A device: %s",
                esp_err_to_name(remove_result)
            );

            /*
             * Keep the handle because the device is still registered on
             * the shared I2C bus. A later deinitialization can retry.
             */
            return remove_result;
        }

        s_device = NULL;

        return result;
    }

    ESP_LOGI(
        TAG,
        "AXP313A detected at address 0x%02X, outputs=0x%02X",
        address,
        output_control
    );

    return ESP_OK;
}

esp_err_t axp313a_driver_read_register(
    uint8_t register_address,
    uint8_t *value
)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *value = 0U;

    if (s_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return i2c_master_transmit_receive(
        s_device,
        &register_address,
        sizeof(register_address),
        value,
        sizeof(*value),
        AXP313A_I2C_TIMEOUT_MS
    );
}

esp_err_t axp313a_driver_write_register(
    uint8_t register_address,
    uint8_t value
)
{
    if (s_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t data[] = {
        register_address,
        value,
    };

    return i2c_master_transmit(
        s_device,
        data,
        sizeof(data),
        AXP313A_I2C_TIMEOUT_MS
    );
}

esp_err_t axp313a_driver_update_register(
    uint8_t register_address,
    uint8_t mask,
    uint8_t value
)
{
    uint8_t current_value = 0U;

    esp_err_t result =
        axp313a_driver_read_register(
            register_address,
            &current_value
        );

    if (result != ESP_OK) {
        return result;
    }

    const uint8_t updated_value =
        (current_value & (uint8_t)~mask) |
        (value & mask);

    if (updated_value == current_value) {
        return ESP_OK;
    }

    return axp313a_driver_write_register(
        register_address,
        updated_value
    );
}

static uint16_t axp313a_driver_decode_dcdc1_voltage(
    uint8_t value
)
{
    const uint8_t code =
        value & 0x7FU;

    if (code <= 70U) {
        return (uint16_t)(
            500U +
            ((uint16_t)code * 10U)
        );
    }

    if (code <= 87U) {
        return (uint16_t)(
            1220U +
            ((uint16_t)(code - 71U) * 20U)
        );
    }

    if (code <= 106U) {
        return (uint16_t)(
            1600U +
            ((uint16_t)(code - 88U) * 100U)
        );
    }

    return 0U;
}

static uint16_t axp313a_driver_decode_dcdc2_voltage(
    uint8_t value
)
{
    const uint8_t code =
        value & 0x7FU;

    if (code <= 70U) {
        return (uint16_t)(
            500U +
            ((uint16_t)code * 10U)
        );
    }

    if (code <= 87U) {
        return (uint16_t)(
            1220U +
            ((uint16_t)(code - 71U) * 20U)
        );
    }

    return 0U;
}

static uint16_t axp313a_driver_decode_dcdc3_voltage(
    uint8_t value
)
{
    const uint8_t code =
        value & 0x7FU;

    if (code <= 32U) {
        return (uint16_t)(
            800U +
            ((uint16_t)code * 10U)
        );
    }

    if (code <= 68U) {
        return (uint16_t)(
            1140U +
            ((uint16_t)(code - 33U) * 20U)
        );
    }

    return 0U;
}

static uint16_t axp313a_driver_decode_ldo_voltage(
    uint8_t value
)
{
    const uint8_t code =
        value & 0x1FU;

    if (code > 30U) {
        return 0U;
    }

    return (uint16_t)(
        500U +
        ((uint16_t)code * 100U)
    );
}

esp_err_t axp313a_driver_get_status(
    axp313a_status_t *status
)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(
        status,
        0,
        sizeof(*status)
    );

    if (s_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t output_control = 0U;
    uint8_t dcdc_control = 0U;
    uint8_t dcdc1_voltage = 0U;
    uint8_t dcdc2_voltage = 0U;
    uint8_t dcdc3_voltage = 0U;
    uint8_t aldo1_voltage = 0U;
    uint8_t dldo1_voltage = 0U;

    esp_err_t result =
        axp313a_driver_read_register(
            AXP313A_REG_OUTPUT_CONTROL,
            &output_control
        );

    if (result != ESP_OK) {
        return result;
    }

    result = axp313a_driver_read_register(
        AXP313A_REG_DCDC_CONTROL,
        &dcdc_control
    );

    if (result != ESP_OK) {
        return result;
    }

    result = axp313a_driver_read_register(
        AXP313A_REG_DCDC1_VOLTAGE,
        &dcdc1_voltage
    );

    if (result != ESP_OK) {
        return result;
    }

    result = axp313a_driver_read_register(
        AXP313A_REG_DCDC2_VOLTAGE,
        &dcdc2_voltage
    );

    if (result != ESP_OK) {
        return result;
    }

    result = axp313a_driver_read_register(
        AXP313A_REG_DCDC3_VOLTAGE,
        &dcdc3_voltage
    );

    if (result != ESP_OK) {
        return result;
    }

    result = axp313a_driver_read_register(
        AXP313A_REG_ALDO1_VOLTAGE,
        &aldo1_voltage
    );

    if (result != ESP_OK) {
        return result;
    }

    result = axp313a_driver_read_register(
        AXP313A_REG_DLDO1_VOLTAGE,
        &dldo1_voltage
    );

    if (result != ESP_OK) {
        return result;
    }

    result = axp313a_driver_read_register(
        AXP313A_REG_POWER_ON_SOURCE,
        &status->power_on_source
    );

    if (result != ESP_OK) {
        return result;
    }

    result = axp313a_driver_read_register(
        AXP313A_REG_IRQ_STATUS,
        &status->irq_status
    );

    if (result != ESP_OK) {
        return result;
    }

    status->dcdc1_enabled =
        (output_control & (1U << 0U)) != 0U;

    status->dcdc2_enabled =
        (output_control & (1U << 1U)) != 0U;

    status->dcdc3_enabled =
        (output_control & (1U << 2U)) != 0U;

    status->aldo1_enabled =
        (output_control & AXP313A_OUTPUT_ALDO1_MASK) != 0U;

    status->dldo1_enabled =
        (output_control & AXP313A_OUTPUT_DLDO1_MASK) != 0U;

    status->dcdc1_configured_voltage_mv =
        axp313a_driver_decode_dcdc1_voltage(
            dcdc1_voltage
        );

    status->dcdc2_configured_voltage_mv =
        axp313a_driver_decode_dcdc2_voltage(
            dcdc2_voltage
        );

    status->dcdc3_configured_voltage_mv =
        axp313a_driver_decode_dcdc3_voltage(
            dcdc3_voltage
        );

    status->aldo1_configured_voltage_mv =
        axp313a_driver_decode_ldo_voltage(
            aldo1_voltage
        );

    status->dldo1_configured_voltage_mv =
        axp313a_driver_decode_ldo_voltage(
            dldo1_voltage
        );

    status->dcdc1_forced_pwm =
        (dcdc_control & (1U << 0U)) != 0U;

    status->dcdc2_forced_pwm =
        (dcdc_control & (1U << 1U)) != 0U;

    status->dcdc3_forced_pwm =
        (dcdc_control & (1U << 2U)) != 0U;

    status->overtemperature_irq =
        (status->irq_status & (1U << 0U)) != 0U;

    status->dcdc2_undervoltage_irq =
        (status->irq_status & (1U << 2U)) != 0U;

    status->dcdc3_undervoltage_irq =
        (status->irq_status & (1U << 3U)) != 0U;

    return ESP_OK;
}

esp_err_t axp313a_driver_deinit(void)
{
    if (s_device == NULL) {
        return ESP_OK;
    }

    const esp_err_t result =
        i2c_master_bus_rm_device(
            s_device
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to remove AXP313A from I2C bus: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    s_device = NULL;

    return ESP_OK;
}

esp_err_t axp313a_driver_set_aldo1_enabled(
    bool enabled
)
{
    return axp313a_driver_update_register(
        AXP313A_REG_OUTPUT_CONTROL,
        AXP313A_OUTPUT_ALDO1_MASK,
        enabled
            ? AXP313A_OUTPUT_ALDO1_MASK
            : 0U
    );
}

esp_err_t axp313a_driver_set_dldo1_enabled(
    bool enabled
)
{
    return axp313a_driver_update_register(
        AXP313A_REG_OUTPUT_CONTROL,
        AXP313A_OUTPUT_DLDO1_MASK,
        enabled
            ? AXP313A_OUTPUT_DLDO1_MASK
            : 0U
    );
}

esp_err_t axp313a_driver_get_configuration(
    axp313a_configuration_t *configuration
)
{
    if (configuration == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(
        configuration,
        0,
        sizeof(*configuration)
    );

    if (s_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t dcdc_control = 0U;
    uint8_t power_control = 0U;
    uint8_t shutdown_control = 0U;
    uint8_t wakeup_control = 0U;
    uint8_t output_monitor = 0U;
    uint8_t irq_control = 0U;

    esp_err_t result =
        axp313a_driver_read_register(
            AXP313A_REG_DCDC_CONTROL,
            &dcdc_control
        );

    if (result != ESP_OK) {
        return result;
    }

    result = axp313a_driver_read_register(
        AXP313A_REG_POWER_CONTROL,
        &power_control
    );

    if (result != ESP_OK) {
        return result;
    }

    result = axp313a_driver_read_register(
        AXP313A_REG_SHUTDOWN_CONTROL,
        &shutdown_control
    );

    if (result != ESP_OK) {
        return result;
    }

    result = axp313a_driver_read_register(
        AXP313A_REG_WAKEUP_CONTROL,
        &wakeup_control
    );

    if (result != ESP_OK) {
        return result;
    }

    result = axp313a_driver_read_register(
        AXP313A_REG_OUTPUT_MONITOR,
        &output_monitor
    );

    if (result != ESP_OK) {
        return result;
    }

    result = axp313a_driver_read_register(
        AXP313A_REG_IRQ_CONTROL,
        &irq_control
    );

    if (result != ESP_OK) {
        return result;
    }

    configuration->dcdc_control_raw =
        dcdc_control;

    configuration->power_control_raw =
        power_control;

    configuration->shutdown_control_raw =
        shutdown_control;

    configuration->wakeup_control_raw =
        wakeup_control;

    configuration->output_monitor_raw =
        output_monitor;

    configuration->irq_control_raw =
        irq_control;

    configuration->spread_spectrum_enabled =
        (dcdc_control & (1U << 7U)) != 0U;

    configuration->spread_spectrum_frequency_khz =
        (dcdc_control & (1U << 6U)) != 0U
            ? 100U
            : 50U;

    configuration->pwron_rising_irq_enabled =
        (irq_control & (1U << 7U)) != 0U;

    configuration->pwron_falling_irq_enabled =
        (irq_control & (1U << 6U)) != 0U;

    configuration->pwron_short_press_irq_enabled =
        (irq_control & (1U << 5U)) != 0U;

    configuration->pwron_long_press_irq_enabled =
        (irq_control & (1U << 4U)) != 0U;

    configuration->dcdc3_undervoltage_irq_enabled =
        (irq_control & (1U << 3U)) != 0U;

    configuration->dcdc2_undervoltage_irq_enabled =
        (irq_control & (1U << 2U)) != 0U;

    configuration->overtemperature_irq_enabled =
        (irq_control & (1U << 0U)) != 0U;

    configuration->startup_pwrok_monitoring_enabled =
        (power_control & (1U << 5U)) != 0U;

    configuration->pwrok_low_restart_enabled =
        (power_control & (1U << 4U)) != 0U;

    configuration->overtemperature_shutdown_enabled =
        (power_control & (1U << 1U)) != 0U;

    configuration->overtemperature_threshold_c =
        (power_control & (1U << 0U)) != 0U
            ? 145U
            : 125U;

    configuration->reverse_shutdown_sequence_enabled =
        (shutdown_control & (1U << 3U)) != 0U;

    configuration->shutdown_delay_enabled =
        (shutdown_control & (1U << 2U)) != 0U;

    configuration->long_press_shutdown_enabled =
        (shutdown_control & (1U << 1U)) != 0U;

    configuration->
        restart_after_long_press_shutdown_enabled =
            (shutdown_control & (1U << 0U)) != 0U;

    configuration->irq_wakeup_enabled =
        (wakeup_control & (1U << 4U)) != 0U;

    configuration->lower_pwrok_during_wakeup =
        (wakeup_control & (1U << 3U)) != 0U;

    configuration->
        preserve_voltage_settings_during_wakeup =
            (wakeup_control & (1U << 2U)) != 0U;

    configuration->sleep_wakeup_enabled =
        (wakeup_control & (1U << 0U)) != 0U;

    configuration->dldo1_overcurrent_shutdown_enabled =
        (output_monitor & (1U << 4U)) != 0U;

    configuration->
        dcdc3_undervoltage_shutdown_enabled =
            (output_monitor & (1U << 3U)) != 0U;

    configuration->
        dcdc2_undervoltage_shutdown_enabled =
            (output_monitor & (1U << 2U)) != 0U;

    configuration->
        dcdc1_undervoltage_shutdown_enabled =
            (output_monitor & (1U << 1U)) != 0U;

    configuration->dcdc_overvoltage_shutdown_enabled =
        (output_monitor & (1U << 0U)) != 0U;

    return ESP_OK;
}
