#include "storage_sd_service.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "sd_card_driver.h"

#include "system_model.h"

#define STORAGE_SD_MOUNT_POINT      SD_CARD_MOUNT_POINT
#define STORAGE_SD_MAX_PATH_LENGTH  256
#define STORAGE_SD_LOCK_TIMEOUT_MS  3000

#define STORAGE_MONITOR_INTERVAL_MS     1000
#define STORAGE_MAX_STATUS_FAILURES     1
#define STORAGE_REMOUNT_INTERVAL_MS     2000

static storage_sd_state_t s_state = STORAGE_STATE_UNAVAILABLE;

static const char *TAG = "storage_sd_service";

/**
 * @brief Convert errno to an ESP-IDF error code.
 */
static esp_err_t storage_sd_service_errno_to_error(
    int error_number
)
{
    switch (error_number) {
        case 0:
            return ESP_OK;

        case ENOENT:
            return ESP_ERR_NOT_FOUND;

        case EINVAL:
            return ESP_ERR_INVALID_ARG;

        case ENOMEM:
            return ESP_ERR_NO_MEM;

        case EACCES:
        case EPERM:
            return ESP_ERR_NOT_ALLOWED;

        case ENOSPC:
            return ESP_ERR_NO_MEM;

        case EBUSY:
            return ESP_ERR_INVALID_STATE;

        default:
            return ESP_FAIL;
    }
}

/**
 * @brief Verify that the SD card can be accessed.
 */
