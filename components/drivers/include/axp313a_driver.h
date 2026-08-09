#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AXP313A_I2C_ADDRESS_PRIMARY    (0x36U)
#define AXP313A_I2C_ADDRESS_SECONDARY  (0x37U)

/**
 * @brief AXP313A power-management status.
 *
 * Voltage fields contain configured regulator setpoints decoded from
 * PMIC registers. They are not measurements of actual output voltage.
 * A zero voltage indicates an unavailable or unsupported register code.
 *
 * IRQ fields represent latched bits from the IRQ status register.
 * Reading the status does not clear these bits.
 */
typedef struct
{
    bool dcdc1_enabled;
    bool dcdc2_enabled;
    bool dcdc3_enabled;
    bool aldo1_enabled;
    bool dldo1_enabled;

    uint16_t dcdc1_configured_voltage_mv;
    uint16_t dcdc2_configured_voltage_mv;
    uint16_t dcdc3_configured_voltage_mv;
    uint16_t aldo1_configured_voltage_mv;
    uint16_t dldo1_configured_voltage_mv;

    bool dcdc1_forced_pwm;
    bool dcdc2_forced_pwm;
    bool dcdc3_forced_pwm;

    bool overtemperature_irq;
    bool dcdc2_undervoltage_irq;
    bool dcdc3_undervoltage_irq;

    uint8_t power_on_source;
    uint8_t irq_status;

} axp313a_status_t;

/**
 * @brief AXP313A protection, interrupt and power-control configuration.
 *
 * The raw register fields preserve the complete register values for
 * diagnostics. Reserved bits must not be modified by application code.
 */
typedef struct
{
    bool spread_spectrum_enabled;
    uint16_t spread_spectrum_frequency_khz;

    bool pwron_rising_irq_enabled;
    bool pwron_falling_irq_enabled;
    bool pwron_short_press_irq_enabled;
    bool pwron_long_press_irq_enabled;
    bool dcdc3_undervoltage_irq_enabled;
    bool dcdc2_undervoltage_irq_enabled;
    bool overtemperature_irq_enabled;

    bool startup_pwrok_monitoring_enabled;
    bool pwrok_low_restart_enabled;
    bool overtemperature_shutdown_enabled;
    uint16_t overtemperature_threshold_c;

    bool reverse_shutdown_sequence_enabled;
    bool shutdown_delay_enabled;
    bool long_press_shutdown_enabled;
    bool restart_after_long_press_shutdown_enabled;

    bool irq_wakeup_enabled;
    bool lower_pwrok_during_wakeup;
    bool preserve_voltage_settings_during_wakeup;
    bool sleep_wakeup_enabled;

    bool dldo1_overcurrent_shutdown_enabled;
    bool dcdc3_undervoltage_shutdown_enabled;
    bool dcdc2_undervoltage_shutdown_enabled;
    bool dcdc1_undervoltage_shutdown_enabled;
    bool dcdc_overvoltage_shutdown_enabled;

    uint8_t dcdc_control_raw;
    uint8_t power_control_raw;
    uint8_t shutdown_control_raw;
    uint8_t wakeup_control_raw;
    uint8_t output_monitor_raw;
    uint8_t irq_control_raw;

} axp313a_configuration_t;

/**
 * @brief Initialize the AXP313A driver on a shared I2C bus.
 *
 * The driver probes the supported addresses and registers the PMIC as
 * an I2C device. The shared bus remains owned by the board module.
 *
 * @param[in] bus Shared I2C master-bus handle.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if bus is NULL,
 * ESP_ERR_INVALID_STATE if already initialized, ESP_ERR_NOT_FOUND if
 * the PMIC is not detected, otherwise an ESP-IDF error code.
 */
esp_err_t axp313a_driver_init(
    i2c_master_bus_handle_t bus
);

/**
 * @brief Remove the AXP313A device from the shared I2C bus.
 *
 * The shared I2C bus itself is not deleted.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t axp313a_driver_deinit(void);

/**
 * @brief Read the current AXP313A runtime status.
 *
 * Reads regulator states, configured voltage setpoints, DCDC operating
 * modes, power-on source and latched interrupt status.
 *
 * This function does not clear interrupt status bits.
 *
 * @param[out] status Destination status structure.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if status is NULL,
 * ESP_ERR_INVALID_STATE if the driver is not initialized, otherwise
 * an ESP-IDF error code.
 */
esp_err_t axp313a_driver_get_status(
    axp313a_status_t *status
);

/**
 * @brief Read the AXP313A protection and power-control configuration.
 *
 * This function only reads PMIC registers and does not change its
 * configuration.
 *
 * @param[out] configuration Destination configuration structure.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if configuration is
 * NULL, ESP_ERR_INVALID_STATE if the driver is not initialized,
 * otherwise an ESP-IDF error code.
 */
esp_err_t axp313a_driver_get_configuration(
    axp313a_configuration_t *configuration
);

/**
 * @brief Read one AXP313A register.
 *
 * @param[in] register_address Register address.
 * @param[out] value Destination for the register value.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if value is NULL,
 * ESP_ERR_INVALID_STATE if the driver is not initialized, otherwise
 * an ESP-IDF error code.
 */
esp_err_t axp313a_driver_read_register(
    uint8_t register_address,
    uint8_t *value
);

/**
 * @brief Write one AXP313A register.
 *
 * @warning Incorrect register values can disable regulator outputs,
 * shut down the PMIC or restart the complete device.
 *
 * @param[in] register_address Register address.
 * @param[in] value Register value.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the driver is not
 * initialized, otherwise an ESP-IDF error code.
 */
esp_err_t axp313a_driver_write_register(
    uint8_t register_address,
    uint8_t value
);

/**
 * @brief Update selected bits in an AXP313A register.
 *
 * The function performs a read-modify-write sequence. It is not
 * currently atomic across multiple application tasks.
 *
 * @warning Incorrect register values can disable regulator outputs,
 * shut down the PMIC or restart the complete device.
 *
 * @param[in] register_address Register address.
 * @param[in] mask Bits to update.
 * @param[in] value New values for the selected bits.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the driver is not
 * initialized, otherwise an ESP-IDF error code.
 */
esp_err_t axp313a_driver_update_register(
    uint8_t register_address,
    uint8_t mask,
    uint8_t value
);

/**
 * @brief Enable or disable the ALDO1 regulator output.
 *
 * The configured voltage is not changed. Enabling the output applies
 * the voltage currently stored in the ALDO1 voltage register.
 *
 * @warning Disabling this output immediately removes power from every
 * circuit connected to ALDO1.
 *
 * @param[in] enabled True to enable ALDO1.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the driver is
 * not initialized, otherwise an ESP-IDF error code.
 */
esp_err_t axp313a_driver_set_aldo1_enabled(
    bool enabled
);

/**
 * @brief Enable or disable the DLDO1 regulator output.
 *
 * The configured voltage is not changed. Enabling the output applies
 * the voltage currently stored in the DLDO1 voltage register.
 *
 * @warning Disabling this output immediately removes power from every
 * circuit connected to DLDO1.
 *
 * @param[in] enabled True to enable DLDO1.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the driver is
 * not initialized, otherwise an ESP-IDF error code.
 */
esp_err_t axp313a_driver_set_dldo1_enabled(
    bool enabled
);

#ifdef __cplusplus
}
#endif
