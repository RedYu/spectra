/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#include "axp313a_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Complete power-management snapshot.
 *
 * The snapshot combines dynamic AXP313A status with its current
 * protection and power-control configuration.
 */
typedef struct
{
    axp313a_status_t status;
    axp313a_configuration_t configuration;

} power_service_snapshot_t;

/**
 * @brief Initialize the power-management service.
 *
 * The service initializes the AXP313A driver using the shared board
 * I2C bus. The service does not take ownership of the bus.
 *
 * @param[in] bus Shared I2C master-bus handle.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if bus is NULL,
 * ESP_ERR_INVALID_STATE if already initialized, ESP_ERR_NO_MEM if
 * synchronization resources cannot be created, otherwise an ESP-IDF
 * error code.
 */
esp_err_t power_service_init(
    i2c_master_bus_handle_t bus
);

/**
 * @brief Deinitialize the power-management service.
 *
 * The AXP313A device is removed from the shared I2C bus. The shared
 * bus itself is not deleted.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not initialized, ESP_ERR_TIMEOUT if the service lock cannot be
 * acquired, otherwise an ESP-IDF error code.
 */
esp_err_t power_service_deinit(void);

/**
 * @brief Get the power-service initialization state.
 *
 * @param[out] initialized Set to true when the service is initialized.
 *
 * @return ESP_OK on success or ESP_ERR_INVALID_ARG if initialized is
 * NULL.
 */
esp_err_t power_service_get_initialized(
    bool *initialized
);

/**
 * @brief Read a consistent power-management snapshot.
 *
 * The function serializes all AXP313A operations so the status and
 * configuration cannot overlap another service operation.
 *
 * Configured voltage fields are regulator setpoints and are not
 * measurements of actual output voltages.
 *
 * @param[out] snapshot Destination snapshot.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if snapshot is NULL,
 * ESP_ERR_INVALID_STATE if the service is not initialized,
 * ESP_ERR_TIMEOUT if the service lock cannot be acquired, otherwise
 * an ESP-IDF error code.
 */
esp_err_t power_service_get_snapshot(
    power_service_snapshot_t *snapshot
);

/**
 * @brief Enable or disable the ALDO1 regulator.
 *
 * The configured regulator voltage is not changed.
 *
 * @warning ALDO1 supplies camera circuitry. The caller must ensure
 * that changing this output is safe for the connected hardware.
 *
 * @param[in] enabled True to enable ALDO1.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not initialized, ESP_ERR_TIMEOUT if the service lock cannot be
 * acquired, otherwise an ESP-IDF error code.
 */
esp_err_t power_service_set_aldo1_enabled(
    bool enabled
);

/**
 * @brief Enable or disable the DLDO1 regulator.
 *
 * The configured regulator voltage is not changed.
 *
 * @warning DLDO1 supplies camera circuitry. The caller must ensure
 * that changing this output is safe for the connected hardware.
 *
 * @param[in] enabled True to enable DLDO1.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not initialized, ESP_ERR_TIMEOUT if the service lock cannot be
 * acquired, otherwise an ESP-IDF error code.
 */
esp_err_t power_service_set_dldo1_enabled(
    bool enabled
);

#ifdef __cplusplus
}
#endif
