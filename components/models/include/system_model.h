#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYSTEM_DEVICE_ID_MAX_LENGTH        (32U)
#define SYSTEM_FW_VERSION_MAX_LENGTH       (32U)
#define SYSTEM_SERIAL_MAX_LENGTH           (32U)
#define SYSTEM_HW_VERSION_MAX_LENGTH       (16U)
#define SYSTEM_DEVICE_NAME_MAX_LENGTH      (32U)

typedef struct
{
    char device_id[SYSTEM_DEVICE_ID_MAX_LENGTH];
    char firmware_version[SYSTEM_FW_VERSION_MAX_LENGTH];
    char hardware_version[SYSTEM_HW_VERSION_MAX_LENGTH];
    char serial_number[SYSTEM_SERIAL_MAX_LENGTH];
    char device_name[SYSTEM_DEVICE_NAME_MAX_LENGTH];

    uint32_t uptime_sec;
    uint32_t free_heap;
    uint32_t minimum_free_heap;

    uint8_t cpu_usage;

    bool storage_ready;
    bool sd_card_mounted;
    bool ota_available;

} system_model_t;

/**
 * @brief Initialize the system model and its synchronization resources.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t system_model_init(void);

/**
 * @brief Replace the complete system model.
 *
 * This function is intended for initial model configuration before
 * concurrent services start. During normal operation, use the
 * specialized field update functions to avoid lost updates.
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
 * The returned structure is independent of the internal model and may
 * be safely used after this function returns.
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
 * @brief Set the internal storage readiness state.
 *
 * @param[in] ready true when internal storage is ready.
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
 * @param[in] mounted true when the SD card is mounted.
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
 * @param[in] available true when an OTA update is available.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the model is
 * not initialized, or ESP_ERR_TIMEOUT if the lock cannot be acquired.
 */
esp_err_t system_model_set_ota_available(
    bool available
);

/**
 * @brief Atomically update the runtime system metrics.
 *
 * @param[in] uptime_sec System uptime in seconds.
 * @param[in] free_heap Current free heap size in bytes.
 * @param[in] minimum_free_heap Minimum observed free heap size in bytes.
 * @param[in] cpu_usage CPU usage percentage from 0 to 100.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the model is
 * not initialized, or ESP_ERR_TIMEOUT if the lock cannot be acquired.
 */
esp_err_t system_model_set_runtime(
    uint32_t uptime_sec,
    uint32_t free_heap,
    uint32_t minimum_free_heap,
    uint8_t cpu_usage
);

#ifdef __cplusplus
}
#endif
