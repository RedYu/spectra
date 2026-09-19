/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "storage_service.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "system_model.h"

#define STORAGE_LOCK_TIMEOUT_MS        (1000U)
#define STORAGE_STREAM_BUFFER_SIZE     (1024U)
#define STORAGE_PARTITION_COUNT        (2U)

#define STORAGE_NVS_NAMESPACE          "storage_ab"
#define STORAGE_NVS_SELECTED_KEY       "selected"

#define STORAGE_VALIDATION_BASE_PATH   "/storage-check"

#if CONFIG_SPECTRA_WEB_CONTENT_GZIP
#define STORAGE_VALIDATION_SUFFIX      ".gz"
#else
#define STORAGE_VALIDATION_SUFFIX      ""
#endif

static const char *TAG = "storage_service";

static const char STORAGE_BASE_PATH[] = "/storage";

static const char *const STORAGE_PARTITION_LABELS[
    STORAGE_PARTITION_COUNT
] = {
    "storage_0",
    "storage_1",
};

static const char *const STORAGE_REQUIRED_FILES[] = {
    "/www/index.html" STORAGE_VALIDATION_SUFFIX,
    "/www/spectra.css" STORAGE_VALIDATION_SUFFIX,
    "/www/spectra.js" STORAGE_VALIDATION_SUFFIX,
};

static SemaphoreHandle_t s_mutex = NULL;
static bool s_is_mounted = false;
static bool s_fallback_used = false;

static storage_service_partition_t s_active_partition =
    STORAGE_SERVICE_PARTITION_0;

static storage_service_partition_t s_selected_partition =
    STORAGE_SERVICE_PARTITION_0;

/*
 * TODO:
 * storage_service_stream_file() currently holds the storage mutex for
 * the entire streaming operation, including calls to the user-provided
 * callback. This prevents storage_service_deinit() from unmounting
 * SPIFFS while a streamed file is still open.
 *
 * The current design has two important restrictions:
 *
 * 1. The stream callback must not call another storage_service_*
 *    function because the service mutex is not recursive.
 *
 * 2. A slow callback, such as an HTTP response blocked by a slow
 *    client, may hold the storage mutex longer than
 *    STORAGE_LOCK_TIMEOUT_MS. Other storage operations will then
 *    return ESP_ERR_TIMEOUT.
 *
 * Replace the long-held mutex with active-operation tracking before
 * allowing concurrent file operations. Deinitialization should first
 * prevent new operations and then wait for the active-operation count
 * to reach zero before unmounting SPIFFS.
 */

static esp_err_t storage_service_lock(void)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_mutex,
            pdMS_TO_TICKS(
                STORAGE_LOCK_TIMEOUT_MS
            )
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void storage_service_unlock(void)
{
    (void)xSemaphoreGive(s_mutex);
}

static bool storage_service_partition_valid(
    storage_service_partition_t partition
)
{
    return
        ((uint32_t)partition <
         STORAGE_PARTITION_COUNT);
}

static storage_service_partition_t
storage_service_other_partition(
    storage_service_partition_t partition
)
{
    return
        partition ==
        STORAGE_SERVICE_PARTITION_0
            ? STORAGE_SERVICE_PARTITION_1
            : STORAGE_SERVICE_PARTITION_0;
}

static const char *storage_service_partition_label(
    storage_service_partition_t partition
)
{
    if (!storage_service_partition_valid(
            partition
        )) {

        return NULL;
    }

    return
        STORAGE_PARTITION_LABELS[
            (uint32_t)partition
        ];
}

