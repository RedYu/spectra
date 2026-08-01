#include "storage_service.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "system_model.h"

#define STORAGE_LOCK_TIMEOUT_MS  (1000U)

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

static esp_err_t storage_service_read_file_locked(
    const char *path,
    char **out_data,
    size_t *out_size
)
{
    const size_t base_path_length =
        strlen(STORAGE_BASE_PATH);

    const size_t path_length =
        strlen(path);

    /*
     * Require an absolute path to a file located below the
     * internal storage mount point.
     */
    if ((path_length <= base_path_length) ||
        (strncmp(
            path,
            STORAGE_BASE_PATH,
            base_path_length
        ) != 0) ||
        (path[base_path_length] != '/') ||
        (path[base_path_length + 1U] == '\0')) {

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
