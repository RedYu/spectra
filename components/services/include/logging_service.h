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

/**
 * @brief Apply per-tag ESP-IDF log levels.
 *
 * Arguments contain comma-separated ESP-IDF log tags. Whitespace
 * around tags is ignored. Tags removed from the configuration are
 * restored to the default ESP-IDF log level.
 *
 * When a tag appears in multiple lists, the following priority is
 * used: disabled, debug, info, warning.
 *
 * @param[in] warning_tags Tags assigned ESP_LOG_WARN.
 * @param[in] info_tags Tags assigned ESP_LOG_INFO.
 * @param[in] debug_tags Tags assigned ESP_LOG_DEBUG.
 * @param[in] disabled_tags Tags assigned ESP_LOG_NONE.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if an argument or
 * tag is invalid, ESP_ERR_INVALID_SIZE if a list or tag is too long,
 * ESP_ERR_INVALID_STATE if the service is not initialized, otherwise
 * an ESP-IDF error code.
 */
esp_err_t logging_service_set_tag_levels(
    const char *warning_tags,
    const char *info_tags,
    const char *debug_tags,
    const char *disabled_tags
);

#ifdef __cplusplus
}
#endif