static esp_err_t storage_service_load_selection(
    storage_service_partition_t *partition
)
{
    if (partition == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *partition =
        STORAGE_SERVICE_PARTITION_0;

    nvs_handle_t handle;

    esp_err_t result =
        nvs_open(
            STORAGE_NVS_NAMESPACE,
            NVS_READONLY,
            &handle
        );

    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }

    if (result != ESP_OK) {
        return result;
    }

    uint8_t value =
        (uint8_t)STORAGE_SERVICE_PARTITION_0;

    result =
        nvs_get_u8(
            handle,
            STORAGE_NVS_SELECTED_KEY,
            &value
        );

    nvs_close(handle);

    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }

    if (result != ESP_OK) {
        return result;
    }

    if (value >= STORAGE_PARTITION_COUNT) {
        ESP_LOGW(
            TAG,
            "Ignoring invalid stored partition: %u",
            (unsigned int)value
        );

        return ESP_OK;
    }

    *partition =
        (storage_service_partition_t)value;

    return ESP_OK;
}

static esp_err_t storage_service_save_selection(
    storage_service_partition_t partition
)
{
    if (!storage_service_partition_valid(
            partition
        )) {

        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;

    esp_err_t result =
        nvs_open(
            STORAGE_NVS_NAMESPACE,
            NVS_READWRITE,
            &handle
        );

    if (result != ESP_OK) {
        return result;
    }

    result =
        nvs_set_u8(
            handle,
            STORAGE_NVS_SELECTED_KEY,
            (uint8_t)partition
        );

    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }

    nvs_close(handle);

    return result;
}

static esp_err_t storage_service_validate_mounted_image(
    const char *base_path
)
{
    if (base_path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t index = 0U;
         index <
         (sizeof(STORAGE_REQUIRED_FILES) /
          sizeof(STORAGE_REQUIRED_FILES[0]));
         index++) {

        char path[PATH_MAX];

        const int length =
            snprintf(
                path,
                sizeof(path),
                "%s%s",
                base_path,
                STORAGE_REQUIRED_FILES[index]
            );

        if ((length < 0) ||
            ((size_t)length >= sizeof(path))) {

            return ESP_ERR_INVALID_SIZE;
        }

        struct stat information;

        if (stat(
                path,
                &information
            ) != 0) {

            ESP_LOGW(
                TAG,
                "Storage image is missing '%s'",
                path
            );

            return
                errno == ENOENT
                    ? ESP_ERR_NOT_FOUND
                    : ESP_FAIL;
        }

        if ((information.st_size <= 0) ||
            !S_ISREG(information.st_mode)) {

            ESP_LOGW(
                TAG,
                "Storage image contains invalid '%s'",
                path
            );

            return ESP_ERR_INVALID_SIZE;
        }
    }

    return ESP_OK;
}

