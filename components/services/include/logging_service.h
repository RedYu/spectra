#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the logging service.
 *
 * Creates the logging queue, starts the file logging task and installs
 * a custom ESP-IDF vprintf callback. UART logging remains enabled.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already
 * initialized, ESP_ERR_NO_MEM if required resources cannot be created,
 * otherwise an ESP-IDF error code.
 */
esp_err_t logging_service_init(void);

/**
 * @brief Enable log recording to the SD card.
 *
 * The SD card must already be mounted. The log file is opened in append
 * mode, preserving existing contents.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not initialized, file logging is already enabled, or the SD card is
 * not mounted, otherwise an ESP-IDF error code.
 */
esp_err_t logging_service_enable_file(void);

/**
 * @brief Disable log recording to the SD card.
 *
 * Flushes pending log messages, flushes buffered file data and closes
 * the log file. UART logging remains enabled. This function must
 * complete successfully before the SD card is unmounted.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not initialized, otherwise an ESP-IDF error code.
 */
esp_err_t logging_service_disable_file(void);

/**
 * @brief Flush pending log messages and buffered file data.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not initialized or file logging is not enabled, otherwise an
 * ESP-IDF error code.
 */
esp_err_t logging_service_flush(void);

/**
 * @brief Get the current file-logging state.
 *
 * @param[out] enabled Set to true when the log file is open.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if enabled is NULL,
 * or ESP_ERR_INVALID_STATE if the service is not initialized.
 */
esp_err_t logging_service_get_file_enabled(
    bool *enabled
);

/**
 * @brief Get the number of log messages that could not be recorded.
 *
 * Messages may be dropped because the queue is full or because writing
 * to the SD card failed.
 *
 * @param[out] count Destination for the dropped-message count.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if count is NULL, or
 * ESP_ERR_INVALID_STATE if the service is not initialized.
 */
esp_err_t logging_service_get_dropped_count(
    uint32_t *count
);

/**
 * @brief Reset the dropped-message counter.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not initialized, otherwise an ESP-IDF error code.
 */
esp_err_t logging_service_reset_dropped_count(void);

#ifdef __cplusplus
}
#endif
