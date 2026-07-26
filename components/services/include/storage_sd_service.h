#pragma once

#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    STORAGE_STATE_UNAVAILABLE,
    STORAGE_STATE_MOUNTED,
    STORAGE_STATE_READY,
    STORAGE_STATE_BUSY,
    STORAGE_STATE_ERROR

} storage_sd_state_t;

/**
 * @brief Open a file on the SD card.
 *
 * Relative paths are automatically prefixed with the SD mount point.
 *
 * Examples:
 *     "config/settings.json"
 *     "/config/settings.json"
 *
 * Both become:
 *     "/sdcard/config/settings.json"
 *
 * @param path Relative path inside the SD card.
 * @param mode Standard fopen mode.
 * @param out_file Receives the opened FILE pointer.
 *
 * @return
 *     - ESP_OK on success
 *     - ESP_ERR_INVALID_ARG for invalid arguments
 *     - ESP_ERR_INVALID_STATE if the SD card is not mounted
 *     - ESP_ERR_TIMEOUT if the SPI bus could not be locked
 *     - ESP_ERR_NOT_FOUND if the file does not exist
 *     - ESP_FAIL for other filesystem errors
 */
esp_err_t storage_sd_service_open(
    const char *path,
    const char *mode,
    FILE **out_file
);

/**
 * @brief Read data from an opened file.
 *
 * @param file Opened file.
 * @param buffer Destination buffer.
 * @param size Maximum number of bytes to read.
 * @param out_bytes_read Receives the actual number of bytes read.
 */
esp_err_t storage_sd_service_read(
    FILE *file,
    void *buffer,
    size_t size,
    size_t *out_bytes_read
);

/**
 * @brief Write data to an opened file.
 *
 * The data is written starting from the current file position.
 *
 * @param file Opened file.
 * @param buffer Source buffer containing the data to write.
 * @param size Number of bytes to write.
 * @param out_bytes_written Receives the actual number of bytes written.
 *                          May be NULL if the caller does not need it.
 *
 * @return
 *     - ESP_OK on success
 *     - ESP_ERR_INVALID_ARG for invalid arguments
 *     - ESP_ERR_INVALID_STATE if the SD card is not mounted
 *     - ESP_ERR_TIMEOUT if the SPI bus could not be locked
 *     - ESP_ERR_NO_MEM if there is not enough space on the SD card
 *     - ESP_FAIL for other filesystem errors
 */
esp_err_t storage_sd_service_write(
    FILE *file,
    const void *buffer,
    size_t size,
    size_t *out_bytes_written
);

/**
 * @brief Change the current file position.
 */
esp_err_t storage_sd_service_seek(
    FILE *file,
    long offset,
    int origin
);

/**
 * @brief Flush buffered file data to the SD card.
 */
esp_err_t storage_sd_service_flush(
    FILE *file
);

/**
 * @brief Close an opened file.
 *
 * The FILE pointer is set to NULL after fclose().
 */
esp_err_t storage_sd_service_close(
    FILE **file
);

/**
 * @brief Remove a file from the SD card.
 */
esp_err_t storage_sd_service_remove(
    const char *path
);

/**
 * @brief Rename or move a file on the SD card.
 */
esp_err_t storage_sd_service_rename(
    const char *old_path,
    const char *new_path
);

/**
 * @brief Read file or directory information.
 */
esp_err_t storage_sd_service_stat(
    const char *path,
    struct stat *out_stat
);

/**
 * @brief Start storage SD service.
 */
esp_err_t storage_sd_service_start(void);

void storage_sd_service_set_state(
    storage_sd_state_t state
);

#ifdef __cplusplus
}
#endif
