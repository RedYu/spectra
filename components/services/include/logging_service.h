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
 * a custom ESP-IDF vprintf callback.
 *
 * UART logging remains enabled after initialization.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_STATE if already initialized.
 *      - ESP_ERR_NO_MEM if required FreeRTOS objects cannot be created.
 */
esp_err_t logging_service_init(void);

/**
 * @brief Enable log recording to the SD card.
 *
 * The SD card must already be mounted. The log file is opened in append
 * mode, so existing log contents are preserved.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_STATE if the service is not initialized,
 *        file logging is already enabled or the SD card is not mounted.
 *      - ESP_FAIL if the log file cannot be opened.
 */
esp_err_t logging_service_enable_file(void);

/**
 * @brief Disable log recording to the SD card.
 *
 * Flushes all buffered file data and closes the log file. UART logging
 * remains enabled.
 *
 * This function must be called before unmounting the SD card.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_STATE if the service is not initialized.
 */
esp_err_t logging_service_disable_file(void);

/**
 * @brief Flush buffered log data to the SD card.
 *
 * @return
 *      - ESP_OK on success.
 *      - ESP_ERR_INVALID_STATE if file logging is not enabled.
 *      - ESP_FAIL if the file flush operation fails.
 */
esp_err_t logging_service_flush(void);

/**
 * @brief Check whether SD-card file logging is enabled.
 *
 * @return true if the log file is currently open.
 */
bool logging_service_is_file_enabled(void);

/**
 * @brief Get the number of messages dropped because the queue was full.
 *
 * @return Number of dropped log messages.
 */
uint32_t logging_service_get_dropped_count(void);

/**
 * @brief Reset the dropped-message counter.
 */
void logging_service_reset_dropped_count(void);

#ifdef __cplusplus
}
#endif