static esp_err_t storage_service_mount_partition(
    storage_service_partition_t partition,
    const char *base_path,
    size_t maximum_open_files
)
{
    const char *label =
        storage_service_partition_label(
            partition
        );

    if ((label == NULL) ||
        (base_path == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    const esp_vfs_spiffs_conf_t config = {
        .base_path = base_path,
        .partition_label = label,
        .max_files = maximum_open_files,
        .format_if_mount_failed = false,
    };

    return esp_vfs_spiffs_register(
        &config
    );
}

static esp_err_t storage_service_validate_partition_locked(
    storage_service_partition_t partition
)
{
    if (!storage_service_partition_valid(
            partition
        )) {

        return ESP_ERR_INVALID_ARG;
    }

    if (s_is_mounted &&
        (partition == s_active_partition)) {

        return
            storage_service_validate_mounted_image(
                STORAGE_BASE_PATH
            );
    }

    const esp_err_t mount_result =
        storage_service_mount_partition(
            partition,
            STORAGE_VALIDATION_BASE_PATH,
            2U
        );

    if (mount_result != ESP_OK) {
        return mount_result;
    }

    const esp_err_t validation_result =
        storage_service_validate_mounted_image(
            STORAGE_VALIDATION_BASE_PATH
        );

    const esp_err_t unmount_result =
        esp_vfs_spiffs_unregister(
            storage_service_partition_label(
                partition
            )
        );

    if (validation_result != ESP_OK) {
        return validation_result;
    }

    return unmount_result;
}

esp_err_t storage_service_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();

        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    const esp_err_t lock_result =
        storage_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    if (s_is_mounted) {
        storage_service_unlock();
        return ESP_OK;
    }

    esp_err_t result =
        storage_service_load_selection(
            &s_selected_partition
        );

    if (result != ESP_OK) {
        storage_service_unlock();

        ESP_LOGE(
            TAG,
            "Failed to load storage selection: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    const storage_service_partition_t candidates[] = {
        s_selected_partition,
        storage_service_other_partition(
            s_selected_partition
        ),
    };

    s_fallback_used = false;

    esp_err_t first_error = ESP_FAIL;

    for (size_t index = 0U;
         index <
         (sizeof(candidates) /
          sizeof(candidates[0]));
         index++) {

        const storage_service_partition_t candidate =
            candidates[index];

        const char *label =
            storage_service_partition_label(
                candidate
            );

        result =
            storage_service_mount_partition(
                candidate,
                STORAGE_BASE_PATH,
                8U
            );

        if (result == ESP_OK) {
            result =
                storage_service_validate_mounted_image(
                    STORAGE_BASE_PATH
                );
        }

        if (result == ESP_OK) {
            s_active_partition = candidate;
            s_is_mounted = true;
            s_fallback_used = index > 0U;
            break;
        }

        if (index == 0U) {
            first_error = result;
        }

        ESP_LOGW(
            TAG,
            "Storage partition '%s' is not usable: %s",
            label,
            esp_err_to_name(result)
        );

        const esp_err_t unregister_result =
            esp_vfs_spiffs_unregister(
                label
            );

        if ((unregister_result != ESP_OK) &&
            (unregister_result !=
             ESP_ERR_INVALID_STATE)) {

            ESP_LOGE(
                TAG,
                "Failed to unmount rejected partition '%s': %s",
                label,
                esp_err_to_name(unregister_result)
            );

            result = unregister_result;
            break;
        }
    }

    if (!s_is_mounted) {
        storage_service_unlock();

        (void)system_model_set_storage_ready(false);

        return
            result != ESP_OK
                ? result
                : first_error;
    }

    if (s_fallback_used) {
        const esp_err_t save_result =
            storage_service_save_selection(
                s_active_partition
            );

        if (save_result == ESP_OK) {
            s_selected_partition =
                s_active_partition;

        } else {
            ESP_LOGW(
                TAG,
                "Failed to persist fallback partition: %s",
                esp_err_to_name(save_result)
            );
        }
    }

    size_t total_bytes = 0U;
    size_t used_bytes = 0U;

    result = esp_spiffs_info(
        storage_service_partition_label(
            s_active_partition
        ),
        &total_bytes,
        &used_bytes
    );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to get SPIFFS information: %s",
            esp_err_to_name(result)
        );

        const esp_err_t unregister_result =
            esp_vfs_spiffs_unregister(
                storage_service_partition_label(
                    s_active_partition
                )
            );

        if (unregister_result == ESP_OK) {
            s_is_mounted = false;

        } else {
            /*
             * Registration remains active because cleanup failed.
             */
            s_is_mounted = true;

            ESP_LOGE(
                TAG,
                "Failed to roll back SPIFFS mount: %s",
                esp_err_to_name(unregister_result)
            );
        }

        storage_service_unlock();

        const esp_err_t model_result =
            system_model_set_storage_ready(
                s_is_mounted
            );

        if (model_result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Failed to update storage model: %s",
                esp_err_to_name(model_result)
            );
        }

        return result;
    }

    storage_service_unlock();

    ESP_LOGI(
        TAG,
        "SPIFFS mounted: partition=%s, total=%u, used=%u, "
        "fallback=%u",
        storage_service_partition_label(
            s_active_partition
        ),
        (unsigned int)total_bytes,
        (unsigned int)used_bytes,
        (unsigned int)s_fallback_used
    );

    const esp_err_t model_result =
        system_model_set_storage_ready(true);

    if (model_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to update storage model: %s",
            esp_err_to_name(model_result)
        );
    }

    return ESP_OK;
}

