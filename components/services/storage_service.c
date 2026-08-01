#include "storage_service.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "system_model.h"

#define STORAGE_LOCK_TIMEOUT_MS        (1000U)

static const char *TAG = "storage_service";

static const char STORAGE_PARTITION_LABEL[] = "storage";
static const char STORAGE_BASE_PATH[] = "/storage";

static SemaphoreHandle_t s_mutex = NULL;
static bool s_is_mounted = false;

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

    const esp_vfs_spiffs_conf_t config = {
        .base_path = STORAGE_BASE_PATH,
        .partition_label = STORAGE_PARTITION_LABEL,
        .max_files = 8U,
        .format_if_mount_failed = false,
    };

    esp_err_t result =
        esp_vfs_spiffs_register(
            &config
        );

    if (result != ESP_OK) {
        storage_service_unlock();

        ESP_LOGE(
            TAG,
            "Failed to mount SPIFFS: %s",
            esp_err_to_name(result)
        );

        const esp_err_t model_result =
            system_model_set_storage_ready(false);

        if (model_result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Failed to update storage model: %s",
                esp_err_to_name(model_result)
            );
        }

        return result;
    }

    size_t total_bytes = 0U;
    size_t used_bytes = 0U;

    result = esp_spiffs_info(
        STORAGE_PARTITION_LABEL,
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
                STORAGE_PARTITION_LABEL
            );

        if (unregister_result != ESP_OK) {
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

    s_is_mounted = true;

    storage_service_unlock();

    ESP_LOGI(
        TAG,
        "SPIFFS mounted: total=%u, used=%u",
        (unsigned int)total_bytes,
        (unsigned int)used_bytes
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
            STORAGE_PARTITION_LABEL
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

    const size_t allocation_size =
        (size_t)file_size + 1U;

    char *data = malloc(
        allocation_size
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
        calloc(
            collection_capacity,
            sizeof(*collected)
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
