/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

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
 * the file buffer cannot be allocated, ESP_ERR_TIMEOUT if the service
 * lock cannot be acquired, otherwise an ESP-IDF error code.
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
 * ESP_ERR_NO_MEM if the temporary listing buffer cannot be allocated,
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

/**
 * @brief Process a block of streamed internal-storage file data.
 *
 * The data pointer is valid only during the callback invocation.
 * The callback must not retain or free it.
 *
 * The callback is invoked synchronously while the storage-service
 * lock is held. It must not call another storage_service_* function
 * and should complete as quickly as possible. A slow callback may
 * cause concurrent storage operations to return ESP_ERR_TIMEOUT.
 *
 * Returning an error aborts the stream and propagates the callback
 * error to storage_service_stream_file().
 *
 * @param[in] data File-data block.
 * @param[in] size Number of valid bytes in data.
 * @param[in,out] context Caller-provided callback context.
 *
 * @return ESP_OK to continue streaming, otherwise an error code to
 * abort the operation.
 */
typedef esp_err_t (*storage_service_stream_callback_t)(
    const void *data,
    size_t size,
    void *context
);

/**
 * @brief Stream an internal-storage file in bounded-size blocks.
 *
 * The path must be an absolute path located below the internal storage
 * mount point, for example "/storage/www/index.html".
 *
 * The function opens the file, reads it in bounded-size blocks and
 * invokes callback for each non-empty block. The file is always closed
 * and the service lock is always released before this function returns.
 *
 * The callback is invoked synchronously from the calling task while
 * the internal-storage service lock is acquired. It must not call
 * another storage_service function.
 *
 * @param[in] path Absolute path below the internal storage mount point.
 * @param[in] callback Callback that processes each file-data block.
 * @param[in,out] context Caller-provided callback context. May be NULL
 * if the callback does not require context.
 *
 * @return ESP_OK when the complete file was streamed successfully,
 * ESP_ERR_INVALID_ARG if path or callback is invalid,
 * ESP_ERR_INVALID_STATE if storage is not mounted,
 * ESP_ERR_NOT_FOUND if the file does not exist,
 * ESP_ERR_NO_MEM if the stream buffer cannot be allocated,
 * ESP_ERR_TIMEOUT if the service lock cannot be acquired,
 * the callback error if the callback aborts the stream,
 * otherwise an ESP-IDF error code.
 */
esp_err_t storage_service_stream_file(
    const char *path,
    storage_service_stream_callback_t callback,
    void *context
);

#ifdef __cplusplus
}
#endif