esp_err_t storage_service_deinit(void)
{
    const esp_err_t lock_result =
        storage_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    if (!s_is_mounted) {
        storage_service_unlock();
        return ESP_OK;
    }

    const esp_err_t result =
        esp_vfs_spiffs_unregister(
            storage_service_partition_label(
                s_active_partition
            )
        );

    if (result == ESP_OK) {
        s_is_mounted = false;
    }

    storage_service_unlock();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to unmount SPIFFS: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    const esp_err_t model_result =
        system_model_set_storage_ready(false);

    if (model_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to update storage model: %s",
            esp_err_to_name(model_result)
        );
    }

    return ESP_OK;
}

esp_err_t storage_service_get_mounted(
    bool *mounted
)
{
    if (mounted == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *mounted = false;

    const esp_err_t result =
        storage_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    *mounted = s_is_mounted;

    storage_service_unlock();

    return ESP_OK;
}

esp_err_t storage_service_get_partition_info(
    storage_service_partition_info_t *info
)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(
        info,
        0,
        sizeof(*info)
    );

    const esp_err_t result =
        storage_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    info->active_partition =
        s_active_partition;

    info->selected_partition =
        s_selected_partition;

    info->mounted = s_is_mounted;
    info->fallback_used = s_fallback_used;

    storage_service_unlock();

    return ESP_OK;
}

