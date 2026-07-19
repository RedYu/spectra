#include "storage_service.h"

#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_spiffs.h"

static const char *TAG = "storage_service";

static const char *STORAGE_PARTITION_LABEL = "storage";
static const char *STORAGE_BASE_PATH = "/storage";

static bool s_is_mounted;

esp_err_t storage_service_init(void)
{
    if (s_is_mounted) {
        return ESP_OK;
    }

    const esp_vfs_spiffs_conf_t config = {
        .base_path = STORAGE_BASE_PATH,
        .partition_label = STORAGE_PARTITION_LABEL,
        .max_files = 8,
        .format_if_mount_failed = false,
    };

    esp_err_t err = esp_vfs_spiffs_register(&config);

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to mount SPIFFS: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    size_t total_bytes = 0;
    size_t used_bytes = 0;

    err = esp_spiffs_info(
        STORAGE_PARTITION_LABEL,
        &total_bytes,
        &used_bytes
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to get SPIFFS information: %s",
            esp_err_to_name(err)
        );

        esp_vfs_spiffs_unregister(STORAGE_PARTITION_LABEL);
        return err;
    }

    s_is_mounted = true;

    ESP_LOGI(
        TAG,
        "SPIFFS mounted: total=%u, used=%u",
        (unsigned int)total_bytes,
        (unsigned int)used_bytes
    );

    return ESP_OK;
}

void storage_service_deinit(void)
{
    if (!s_is_mounted) {
        return;
    }

    esp_vfs_spiffs_unregister(STORAGE_PARTITION_LABEL);
    s_is_mounted = false;
}

bool storage_service_is_mounted(void)
{
    return s_is_mounted;
}

esp_err_t storage_service_read_file(
    const char *path,
    char **out_data,
    size_t *out_size
)
{
    if (path == NULL || out_data == NULL || out_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_data = NULL;
    *out_size = 0;

    if (!s_is_mounted) {
        ESP_LOGE(TAG, "Storage is not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return ESP_FAIL;
    }

    const long file_size = ftell(file);

    if (file_size < 0) {
        fclose(file);
        return ESP_FAIL;
    }

    rewind(file);

    /*
     * One additional byte is allocated for the null terminator.
     * This allows the buffer to be used as a C string.
     */
    char *data = malloc((size_t)file_size + 1);

    if (data == NULL) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    const size_t bytes_read = fread(
        data,
        1,
        (size_t)file_size,
        file
    );

    fclose(file);

    if (bytes_read != (size_t)file_size) {
        free(data);
        return ESP_FAIL;
    }

    data[bytes_read] = '\0';

    *out_data = data;
    *out_size = bytes_read;

    return ESP_OK;
}