static esp_err_t storage_sd_service_validate_state(void)
{
    if (!sd_card_driver_is_mounted()) {
        ESP_LOGW(
            TAG,
            "SD card is not mounted"
        );

        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

/**
 * @brief Build an absolute VFS path.
 */
static esp_err_t storage_sd_service_build_path(
    const char *path,
    char *full_path,
    size_t full_path_size
)
{
    if (path == NULL ||
        full_path == NULL ||
        full_path_size == 0) {

        return ESP_ERR_INVALID_ARG;
    }

    if (path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    int written;

    /*
     * Do not add the mount point twice when the caller has
     * already provided a complete SD card path.
     */
    if (strncmp(
            path,
            STORAGE_SD_MOUNT_POINT,
            strlen(STORAGE_SD_MOUNT_POINT)
        ) == 0) {

        written = snprintf(
            full_path,
            full_path_size,
            "%s",
            path
        );
    } else if (path[0] == '/') {
        written = snprintf(
            full_path,
            full_path_size,
            STORAGE_SD_MOUNT_POINT "%s",
            path
        );
    } else {
        written = snprintf(
            full_path,
            full_path_size,
            STORAGE_SD_MOUNT_POINT "/%s",
            path
        );
    }

    if (written < 0 ||
        (size_t)written >= full_path_size) {

        ESP_LOGE(
            TAG,
            "SD path is too long"
        );

        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

/**
 * @brief Lock the shared LCD/SD SPI bus.
 */
static esp_err_t storage_sd_service_lock(void)
{
    if (!board_spi_lock(
            pdMS_TO_TICKS(
                STORAGE_SD_LOCK_TIMEOUT_MS
            )
        )) {

        ESP_LOGW(
            TAG,
            "Timed out waiting for the SPI bus"
        );

        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t storage_sd_service_open(
    const char *path,
    const char *mode,
    FILE **out_file
)
{
    if (path == NULL ||
        mode == NULL ||
        out_file == NULL) {

        return ESP_ERR_INVALID_ARG;
    }

    *out_file = NULL;

    esp_err_t result =
        storage_sd_service_validate_state();

    if (result != ESP_OK) {
        return result;
    }

    char full_path[
        STORAGE_SD_MAX_PATH_LENGTH
    ];

    result = storage_sd_service_build_path(
        path,
        full_path,
        sizeof(full_path)
    );

    if (result != ESP_OK) {
        return result;
    }

    result = storage_sd_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    errno = 0;

    FILE *file = fopen(
        full_path,
        mode
    );

    const int saved_errno = errno;

    board_spi_unlock();

    if (file == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to open '%s': errno=%d (%s)",
            full_path,
            saved_errno,
            strerror(saved_errno)
        );

        return storage_sd_service_errno_to_error(
            saved_errno
        );
    }

    *out_file = file;

    ESP_LOGD(
        TAG,
        "File opened: %s",
        full_path
    );

    return ESP_OK;
}

esp_err_t storage_sd_service_read(
    FILE *file,
    void *buffer,
    size_t size,
    size_t *out_bytes_read
)
{
    if (file == NULL ||
        buffer == NULL ||
        out_bytes_read == NULL) {

        return ESP_ERR_INVALID_ARG;
    }

    *out_bytes_read = 0;

    if (size == 0) {
        return ESP_OK;
    }

    esp_err_t result =
        storage_sd_service_validate_state();

    if (result != ESP_OK) {
        return result;
    }

    result = storage_sd_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    errno = 0;
    clearerr(file);

    const size_t bytes_read = fread(
        buffer,
        1,
        size,
        file
    );

    const bool read_error =
        ferror(file) != 0;

    const int saved_errno = errno;

    board_spi_unlock();

    *out_bytes_read = bytes_read;

    if (read_error) {
        ESP_LOGE(
            TAG,
            "Failed to read file: errno=%d (%s)",
            saved_errno,
            strerror(saved_errno)
        );

        return storage_sd_service_errno_to_error(
            saved_errno
        );
    }

    /*
     * bytes_read may be less than size at the end of the file.
     * This is not considered an error.
     */
    return ESP_OK;
}

esp_err_t storage_sd_service_seek(
    FILE *file,
    long offset,
    int origin
)
{
    if (file == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (origin != SEEK_SET &&
        origin != SEEK_CUR &&
        origin != SEEK_END) {

        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        storage_sd_service_validate_state();

    if (result != ESP_OK) {
        return result;
    }

    result = storage_sd_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    errno = 0;

    const int seek_result = fseek(
        file,
        offset,
        origin
    );

    const int saved_errno = errno;

    board_spi_unlock();

    if (seek_result != 0) {
        ESP_LOGE(
            TAG,
            "Failed to seek file: errno=%d (%s)",
            saved_errno,
            strerror(saved_errno)
        );

        return storage_sd_service_errno_to_error(
            saved_errno
        );
    }

    return ESP_OK;
}

esp_err_t storage_sd_service_flush(
    FILE *file
)
{
    if (file == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        storage_sd_service_validate_state();

    if (result != ESP_OK) {
        return result;
    }

    result = storage_sd_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    errno = 0;

    const int flush_result =
        fflush(file);

    const int saved_errno = errno;

    board_spi_unlock();

    if (flush_result != 0) {
        ESP_LOGE(
            TAG,
            "Failed to flush file: errno=%d (%s)",
            saved_errno,
            strerror(saved_errno)
        );

        return storage_sd_service_errno_to_error(
            saved_errno
        );
    }

    return ESP_OK;
}

esp_err_t storage_sd_service_close(
    FILE **file
)
{
    if (file == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (*file == NULL) {
        return ESP_OK;
    }

    /*
     * fclose() must still be attempted even if the storage state
     * has already changed to unavailable.
     */
    esp_err_t result =
        storage_sd_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    FILE *file_to_close = *file;

    errno = 0;

    const int close_result =
        fclose(file_to_close);

    const int saved_errno = errno;

    /*
     * fclose() invalidates the stream even when it reports an error.
     */
    *file = NULL;

    board_spi_unlock();

    if (close_result != 0) {
        ESP_LOGE(
            TAG,
            "Failed to close file: errno=%d (%s)",
            saved_errno,
            strerror(saved_errno)
        );

        return storage_sd_service_errno_to_error(
            saved_errno
        );
    }

    return ESP_OK;
}

esp_err_t storage_sd_service_remove(
    const char *path
)
{
    if (path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        storage_sd_service_validate_state();

    if (result != ESP_OK) {
        return result;
    }

    char full_path[
        STORAGE_SD_MAX_PATH_LENGTH
    ];

    result = storage_sd_service_build_path(
        path,
        full_path,
        sizeof(full_path)
    );

    if (result != ESP_OK) {
        return result;
    }

    result = storage_sd_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    errno = 0;

    const int remove_result =
        remove(full_path);

    const int saved_errno = errno;

    board_spi_unlock();

    if (remove_result != 0) {
        ESP_LOGE(
            TAG,
            "Failed to remove '%s': errno=%d (%s)",
            full_path,
            saved_errno,
            strerror(saved_errno)
        );

        return storage_sd_service_errno_to_error(
            saved_errno
        );
    }

    ESP_LOGD(
        TAG,
        "File removed: %s",
        full_path
    );

    return ESP_OK;
}

esp_err_t storage_sd_service_rename(
    const char *old_path,
    const char *new_path
)
{
    if (old_path == NULL ||
        new_path == NULL) {

        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        storage_sd_service_validate_state();

    if (result != ESP_OK) {
        return result;
    }

    char old_full_path[
        STORAGE_SD_MAX_PATH_LENGTH
    ];

    char new_full_path[
        STORAGE_SD_MAX_PATH_LENGTH
    ];

    result = storage_sd_service_build_path(
        old_path,
        old_full_path,
        sizeof(old_full_path)
    );

    if (result != ESP_OK) {
        return result;
    }

    result = storage_sd_service_build_path(
        new_path,
        new_full_path,
        sizeof(new_full_path)
    );

    if (result != ESP_OK) {
        return result;
    }

    result = storage_sd_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    errno = 0;

    const int rename_result =
        rename(
            old_full_path,
            new_full_path
        );

    const int saved_errno = errno;

    board_spi_unlock();

    if (rename_result != 0) {
        ESP_LOGE(
            TAG,
            "Failed to rename '%s' to '%s': errno=%d (%s)",
            old_full_path,
            new_full_path,
            saved_errno,
            strerror(saved_errno)
        );

        return storage_sd_service_errno_to_error(
            saved_errno
        );
    }

    ESP_LOGD(
        TAG,
        "File renamed: %s -> %s",
        old_full_path,
        new_full_path
    );

    return ESP_OK;
}

esp_err_t storage_sd_service_stat(
    const char *path,
    struct stat *out_stat
)
{
    if (path == NULL ||
        out_stat == NULL) {

        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        storage_sd_service_validate_state();

    if (result != ESP_OK) {
        return result;
    }

    char full_path[
        STORAGE_SD_MAX_PATH_LENGTH
    ];

    result = storage_sd_service_build_path(
        path,
        full_path,
        sizeof(full_path)
    );

    if (result != ESP_OK) {
        return result;
    }

    result = storage_sd_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    errno = 0;

    const int stat_result =
        stat(
            full_path,
            out_stat
        );

    const int saved_errno = errno;

    board_spi_unlock();

    if (stat_result != 0) {
        ESP_LOGE(
            TAG,
            "Failed to stat '%s': errno=%d (%s)",
            full_path,
            saved_errno,
            strerror(saved_errno)
        );

        return storage_sd_service_errno_to_error(
            saved_errno
        );
    }

    return ESP_OK;
}


void storage_sd_service_set_state(
    storage_sd_state_t state
)
{
    if (s_state == state) {
        return;
    }

    s_state = state;

    const bool mounted = state != STORAGE_STATE_UNAVAILABLE;

    const esp_err_t result =
        system_model_set_sd_card_mounted(
            mounted
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to update SD card state: %s",
            esp_err_to_name(result)
        );
    }

    ESP_LOGI(
        TAG,
        "Storage SD state changed to %d",
        state
    );
}

static void storage_sd_monitor_task(
    void *argument
)
{
    (void)argument;

    uint8_t failure_count = 0;

    ESP_LOGI(
        TAG,
        "Storage SD monitor task started"
    );

    while (true) {
        if (sd_card_driver_is_mounted()) {
            const esp_err_t result =
                sd_card_driver_check();

            if (result == ESP_OK) {
                failure_count = 0;
            } else {
                failure_count++;

                ESP_LOGW(
                    TAG,
                    "SD card status check failed %u/%u: %s",
                    (unsigned int)failure_count,
                    STORAGE_MAX_STATUS_FAILURES,
                    esp_err_to_name(result)
                );

                if (failure_count >=
                    STORAGE_MAX_STATUS_FAILURES) {

                    ESP_LOGE(
                        TAG,
                        "SD card is no longer responding"
                    );

                    /*
                     * First stop CAN logging and prevent any new
                     * storage operations.
                     */
                    storage_sd_service_set_state(STORAGE_STATE_UNAVAILABLE);

                    /*
                     * Close files owned by the storage service.
                     */
                    //storage_service_close_all_files();

                    /*
                     * Remove the broken FATFS mount.
                     */
                    sd_card_driver_unmount();

                    failure_count = 0;
                }
            }

            vTaskDelay(
                pdMS_TO_TICKS(
                    STORAGE_MONITOR_INTERVAL_MS
                )
            );
        } else {
            /*
             * Without a Card Detect contact, the only way to detect
             * insertion is to periodically attempt mounting.
             */
            const esp_err_t result =
                sd_card_driver_mount();

            if (result == ESP_OK) {
                ESP_LOGI(
                    TAG,
                    "SD card detected and mounted"
                );

                storage_sd_service_set_state(STORAGE_STATE_MOUNTED);
            }

            vTaskDelay(
                pdMS_TO_TICKS(
                    STORAGE_REMOUNT_INTERVAL_MS
                )
            );
        }
    }
}

esp_err_t storage_sd_service_start(void)
{
    ESP_RETURN_ON_ERROR(
        sd_card_driver_init(),
        TAG,
        "Failed to initialize SD card driver"
    );

    /*
     * Initial mount attempt. Absence of the SD card is not fatal.
     */
    const esp_err_t mount_result =
        sd_card_driver_mount();

    storage_sd_service_set_state(mount_result == ESP_OK ? 
        STORAGE_STATE_MOUNTED : STORAGE_STATE_UNAVAILABLE
    );

    const BaseType_t task_created =
        xTaskCreate(
            storage_sd_monitor_task,
            "storage_sd_monitor",
            4096,
            NULL,
            4,
            NULL
        );

    if (task_created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
