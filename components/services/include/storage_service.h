#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and mount the internal storage filesystem.
 *
 * @return ESP_OK on success, ESP_ERR_NO_MEM if synchronization
 * resources cannot be created, otherwise an ESP-IDF error code.
 */
esp_err_t storage_service_init(void);

/**
 * @brief Unmount the internal storage filesystem.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the storage
 * service is not initialized, ESP_ERR_TIMEOUT if the service lock
 * cannot be acquired, otherwise an ESP-IDF error code.
 */
esp_err_t storage_service_deinit(void);

/**
 * @brief Get the internal storage mount state.
 *
 * @param[out] mounted Set to true when the filesystem is mounted;
 * otherwise set to false.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if mounted is NULL,
 * ESP_ERR_INVALID_STATE if the storage service is not initialized,
 * ESP_ERR_TIMEOUT if the service lock cannot be acquired, otherwise
 * an ESP-IDF error code.
 */
esp_err_t storage_service_get_mounted(
    bool *mounted
);

/**
 * @brief Read an entire file into a newly allocated memory buffer.
 *
 * The returned buffer is null-terminated. The caller owns the buffer
 * and must release it with free().
 *
 * @param[in] path Absolute VFS path located under the internal storage
 * mount point, for example "/storage/device_config.json".
 * @param[out] out_data Receives the allocated file buffer.
 * @param[out] out_size Receives the file size in bytes, excluding
 * the terminating null character.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if an argument is
 * invalid, ESP_ERR_INVALID_STATE if storage is not mounted,
 * ESP_ERR_TIMEOUT if the service lock cannot be acquired,
 * ESP_ERR_NOT_FOUND if the file does not exist, ESP_ERR_NO_MEM if
 * memory allocation fails, otherwise an ESP-IDF error code.
 */
esp_err_t storage_service_read_file(
    const char *path,
    char **out_data,
    size_t *out_size
);

#ifdef __cplusplus
}
#endif
