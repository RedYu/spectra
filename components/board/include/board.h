/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "driver/spi_master.h"

#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the board and its shared resources.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t board_init(void);

/**
 * @brief Initialize the shared SPI bus.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t board_spi_init(void);

/**
 * @brief Check whether the shared SPI bus is initialized.
 */
bool board_spi_is_initialized(void);

/**
 * @brief Get the SPI host used by the board.
 *
 * @note Call only after successful SPI initialization.
 */
spi_host_device_t board_spi_get_host(void);

/**
 * @brief Acquire exclusive access to the shared SPI bus.
 *
 * @param[in] timeout Maximum time to wait, in FreeRTOS ticks.
 *
 * @return True when the lock was acquired; otherwise false.
 */
bool board_spi_lock(
    TickType_t timeout
);

/**
 * @brief Release the shared SPI bus lock.
 */
void board_spi_unlock(void);

/**
 * @brief Release the shared SPI bus lock from an ISR.
 *
 * @param[out] higher_priority_task_woken Receives pdTRUE when a
 * context switch should be requested.
 */
void board_spi_unlock_from_isr(
    BaseType_t *higher_priority_task_woken
);

/**
 * @brief Initialize the shared I2C bus.
 *
 * The bus is shared by devices such as the GT911 touch controller and
 * the AXP313A power-management controller.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t board_i2c_init(void);

/**
 * @brief Check whether the shared I2C bus is initialized.
 */
bool board_i2c_is_initialized(void);

/**
 * @brief Get the shared I2C master-bus handle.
 *
 * The returned handle is owned by the board module. Device drivers
 * must not delete the shared bus.
 *
 * @return Shared I2C bus handle, or NULL if it is not initialized.
 */
i2c_master_bus_handle_t board_i2c_get_handle(void);

/**
 * @brief Initialize the shared GPIO ISR service.
 *
 * The GPIO ISR service is global to ESP-IDF and is owned by the board
 * module for the complete application lifetime. Repeated calls are
 * accepted.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t board_gpio_isr_service_init(void);

/**
 * @brief Check whether the shared GPIO ISR service is available.
 */
bool board_gpio_isr_service_is_initialized(void);

#ifdef __cplusplus
}
#endif
