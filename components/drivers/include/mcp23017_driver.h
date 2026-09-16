/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MCP23017_I2C_ADDRESS_DEFAULT  (0x20U)
#define MCP23017_I2C_ADDRESS_MINIMUM  (0x20U)
#define MCP23017_I2C_ADDRESS_MAXIMUM  (0x27U)

#define MCP23017_PIN_COUNT  (16U)

/**
 * @brief MCP23017 I2C device configuration.
 *
 * Set address to zero to use MCP23017_I2C_ADDRESS_DEFAULT. Set
 * clock_speed_hz to zero to use the driver's 400 kHz default.
 */
typedef struct
{
    uint8_t address;
    uint32_t clock_speed_hz;

} mcp23017_driver_config_t;

/**
 * @brief Complete GPIO configuration for both MCP23017 ports.
 *
 * Bits 0..7 represent GPA0..GPA7 and bits 8..15 represent GPB0..GPB7.
 * A set direction bit configures the corresponding pin as an input.
 */
typedef struct
{
    uint16_t direction;
    uint16_t input_polarity;
    uint16_t pull_up;
    uint16_t output_latch;

} mcp23017_gpio_config_t;

/**
 * @brief Initialize MCP23017 on the shared board I2C bus.
 *
 * The driver probes the configured address, registers the device and places
 * the register map into sequential BANK=0 mode. It does not change GPIO
 * direction, pull-up or output configuration.
 *
 * @param[in] bus Shared I2C master-bus handle.
 * @param[in] config Optional device configuration. NULL selects defaults.
 *
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND when the device does not
 * acknowledge, ESP_ERR_INVALID_ARG for an invalid configuration,
 * ESP_ERR_INVALID_STATE if already initialized, otherwise an ESP-IDF error.
 */
esp_err_t mcp23017_driver_init(
    i2c_master_bus_handle_t bus,
    const mcp23017_driver_config_t *config
);

/**
 * @brief Remove MCP23017 from the shared I2C bus.
 *
 * The board-owned I2C bus itself remains active.
 */
esp_err_t mcp23017_driver_deinit(void);

/**
 * @brief Apply complete GPIO configuration to both ports.
 *
 * The output latch is written before pins are changed to outputs to avoid
 * unintended output pulses. Hardware interrupt registers remain disabled.
 */
esp_err_t mcp23017_driver_configure_gpio(
    const mcp23017_gpio_config_t *config
);

/**
 * @brief Configure selected pins as inputs or outputs.
 *
 * @param[in] mask Pins to change.
 * @param[in] inputs New direction bits; one selects input, zero output.
 */
esp_err_t mcp23017_driver_set_direction(
    uint16_t mask,
    uint16_t inputs
);

/**
 * @brief Enable or disable internal pull-ups for selected input pins.
 */
esp_err_t mcp23017_driver_set_pull_up(
    uint16_t mask,
    uint16_t enabled
);

/**
 * @brief Enable or disable input-polarity inversion for selected pins.
 */
esp_err_t mcp23017_driver_set_input_polarity(
    uint16_t mask,
    uint16_t inverted
);

/**
 * @brief Read all GPA and GPB input levels in one I2C transaction.
 */
esp_err_t mcp23017_driver_read_gpio(
    uint16_t *levels
);

/**
 * @brief Replace the complete output latch for both ports.
 */
esp_err_t mcp23017_driver_write_gpio(
    uint16_t levels
);

/**
 * @brief Atomically update selected output-latch bits under the driver lock.
 */
esp_err_t mcp23017_driver_update_gpio(
    uint16_t mask,
    uint16_t levels
);

/**
 * @brief Read one MCP23017 pin.
 *
 * @param[in] pin Pin index: 0..7 for GPA and 8..15 for GPB.
 * @param[out] level Current logical input level.
 */
esp_err_t mcp23017_driver_read_pin(
    uint8_t pin,
    bool *level
);

/**
 * @brief Update one output-latch bit.
 *
 * The pin direction is not changed by this function.
 */
esp_err_t mcp23017_driver_write_pin(
    uint8_t pin,
    bool level
);

#ifdef __cplusplus
}
#endif
