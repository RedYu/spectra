#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>

#include "esp_err.h"

#include "storage_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    STORAGE_SD_STATE_UNAVAILABLE = 0,
    STORAGE_SD_STATE_MOUNTED = 1,
    STORAGE_SD_STATE_ERROR = 2

} storage_sd_state_t;

/**
 * @brief Open a file on the SD card.
 *
 * Relative paths are automatically prefixed with the SD mount point.
 * For example, "config/settings.json" and "/config/settings.json"
 * both become "/sdcard/config/settings.json".
 *
 * A path already beginning with "/sdcard/" is used without adding
 * the mount point again.
 *
 * The returned stream must be closed with
 * storage_sd_service_close(). A stream must not be accessed
 * concurrently from multiple tasks without external synchronization.
 *
 * @param[in] path Path inside the SD card filesystem.
 * @param[in] mode Standard fopen mode.
 * @param[out] out_file Receives the opened stream.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid
 * arguments, ESP_ERR_INVALID_STATE if the card is not mounted,
 * ESP_ERR_TIMEOUT if the SPI bus cannot be locked,
 * ESP_ERR_NOT_FOUND if the file does not exist, otherwise an
 * ESP-IDF error code.
 */
esp_err_t storage_sd_service_open(
    const char *path,
    const char *mode,
    FILE **out_file
);

/**
 * @brief Read data from an opened file.
 *
 * Reading fewer bytes than requested at the end of the file is not
 * considered an error.
 *
 * @param[in] file Opened file stream.
 * @param[out] buffer Destination buffer.
 * @param[in] size Maximum number of bytes to read.
 * @param[out] out_bytes_read Receives the actual number of bytes read.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid
 * arguments, ESP_ERR_INVALID_STATE if the card is not mounted,
 * ESP_ERR_TIMEOUT if the SPI bus cannot be locked, otherwise an
 * ESP-IDF error code.
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
 * @param[in] file Opened file stream.
 * @param[in] buffer Source buffer containing the data to write.
 * @param[in] size Number of bytes to write.
 * @param[out] out_bytes_written Receives the actual number of bytes
 * written. May be NULL if the caller does not need it.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid
 * arguments, ESP_ERR_INVALID_STATE if the card is not mounted,
 * ESP_ERR_TIMEOUT if the SPI bus cannot be locked,
 * ESP_ERR_NO_MEM if the card has insufficient free space,
 * otherwise an ESP-IDF error code.
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
 * @brief List entries in an SD-card directory.
 *
 * Relative paths are automatically prefixed with the SD-card mount
 * point. The paths "/", "/logs" and "/sdcard/logs" are accepted.
 *
 * The special directory entries "." and ".." are not returned.
 * At most capacity entries are written. If more entries are available,
 * out_has_more is set to true. The offset parameter can be used to
 * request the next page.
 *
 * Unlike SPIFFS, the SD-card filesystem provides real directories.
 *
 * @param[in] path Directory path inside the SD-card filesystem.
 * @param[in] offset Number of matching entries to skip.
 * @param[out] entries Destination array for directory entries.
 * @param[in] capacity Number of elements available in entries.
 * @param[out] out_count Number of entries written.
 * @param[out] out_has_more True when additional entries are available.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if an argument or path
 * is invalid, ESP_ERR_INVALID_STATE if the service is not started or
 * the SD card is not mounted, ESP_ERR_NOT_FOUND if the directory does
 * not exist, ESP_ERR_TIMEOUT if a required lock cannot be acquired,
 * otherwise an ESP-IDF error code.
 */
esp_err_t storage_sd_service_list(
    const char *path,
    size_t offset,
    storage_file_entry_t *entries,
    size_t capacity,
    size_t *out_count,
    bool *out_has_more
);

/**
 * @brief Start the SD storage service.
 *
 * This function initializes the SD card driver and performs an
 * initial mount attempt. Absence of a card is not considered fatal.
 *
 * @return ESP_OK when the service starts, otherwise an ESP-IDF
 * error code.
 */
esp_err_t storage_sd_service_start(void);

/**
 * @brief Mount the SD card and update the service state.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t storage_sd_service_mount(void);

/**
 * @brief Disable service-owned SD users and unmount the SD card.
 *
 * All file streams opened by callers must be closed before this
 * function is called.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service
 * is not started, ESP_ERR_TIMEOUT if a required lock cannot be
 * acquired, otherwise an ESP-IDF error code.
 */
esp_err_t storage_sd_service_unmount(void);

/**
 * @brief Get the current SD storage service state.
 *
 * @param[out] state Destination for the current state.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if state is NULL,
 * ESP_ERR_INVALID_STATE if the service is not started,
 * ESP_ERR_TIMEOUT if the state lock cannot be acquired, otherwise
 * an ESP-IDF error code.
 */
esp_err_t storage_sd_service_get_state(
    storage_sd_state_t *state
);

#ifdef __cplusplus
}
#endif
