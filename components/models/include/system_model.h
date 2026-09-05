/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYSTEM_DEVICE_ID_MAX_LENGTH        (32U)
#define SYSTEM_FW_VERSION_MAX_LENGTH       (32U)
#define SYSTEM_SERIAL_MAX_LENGTH           (32U)
#define SYSTEM_HW_VERSION_MAX_LENGTH       (16U)
#define SYSTEM_DEVICE_NAME_MAX_LENGTH      (32U)

#define SYSTEM_CHIP_MODEL_MAX_LENGTH       (16U)

typedef struct
{
    char device_id[
        SYSTEM_DEVICE_ID_MAX_LENGTH
    ];

    char firmware_version[
        SYSTEM_FW_VERSION_MAX_LENGTH
    ];

    char hardware_version[
        SYSTEM_HW_VERSION_MAX_LENGTH
    ];

    char serial_number[
        SYSTEM_SERIAL_MAX_LENGTH
    ];

    char device_name[
        SYSTEM_DEVICE_NAME_MAX_LENGTH
    ];

    /*
     * Static processor and memory information.
     */
    char chip_model[
        SYSTEM_CHIP_MODEL_MAX_LENGTH
    ];

    uint8_t chip_cores;
    uint16_t chip_revision;

    uint32_t cpu_frequency_mhz;

    uint32_t flash_size;
    uint32_t psram_size;

    esp_reset_reason_t reset_reason;

    /*
     * Runtime system information.
     */
    uint32_t uptime_sec;

    uint32_t free_heap;
    uint32_t minimum_free_heap;

    uint32_t psram_free;
    uint32_t psram_minimum_free;

    uint8_t cpu_usage;

    float chip_temperature_celsius;
    bool chip_temperature_valid;

    /*
     * Application service states.
     */
    bool storage_ready;
    bool sd_card_mounted;
    bool ota_available;
    bool internet_available;

} system_model_t;

/**
 * @brief Initialize the system model and synchronization resources.
 *
 * @return ESP_OK on success, ESP_ERR_NO_MEM if synchronization
 * resources cannot be created, otherwise an ESP-IDF error code.
 */
esp_err_t system_model_init(void);

/**
 * @brief Replace the complete system model.
 *
 * This function is intended for initial model configuration before
 * concurrent services start. During normal operation, use specialized
 * field update functions to avoid lost updates.
 *
 * @param[in] model New system model.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if model is NULL,
 * ESP_ERR_INVALID_STATE if the model is not initialized, or
 * ESP_ERR_TIMEOUT if the model lock cannot be acquired.
 */
esp_err_t system_model_set(
    const system_model_t *model
);

/**
 * @brief Copy a consistent snapshot of the current system model.
 *
 * @param[out] out_model Destination model structure.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out_model is NULL,
 * ESP_ERR_INVALID_STATE if the model is not initialized, or
 * ESP_ERR_TIMEOUT if the model lock cannot be acquired.
 */
esp_err_t system_model_get_snapshot(
    system_model_t *out_model
);

/**
 * @brief Set static processor and memory information.
 *
 * This function normally needs to be called only once during startup.
 *
 * @param[in] chip_model Processor model name.
 * @param[in] chip_cores Number of processor cores.
 * @param[in] chip_revision Hardware revision.
 * @param[in] cpu_frequency_mhz Configured CPU frequency in MHz.
 * @param[in] flash_size Flash capacity in bytes.
 * @param[in] psram_size PSRAM capacity in bytes.
 * @param[in] reset_reason Reason for the most recent system reset.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if chip_model is
 * NULL, ESP_ERR_INVALID_STATE if the model is not initialized, or
 * ESP_ERR_TIMEOUT if the model lock cannot be acquired.
 */
esp_err_t system_model_set_hardware_info(
    const char *chip_model,
    uint8_t chip_cores,
    uint16_t chip_revision,
    uint32_t cpu_frequency_mhz,
    uint32_t flash_size,
    uint32_t psram_size,
    esp_reset_reason_t reset_reason
);

/**
 * @brief Set the internal storage readiness state.
 *
 * @param[in] ready True when internal storage is ready.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the model is
 * not initialized, or ESP_ERR_TIMEOUT if the lock cannot be acquired.
 */
esp_err_t system_model_set_storage_ready(
    bool ready
);

/**
 * @brief Set the SD card mount state.
 *
 * @param[in] mounted True when the SD card is mounted.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the model is
 * not initialized, or ESP_ERR_TIMEOUT if the lock cannot be acquired.
 */
esp_err_t system_model_set_sd_card_mounted(
    bool mounted
);

/**
 * @brief Set the OTA update availability state.
 *
 * @param[in] available True when an OTA update is available.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the model is
 * not initialized, or ESP_ERR_TIMEOUT if the lock cannot be acquired.
 */
esp_err_t system_model_set_ota_available(
    bool available
);

/**
 * @brief Set the confirmed Internet availability state.
 *
 * Internet availability means that communication with the configured
 * Spectra backend has completed successfully. A local Wi-Fi connection
 * alone does not make this state available.
 *
 * @param[in] available True when the Spectra backend is reachable.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the model is
 * not initialized, or ESP_ERR_TIMEOUT if the lock cannot be acquired.
 */
esp_err_t system_model_set_internet_available(
    bool available
);

/**
 * @brief Atomically update runtime system metrics.
 *
 * @param[in] uptime_sec System uptime in seconds.
 * @param[in] free_heap Current internal heap size in bytes.
 * @param[in] minimum_free_heap Minimum observed internal heap size.
 * @param[in] psram_free Current free PSRAM size in bytes.
 * @param[in] psram_minimum_free Minimum observed free PSRAM size.
 * @param[in] cpu_usage CPU usage percentage from 0 to 100.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the model is
 * not initialized, or ESP_ERR_TIMEOUT if the lock cannot be acquired.
 */
esp_err_t system_model_set_runtime(
    uint32_t uptime_sec,
    uint32_t free_heap,
    uint32_t minimum_free_heap,
    uint32_t psram_free,
    uint32_t psram_minimum_free,
    uint8_t cpu_usage
);

/**
 * @brief Update the internal chip temperature.
 *
 * @param[in] temperature_celsius Measured temperature in degrees
 * Celsius.
 * @param[in] valid True when the supplied measurement is valid.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the model is
 * not initialized, or ESP_ERR_TIMEOUT if the lock cannot be acquired.
 */
esp_err_t system_model_set_chip_temperature(
    float temperature_celsius,
    bool valid
);

#ifdef __cplusplus
}
#endif
