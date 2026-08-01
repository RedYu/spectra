#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#include "storage_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The sum of offset and capacity must not exceed the internal
 * storage listing limit.
 */
#define STORAGE_LIST_MAX_RESULT_COUNT      (128U)

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
 * The path must be an absolute path located below the internal storage
 * mount point, for example "/storage/www/index.html".
 *
 * The returned buffer is null-terminated. The caller owns the buffer
 * and must release it with free().
 *
 * @param[in] path Absolute path below the internal storage mount point.
 * @param[out] out_data Receives the allocated file buffer.
 * @param[out] out_size Receives the file size in bytes, excluding the
 * terminating null character.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if an argument or path
 * is invalid, ESP_ERR_INVALID_STATE if storage is not mounted,
 * ESP_ERR_NOT_FOUND if the file does not exist, ESP_ERR_NO_MEM if
 * memory allocation fails, otherwise an ESP-IDF error code.
 */
esp_err_t storage_service_read_file(
    const char *path,
    char **out_data,
    size_t *out_size
);

/**
 * @brief List entries located below an internal storage path.
 *
 * SPIFFS does not provide real directories. Directory-like paths are
 * represented by slash-separated file-name prefixes. The service
 * converts these prefixes into logical directory entries.
 *
 * The path must be an absolute path located at or below the internal
 * storage mount point. Examples:
 *
 *     "/storage"
 *     "/storage/www"
 *
 * At most capacity entries are written. If more entries exist,
 * out_has_more is set to true. The offset parameter can be used to
 * request the next page.
 *
 * @param[in] path Absolute directory-like path to list.
 * @param[in] offset Number of matching entries to skip.
 * @param[out] entries Destination array for file entries.
 * @param[in] capacity Number of elements available in entries.
 * @param[out] out_count Number of entries written.
 * @param[out] out_has_more True when additional entries are available.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if an argument or path
 * is invalid, ESP_ERR_INVALID_STATE if storage is not mounted,
 * ESP_ERR_NOT_FOUND if the requested path does not exist,
 * ESP_ERR_TIMEOUT if the service lock cannot be acquired, otherwise
 * an ESP-IDF error code.
 */
esp_err_t storage_service_list(
    const char *path,
    size_t offset,
    storage_file_entry_t *entries,
    size_t capacity,
    size_t *out_count,
    bool *out_has_more
);

#ifdef __cplusplus
}
#endif