esp_err_t storage_service_validate_partition(
    storage_service_partition_t partition
)
{
    if (!storage_service_partition_valid(
            partition
        )) {

        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t lock_result =
        storage_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    const esp_err_t result =
        storage_service_validate_partition_locked(
            partition
        );

    storage_service_unlock();

    return result;
}

esp_err_t storage_service_select_partition(
    storage_service_partition_t partition
)
{
    if (!storage_service_partition_valid(
            partition
        )) {

        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t lock_result =
        storage_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    esp_err_t result =
        storage_service_validate_partition_locked(
            partition
        );

    if (result == ESP_OK) {
        result =
            storage_service_save_selection(
                partition
            );
    }

    if (result == ESP_OK) {
        s_selected_partition = partition;

        ESP_LOGI(
            TAG,
            "Storage partition selected for next boot: %s",
            storage_service_partition_label(
                partition
            )
        );
    }

    storage_service_unlock();

    return result;
}

static bool storage_service_is_valid_path(
    const char *path,
    bool allow_root
)
{
    if (path == NULL) {
        return false;
    }

    const size_t base_length =
        strlen(STORAGE_BASE_PATH);

    const size_t path_length =
        strlen(path);

    if (path_length < base_length) {
        return false;
    }

    if (strncmp(
            path,
            STORAGE_BASE_PATH,
            base_length
        ) != 0) {

        return false;
    }

    if (path_length == base_length) {
        return allow_root;
    }

    if (path[base_length] != '/') {
        return false;
    }

    /*
     * Reject parent-directory traversal, repeated separators and
     * Windows-style path separators.
     */
    if ((strstr(path, "..") != NULL) ||
        (strstr(
            path + base_length,
            "//"
        ) != NULL) ||
        (strchr(path, '\\') != NULL)) {

        return false;
    }

    /*
     * Reject an empty path below the mount point.
     */
    if (path[base_length + 1U] == '\0') {
        return false;
    }

    return true;
}

static esp_err_t storage_service_read_file_locked(
    const char *path,
    char **out_data,
    size_t *out_size
)
{
    /*
     * Require an absolute path to a file located below the
     * internal storage mount point.
     */
    if (!storage_service_is_valid_path(
            path,
            false
        )) {

        return ESP_ERR_INVALID_ARG;
    }

    errno = 0;

    FILE *file = fopen(
        path,
        "rb"
    );

    if (file == NULL) {
        if (errno == ENOENT) {
            return ESP_ERR_NOT_FOUND;
        }

        return ESP_FAIL;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        (void)fclose(file);
        return ESP_FAIL;
    }

    const long file_size = ftell(file);

    if (file_size < 0L) {
        (void)fclose(file);
        return ESP_FAIL;
    }

    if (fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        return ESP_FAIL;
    }

    if ((uintmax_t)file_size >
        (uintmax_t)(SIZE_MAX - 1U)) {

        (void)fclose(file);

        return ESP_ERR_INVALID_SIZE;
    }

    const size_t allocation_size =
        (size_t)file_size + 1U;

    char *data =
        heap_caps_malloc(
            allocation_size,
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

    if (data == NULL) {
        (void)fclose(file);
        return ESP_ERR_NO_MEM;
    }

    const size_t bytes_read = fread(
        data,
        1U,
        (size_t)file_size,
        file
    );

    const int close_result =
        fclose(file);

    if ((bytes_read != (size_t)file_size) ||
        (close_result != 0)) {

        free(data);
        return ESP_FAIL;
    }

    data[bytes_read] = '\0';

    *out_data = data;
    *out_size = bytes_read;

    return ESP_OK;
}

static bool storage_service_get_child_name(
    const char *file_name,
    const char *prefix,
    char *out_name,
    size_t out_name_size,
    bool *out_is_directory
)
{
    if ((file_name == NULL) ||
        (prefix == NULL) ||
        (out_name == NULL) ||
        (out_name_size == 0U) ||
        (out_is_directory == NULL)) {

        return false;
    }

    /*
     * Some SPIFFS/VFS versions return names with a leading slash.
     */
    while (*file_name == '/') {
        file_name++;
    }

    const size_t prefix_length =
        strlen(prefix);

    if (prefix_length > 0U) {
        if (strncmp(
                file_name,
                prefix,
                prefix_length
            ) != 0) {

            return false;
        }

        file_name += prefix_length;
    }

    if (*file_name == '\0') {
        return false;
    }

    const char *separator =
        strchr(
            file_name,
            '/'
        );

    const size_t name_length =
        separator != NULL
            ? (size_t)(separator - file_name)
            : strlen(file_name);

    if ((name_length == 0U) ||
        (name_length >= out_name_size)) {

        return false;
    }

    memcpy(
        out_name,
        file_name,
        name_length
    );

    out_name[name_length] = '\0';

    *out_is_directory =
        separator != NULL;

    return true;
}

static storage_file_entry_t *
storage_service_find_entry(
    storage_file_entry_t *entries,
    size_t entry_count,
    const char *name
)
{
    if ((entries == NULL) ||
        (name == NULL)) {

        return NULL;
    }

    for (size_t index = 0U;
         index < entry_count;
         index++) {

        if (strcmp(
                entries[index].name,
                name
            ) == 0) {

            return &entries[index];
        }
    }

    return NULL;
}

static esp_err_t storage_service_list_locked(
    const char *path,
    size_t offset,
    storage_file_entry_t *entries,
    size_t capacity,
    size_t *out_count,
    bool *out_has_more
)
{
    if (!storage_service_is_valid_path(
            path,
            true
        )) {

        return ESP_ERR_INVALID_ARG;
    }

    if ((capacity == 0U) ||
        (offset >
         STORAGE_LIST_MAX_RESULT_COUNT) ||
        (capacity >
         STORAGE_LIST_MAX_RESULT_COUNT) ||
        (offset >
         STORAGE_LIST_MAX_RESULT_COUNT -
         capacity)) {

        return ESP_ERR_INVALID_ARG;
    }

    const size_t requested_count =
        offset + capacity;

    /*
     * One additional entry is collected to determine whether another
     * page is available.
     */
    const size_t collection_capacity =
        requested_count <
        STORAGE_LIST_MAX_RESULT_COUNT
            ? requested_count + 1U
            : requested_count;

    storage_file_entry_t *collected =
        heap_caps_calloc(
            collection_capacity,
            sizeof(*collected),
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

    if (collected == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const size_t base_length =
        strlen(STORAGE_BASE_PATH);

    const char *relative_path =
        path + base_length;

    while (*relative_path == '/') {
        relative_path++;
    }

    char prefix[
        STORAGE_FILE_NAME_MAX_LENGTH
    ] = {0};

    if (*relative_path != '\0') {
        const int prefix_length =
            snprintf(
                prefix,
                sizeof(prefix),
                "%s/",
                relative_path
            );

        if ((prefix_length < 0) ||
            ((size_t)prefix_length >=
             sizeof(prefix))) {

            free(collected);

            return ESP_ERR_INVALID_ARG;
        }
    }

    errno = 0;

    DIR *directory =
        opendir(
            STORAGE_BASE_PATH
        );

    if (directory == NULL) {
        free(collected);

        if (errno == ENOENT) {
            return ESP_ERR_NOT_FOUND;
        }

        return ESP_FAIL;
    }

    esp_err_t result = ESP_OK;
    size_t collected_count = 0U;

    for (;;) {
        errno = 0;

        struct dirent *entry =
            readdir(directory);

        if (entry == NULL) {
            if (errno != 0) {
                result = ESP_FAIL;
            }

            break;
        }

        char child_name[
            STORAGE_FILE_NAME_MAX_LENGTH
        ] = {0};

        bool is_directory = false;

        if (!storage_service_get_child_name(
                entry->d_name,
                prefix,
                child_name,
                sizeof(child_name),
                &is_directory
            )) {

            continue;
        }

        storage_file_entry_t *existing =
            storage_service_find_entry(
                collected,
                collected_count,
                child_name
            );

        if (existing != NULL) {
            /*
             * Prefer a logical directory when SPIFFS contains both a file
             * and one or more files using the same name as a path prefix.
             */
            if (is_directory) {
                existing->is_directory = true;
                existing->size = 0U;
            }

            continue;
        }

        if (collected_count >=
            collection_capacity) {

            *out_has_more = true;
            break;
        }

        storage_file_entry_t *destination =
            &collected[collected_count];

        (void)strlcpy(
            destination->name,
            child_name,
            sizeof(destination->name)
        );

        destination->is_directory =
            is_directory;

        destination->size = 0U;

        if (!is_directory) {
            char full_path[
                PATH_MAX
            ];

            const int written =
                snprintf(
                    full_path,
                    sizeof(full_path),
                    "%s/%s%s",
                    STORAGE_BASE_PATH,
                    prefix,
                    child_name
                );

            if ((written < 0) ||
                ((size_t)written >=
                 sizeof(full_path))) {

                result =
                    ESP_ERR_INVALID_SIZE;

                break;
            }

            struct stat file_stat;

            if (stat(
                    full_path,
                    &file_stat
                ) != 0) {

                result =
                    errno == ENOENT
                        ? ESP_ERR_NOT_FOUND
                        : ESP_FAIL;

                break;
            }

            if (file_stat.st_size < 0) {
                result = ESP_FAIL;
                break;
            }

            destination->size =
                (size_t)file_stat.st_size;
        }

        collected_count++;
    }

    const int close_result =
        closedir(directory);

    if ((result == ESP_OK) &&
        (close_result != 0)) {

        result = ESP_FAIL;
    }

    if (result != ESP_OK) {
        free(collected);

        return result;
    }

    if ((relative_path[0] != '\0') &&
        (collected_count == 0U)) {

        free(collected);

        return ESP_ERR_NOT_FOUND;
    }

    if (collected_count > requested_count) {
        *out_has_more = true;
    }

    if (collected_count > offset) {
        size_t available =
            collected_count - offset;

        if (available > capacity) {
            available = capacity;
        }

        memcpy(
            entries,
            &collected[offset],
            available * sizeof(*entries)
        );

        *out_count = available;
    }

    free(collected);

    return ESP_OK;
}

static esp_err_t storage_service_stream_file_locked(
    const char *path,
    storage_service_stream_callback_t callback,
    void *context
)
{
    if ((path == NULL) ||
        (callback == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (!storage_service_is_valid_path(
            path,
            false
        )) {

        return ESP_ERR_INVALID_ARG;
    }

    errno = 0;

    FILE *file = fopen(
        path,
        "rb"
    );

    if (file == NULL) {
        const int open_errno = errno;

        if (open_errno == ENOENT) {
            return ESP_ERR_NOT_FOUND;
        }

        ESP_LOGE(
            TAG,
            "Failed to open file '%s': errno=%d",
            path,
            open_errno
        );

        return ESP_FAIL;
    }

    uint8_t *stream_buffer =
        heap_caps_malloc(
            STORAGE_STREAM_BUFFER_SIZE,
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

    if (stream_buffer == NULL) {
        const int close_result =
            fclose(file);

        if (close_result != 0) {
            ESP_LOGW(
                TAG,
                "Failed to close file '%s' after "
                "stream buffer allocation failure",
                path
            );
        }

        return ESP_ERR_NO_MEM;
    }

    esp_err_t result = ESP_OK;

    while (true) {
        const size_t bytes_read =
            fread(
                stream_buffer,
                1U,
                STORAGE_STREAM_BUFFER_SIZE,
                file
            );

        if (bytes_read > 0U) {
            result = callback(
                stream_buffer,
                bytes_read,
                context
            );

            if (result != ESP_OK) {
                break;
            }
        }

        if (bytes_read <
            STORAGE_STREAM_BUFFER_SIZE) {

            if (ferror(file) != 0) {
                ESP_LOGE(
                    TAG,
                    "Failed to read file '%s'",
                    path
                );

                result = ESP_FAIL;
            }

            break;
        }
    }

    errno = 0;

    const int close_result =
        fclose(file);

    const int close_errno =
        errno;

    free(stream_buffer);

    if ((close_result != 0) &&
        (result == ESP_OK)) {

        ESP_LOGE(
            TAG,
            "Failed to close file '%s': errno=%d",
            path,
            close_errno
        );

        result = ESP_FAIL;
    }

    return result;
}

esp_err_t storage_service_read_file(
    const char *path,
    char **out_data,
    size_t *out_size
)
{
    if ((path == NULL) ||
        (out_data == NULL) ||
        (out_size == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *out_data = NULL;
    *out_size = 0U;

    const esp_err_t lock_result =
        storage_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    esp_err_t result = ESP_ERR_INVALID_STATE;

    if (s_is_mounted) {
        result = storage_service_read_file_locked(
            path,
            out_data,
            out_size
        );
    }

    storage_service_unlock();

    return result;
}

esp_err_t storage_service_list(
    const char *path,
    size_t offset,
    storage_file_entry_t *entries,
    size_t capacity,
    size_t *out_count,
    bool *out_has_more
)
{
    if ((path == NULL) ||
        (entries == NULL) ||
        (capacity == 0U) ||
        (out_count == NULL) ||
        (out_has_more == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *out_count = 0U;
    *out_has_more = false;

    const esp_err_t lock_result =
        storage_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    esp_err_t result =
        ESP_ERR_INVALID_STATE;

    if (s_is_mounted) {
        result =
            storage_service_list_locked(
                path,
                offset,
                entries,
                capacity,
                out_count,
                out_has_more
            );
    }

    storage_service_unlock();

    return result;
}

esp_err_t storage_service_stream_file(
    const char *path,
    storage_service_stream_callback_t callback,
    void *context
)
{
    if ((path == NULL) ||
        (callback == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t lock_result =
        storage_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    esp_err_t result =
        ESP_ERR_INVALID_STATE;

    if (s_is_mounted) {
        result =
            storage_service_stream_file_locked(
                path,
                callback,
                context
            );
    }

    storage_service_unlock();

    return result;
}
