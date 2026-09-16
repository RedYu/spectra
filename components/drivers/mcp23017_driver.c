/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "mcp23017_driver.h"

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_log.h"

#define MCP23017_I2C_FREQUENCY_HZ  (400000U)
#define MCP23017_I2C_TIMEOUT_MS     (100U)
#define MCP23017_LOCK_TIMEOUT_MS    (100U)

#define MCP23017_REG_IODIRA    (0x00U)
#define MCP23017_REG_IPOLA     (0x02U)
#define MCP23017_REG_GPINTENA  (0x04U)
#define MCP23017_REG_IOCONA    (0x0AU)
#define MCP23017_REG_GPPUA     (0x0CU)
#define MCP23017_REG_GPIOA     (0x12U)
#define MCP23017_REG_OLATA     (0x14U)

#define MCP23017_IOCON_BANK   (1U << 7U)
#define MCP23017_IOCON_SEQOP  (1U << 5U)

static const char *TAG =
    "mcp23017_driver";

static i2c_master_dev_handle_t s_device = NULL;
static SemaphoreHandle_t s_mutex = NULL;
static uint16_t s_output_latch = 0U;

static esp_err_t mcp23017_driver_lock(void)
{
    if ((s_device == NULL) ||
        (s_mutex == NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    return xSemaphoreTake(
        s_mutex,
        pdMS_TO_TICKS(MCP23017_LOCK_TIMEOUT_MS)
    ) == pdTRUE
        ? ESP_OK
        : ESP_ERR_TIMEOUT;
}

static void mcp23017_driver_unlock(void)
{
    (void)xSemaphoreGive(s_mutex);
}

static esp_err_t mcp23017_driver_read_register_unlocked(
    uint8_t register_address,
    uint8_t *value
)
{
    return i2c_master_transmit_receive(
        s_device,
        &register_address,
        sizeof(register_address),
        value,
        sizeof(*value),
        MCP23017_I2C_TIMEOUT_MS
    );
}

static esp_err_t mcp23017_driver_write_register_unlocked(
    uint8_t register_address,
    uint8_t value
)
{
    const uint8_t data[] = {
        register_address,
        value,
    };

    return i2c_master_transmit(
        s_device,
        data,
        sizeof(data),
        MCP23017_I2C_TIMEOUT_MS
    );
}

static esp_err_t mcp23017_driver_read_register_pair_unlocked(
    uint8_t register_address,
    uint16_t *value
)
{
    uint8_t data[2] = {0U};

    const esp_err_t result =
        i2c_master_transmit_receive(
            s_device,
            &register_address,
            sizeof(register_address),
            data,
            sizeof(data),
            MCP23017_I2C_TIMEOUT_MS
        );

    if (result == ESP_OK) {
        *value =
            (uint16_t)data[0] |
            ((uint16_t)data[1] << 8U);
    }

    return result;
}

static esp_err_t mcp23017_driver_write_register_pair_unlocked(
    uint8_t register_address,
    uint16_t value
)
{
    const uint8_t data[] = {
        register_address,
        (uint8_t)(value & 0xFFU),
        (uint8_t)(value >> 8U),
    };

    return i2c_master_transmit(
        s_device,
        data,
        sizeof(data),
        MCP23017_I2C_TIMEOUT_MS
    );
}

static esp_err_t mcp23017_driver_update_register_pair(
    uint8_t register_address,
    uint16_t mask,
    uint16_t value
)
{
    esp_err_t result =
        mcp23017_driver_lock();

    if (result != ESP_OK) {
        return result;
    }

    uint16_t current = 0U;

    result =
        mcp23017_driver_read_register_pair_unlocked(
            register_address,
            &current
        );

    const uint16_t updated =
        (current & (uint16_t)~mask) |
        (value & mask);

    if ((result == ESP_OK) &&
        (updated != current)) {

        result =
            mcp23017_driver_write_register_pair_unlocked(
                register_address,
                updated
            );
    }

    mcp23017_driver_unlock();

    return result;
}

esp_err_t mcp23017_driver_init(
    i2c_master_bus_handle_t bus,
    const mcp23017_driver_config_t *config
)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((s_device != NULL) ||
        (s_mutex != NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t address =
        ((config == NULL) ||
         (config->address == 0U))
            ? MCP23017_I2C_ADDRESS_DEFAULT
            : config->address;

    if ((address < MCP23017_I2C_ADDRESS_MINIMUM) ||
        (address > MCP23017_I2C_ADDRESS_MAXIMUM)) {

        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t clock_speed_hz =
        ((config == NULL) ||
         (config->clock_speed_hz == 0U))
            ? MCP23017_I2C_FREQUENCY_HZ
            : config->clock_speed_hz;

    esp_err_t result =
        i2c_master_probe(
            bus,
            address,
            MCP23017_I2C_TIMEOUT_MS
        );

    if (result != ESP_OK) {
        return result == ESP_ERR_TIMEOUT
            ? ESP_ERR_NOT_FOUND
            : result;
    }

    s_mutex = xSemaphoreCreateMutex();

    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const i2c_device_config_t device_config = {
        .dev_addr_length =
            I2C_ADDR_BIT_LEN_7,

        .device_address =
            address,

        .scl_speed_hz =
            clock_speed_hz,
    };

    result =
        i2c_master_bus_add_device(
            bus,
            &device_config,
            &s_device
        );

    if (result == ESP_OK) {
        uint8_t iocon = 0U;

        result =
            mcp23017_driver_read_register_unlocked(
                MCP23017_REG_IOCONA,
                &iocon
            );

        if ((result == ESP_OK) &&
            ((iocon & MCP23017_IOCON_BANK) != 0U)) {

            result = ESP_ERR_INVALID_STATE;
        }

        if (result == ESP_OK) {
            iocon &=
                (uint8_t)~(
                    MCP23017_IOCON_BANK |
                    MCP23017_IOCON_SEQOP
                );

            result =
                mcp23017_driver_write_register_unlocked(
                    MCP23017_REG_IOCONA,
                    iocon
                );
        }

        if (result == ESP_OK) {
            result =
                mcp23017_driver_read_register_pair_unlocked(
                    MCP23017_REG_OLATA,
                    &s_output_latch
                );
        }
    }

    if (result != ESP_OK) {
        if (s_device != NULL) {
            (void)i2c_master_bus_rm_device(s_device);
            s_device = NULL;
        }

        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        s_output_latch = 0U;

        return result;
    }

    ESP_LOGI(
        TAG,
        "MCP23017 detected at address 0x%02X",
        address
    );

    return ESP_OK;
}

esp_err_t mcp23017_driver_deinit(void)
{
    esp_err_t result =
        mcp23017_driver_lock();

    if (result != ESP_OK) {
        return result;
    }

    result =
        i2c_master_bus_rm_device(
            s_device
        );

    mcp23017_driver_unlock();

    if (result != ESP_OK) {
        return result;
    }

    s_device = NULL;
    s_output_latch = 0U;

    vSemaphoreDelete(s_mutex);
    s_mutex = NULL;

    return ESP_OK;
}

esp_err_t mcp23017_driver_configure_gpio(
    const mcp23017_gpio_config_t *config
)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        mcp23017_driver_lock();

    if (result != ESP_OK) {
        return result;
    }

    result =
        mcp23017_driver_write_register_pair_unlocked(
            MCP23017_REG_GPINTENA,
            0U
        );

    if (result == ESP_OK) {
        result =
            mcp23017_driver_write_register_pair_unlocked(
                MCP23017_REG_OLATA,
                config->output_latch
            );
    }

    if (result == ESP_OK) {
        result =
            mcp23017_driver_write_register_pair_unlocked(
                MCP23017_REG_IPOLA,
                config->input_polarity
            );
    }

    if (result == ESP_OK) {
        result =
            mcp23017_driver_write_register_pair_unlocked(
                MCP23017_REG_GPPUA,
                config->pull_up
            );
    }

    if (result == ESP_OK) {
        result =
            mcp23017_driver_write_register_pair_unlocked(
                MCP23017_REG_IODIRA,
                config->direction
            );
    }

    if (result == ESP_OK) {
        s_output_latch =
            config->output_latch;
    }

    mcp23017_driver_unlock();

    return result;
}

esp_err_t mcp23017_driver_set_direction(
    uint16_t mask,
    uint16_t inputs
)
{
    return mcp23017_driver_update_register_pair(
        MCP23017_REG_IODIRA,
        mask,
        inputs
    );
}

esp_err_t mcp23017_driver_set_pull_up(
    uint16_t mask,
    uint16_t enabled
)
{
    return mcp23017_driver_update_register_pair(
        MCP23017_REG_GPPUA,
        mask,
        enabled
    );
}

esp_err_t mcp23017_driver_set_input_polarity(
    uint16_t mask,
    uint16_t inverted
)
{
    return mcp23017_driver_update_register_pair(
        MCP23017_REG_IPOLA,
        mask,
        inverted
    );
}

esp_err_t mcp23017_driver_read_gpio(
    uint16_t *levels
)
{
    if (levels == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *levels = 0U;

    esp_err_t result =
        mcp23017_driver_lock();

    if (result == ESP_OK) {
        result =
            mcp23017_driver_read_register_pair_unlocked(
                MCP23017_REG_GPIOA,
                levels
            );

        mcp23017_driver_unlock();
    }

    return result;
}

esp_err_t mcp23017_driver_write_gpio(
    uint16_t levels
)
{
    esp_err_t result =
        mcp23017_driver_lock();

    if (result == ESP_OK) {
        result =
            mcp23017_driver_write_register_pair_unlocked(
                MCP23017_REG_OLATA,
                levels
            );

        if (result == ESP_OK) {
            s_output_latch = levels;
        }

        mcp23017_driver_unlock();
    }

    return result;
}

esp_err_t mcp23017_driver_update_gpio(
    uint16_t mask,
    uint16_t levels
)
{
    esp_err_t result =
        mcp23017_driver_lock();

    if (result != ESP_OK) {
        return result;
    }

    const uint16_t updated =
        (s_output_latch & (uint16_t)~mask) |
        (levels & mask);

    if (updated != s_output_latch) {
        result =
            mcp23017_driver_write_register_pair_unlocked(
                MCP23017_REG_OLATA,
                updated
            );

        if (result == ESP_OK) {
            s_output_latch = updated;
        }
    }

    mcp23017_driver_unlock();

    return result;
}

esp_err_t mcp23017_driver_read_pin(
    uint8_t pin,
    bool *level
)
{
    if ((pin >= MCP23017_PIN_COUNT) ||
        (level == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *level = false;

    uint16_t levels = 0U;

    const esp_err_t result =
        mcp23017_driver_read_gpio(
            &levels
        );

    if (result == ESP_OK) {
        *level =
            (levels & (uint16_t)(1U << pin)) != 0U;
    }

    return result;
}

esp_err_t mcp23017_driver_write_pin(
    uint8_t pin,
    bool level
)
{
    if (pin >= MCP23017_PIN_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint16_t mask =
        (uint16_t)(1U << pin);

    return mcp23017_driver_update_gpio(
        mask,
        level
            ? mask
            : 0U
    );
}
