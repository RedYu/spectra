#include "storage_sd_service.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "sd_card_driver.h"
#include "logging_service.h"
#include "system_model.h"
#include "settings_model.h"

#define STORAGE_SD_MUTEX_TIMEOUT_MS  (1000U)

#define STORAGE_SD_MOUNT_POINT      SD_CARD_MOUNT_POINT
#define STORAGE_SD_MAX_PATH_LENGTH  (256U)
#define STORAGE_SD_LOCK_TIMEOUT_MS  (3000U)

static const char *TAG = "storage_sd_service";

static storage_sd_state_t s_state =
    STORAGE_SD_STATE_UNAVAILABLE;

static SemaphoreHandle_t s_mutex = NULL;
static bool s_started = false;

static size_t s_open_file_count = 0U;

/*
 * TODO:
 * - Restore file logging if SD card unmounting fails because files
 *   are still open. Currently, logging remains disabled while the
 *   card stays mounted.
 *
 * - Use STORAGE_SD_STATE_ERROR for unrecoverable filesystem, SPI,
 *   or unexpected card-removal errors.
 *
 * - Reject ".." path components in storage_sd_service_build_path()
 *   before accepting paths from untrusted external sources.
 */

static esp_err_t storage_sd_service_mutex_lock(void)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_mutex,
            pdMS_TO_TICKS(
                STORAGE_SD_MUTEX_TIMEOUT_MS
            )
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void storage_sd_service_mutex_unlock(void)
{
    (void)xSemaphoreGive(s_mutex);
}

static esp_err_t storage_sd_service_errno_to_error(
    int error_number
)
{
    switch (error_number) {
        case ENOENT:
            return ESP_ERR_NOT_FOUND;

        case EINVAL:
            return ESP_ERR_INVALID_ARG;

        case ENOMEM:
        case ENOSPC:
            return ESP_ERR_NO_MEM;

        case EACCES:
        case EPERM:
            return ESP_ERR_NOT_ALLOWED;

        case EBUSY:
            return ESP_ERR_INVALID_STATE;

        default:
            return ESP_FAIL;
    }
}

static esp_err_t storage_sd_service_validate_state_locked(void)
{
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_state != STORAGE_SD_STATE_MOUNTED) {
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

    const size_t mount_length =
        strlen(STORAGE_SD_MOUNT_POINT);

    const bool has_mount_prefix =
        (strncmp(
            path,
            STORAGE_SD_MOUNT_POINT,
            mount_length
        ) == 0) &&
        ((path[mount_length] == '/') ||
        (path[mount_length] == '\0'));

    int written;

    /*
     * Do not add the mount point twice when the caller has
     * already provided a complete SD card path.
     */
    if (has_mount_prefix) {
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
static esp_err_t storage_sd_service_spi_lock(void)
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
    if ((path == NULL) ||
        (mode == NULL) ||
        (out_file == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *out_file = NULL;

    esp_err_t result =
        storage_sd_service_mutex_lock();

    if (result != ESP_OK) {
        return result;
    }

    result =
        storage_sd_service_validate_state_locked();

    if (result != ESP_OK) {
        storage_sd_service_mutex_unlock();
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
        storage_sd_service_mutex_unlock();
        return result;
    }

    result = storage_sd_service_spi_lock();

    if (result != ESP_OK) {
        storage_sd_service_mutex_unlock();
        return result;
    }

    errno = 0;

    FILE *file = fopen(
        full_path,
        mode
    );

    const int saved_errno = errno;

    if (file != NULL) {
        ++s_open_file_count;
    }

    board_spi_unlock();
    storage_sd_service_mutex_unlock();

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

    return ESP_OK;
}

esp_err_t storage_sd_service_read(
    FILE *file,
    void *buffer,
    size_t size,
    size_t *out_bytes_read
)
{
    if ((file == NULL) ||
        (buffer == NULL) ||
        (out_bytes_read == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *out_bytes_read = 0U;

    esp_err_t result =
        storage_sd_service_mutex_lock();

    if (result != ESP_OK) {
        return result;
    }

    result =
        storage_sd_service_validate_state_locked();

    if (result != ESP_OK) {
        storage_sd_service_mutex_unlock();
        return result;
    }

    if (size == 0U) {
        storage_sd_service_mutex_unlock();
        return ESP_OK;
    }

    result = storage_sd_service_spi_lock();

    if (result != ESP_OK) {
        storage_sd_service_mutex_unlock();
        return result;
    }

    errno = 0;
    clearerr(file);

    const size_t bytes_read = fread(
        buffer,
        1U,
        size,
        file
    );

    const bool read_error =
        ferror(file) != 0;

    const int saved_errno = errno;

    board_spi_unlock();
    storage_sd_service_mutex_unlock();

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
     * A short read at the end of the file is not an error.
     */
    return ESP_OK;
}

esp_err_t storage_sd_service_write(
    FILE *file,
    const void *buffer,
    size_t size,
    size_t *out_bytes_written
)
{
    if ((file == NULL) ||
        (buffer == NULL) ||
        (size == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (out_bytes_written != NULL) {
        *out_bytes_written = 0U;
    }

    esp_err_t result =
        storage_sd_service_mutex_lock();

    if (result != ESP_OK) {
        return result;
    }

    result =
        storage_sd_service_validate_state_locked();

    if (result != ESP_OK) {
        storage_sd_service_mutex_unlock();
        return result;
    }

    result = storage_sd_service_spi_lock();

    if (result != ESP_OK) {
        storage_sd_service_mutex_unlock();
        return result;
    }

    errno = 0;
    clearerr(file);

    const size_t bytes_written = fwrite(
        buffer,
        1U,
        size,
        file
    );

    const bool write_error =
        ferror(file) != 0;

    const int saved_errno = errno;

    board_spi_unlock();
    storage_sd_service_mutex_unlock();

    if (out_bytes_written != NULL) {
        *out_bytes_written = bytes_written;
    }

    if (write_error ||
        (bytes_written != size)) {

        ESP_LOGE(
            TAG,
            "Failed to write file: errno=%d (%s)",
            saved_errno,
            strerror(saved_errno)
        );

        return storage_sd_service_errno_to_error(
            saved_errno
        );
    }

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

    if ((origin != SEEK_SET) &&
        (origin != SEEK_CUR) &&
        (origin != SEEK_END)) {

        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        storage_sd_service_mutex_lock();

    if (result != ESP_OK) {
        return result;
    }

    result =
        storage_sd_service_validate_state_locked();

    if (result != ESP_OK) {
        storage_sd_service_mutex_unlock();
        return result;
    }

    result = storage_sd_service_spi_lock();

    if (result != ESP_OK) {
        storage_sd_service_mutex_unlock();
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
    storage_sd_service_mutex_unlock();

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
        storage_sd_service_mutex_lock();

    if (result != ESP_OK) {
        return result;
    }

    result =
        storage_sd_service_validate_state_locked();

    if (result != ESP_OK) {
        storage_sd_service_mutex_unlock();
        return result;
    }

    result = storage_sd_service_spi_lock();

    if (result != ESP_OK) {
        storage_sd_service_mutex_unlock();
        return result;
    }

    errno = 0;

    const int flush_result =
        fflush(file);

    const int saved_errno = errno;

    board_spi_unlock();
    storage_sd_service_mutex_unlock();

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

    esp_err_t result =
        storage_sd_service_mutex_lock();

    if (result != ESP_OK) {
        return result;
    }

    /*
     * Do not validate the mounted state here. Closing should still
     * be attempted when the card state has changed.
     */
    result = storage_sd_service_spi_lock();

    if (result != ESP_OK) {
        storage_sd_service_mutex_unlock();
        return result;
    }

    FILE *file_to_close = *file;

    errno = 0;

    const int close_result =
        fclose(file_to_close);

    const int saved_errno = errno;

    *file = NULL;

    if (s_open_file_count > 0U) {
        --s_open_file_count;
    } else {
        ESP_LOGE(
            TAG,
            "SD open file counter underflow"
        );
    }

    board_spi_unlock();
    storage_sd_service_mutex_unlock();

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
        storage_sd_service_mutex_lock();

    if (result != ESP_OK) {
        return result;
    }

    result =
        storage_sd_service_validate_state_locked();

    if (result != ESP_OK) {
        storage_sd_service_mutex_unlock();
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
        storage_sd_service_mutex_unlock();
        return result;
    }

    result = storage_sd_service_spi_lock();

    if (result != ESP_OK) {
        storage_sd_service_mutex_unlock();
        return result;
    }

    errno = 0;

    const int remove_result =
        remove(full_path);

    const int saved_errno = errno;

    board_spi_unlock();
    storage_sd_service_mutex_unlock();

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
    if ((old_path == NULL) ||
        (new_path == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        storage_sd_service_mutex_lock();

    if (result != ESP_OK) {
        return result;
    }

    result =
        storage_sd_service_validate_state_locked();

    if (result != ESP_OK) {
        storage_sd_service_mutex_unlock();
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

    if (result == ESP_OK) {
        result = storage_sd_service_build_path(
            new_path,
            new_full_path,
            sizeof(new_full_path)
        );
    }

    if (result != ESP_OK) {
        storage_sd_service_mutex_unlock();
        return result;
    }

    result = storage_sd_service_spi_lock();

    if (result != ESP_OK) {
        storage_sd_service_mutex_unlock();
        return result;
    }

    errno = 0;

    const int rename_result = rename(
        old_full_path,
        new_full_path
    );

    const int saved_errno = errno;

    board_spi_unlock();
    storage_sd_service_mutex_unlock();

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
    if ((path == NULL) ||
        (out_stat == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    memset(
        out_stat,
        0,
        sizeof(*out_stat)
    );

    esp_err_t result =
        storage_sd_service_mutex_lock();

    if (result != ESP_OK) {
        return result;
    }

    result =
        storage_sd_service_validate_state_locked();

    if (result != ESP_OK) {
        storage_sd_service_mutex_unlock();
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
        storage_sd_service_mutex_unlock();
        return result;
    }

    result = storage_sd_service_spi_lock();

    if (result != ESP_OK) {
        storage_sd_service_mutex_unlock();
        return result;
    }

    errno = 0;

    const int stat_result = stat(
        full_path,
        out_stat
    );

    const int saved_errno = errno;

    board_spi_unlock();
    storage_sd_service_mutex_unlock();

    if (stat_result != 0) {
        /*
         * Do not expose partially written stat data on failure.
         */
        memset(
            out_stat,
            0,
            sizeof(*out_stat)
        );

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

static void storage_sd_service_set_state_locked(
    storage_sd_state_t state
)
{
    const bool state_changed =
        s_state != state;

    s_state = state;

    const bool mounted =
        state == STORAGE_SD_STATE_MOUNTED;

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

    if (state_changed) {
        ESP_LOGI(
            TAG,
            "Storage SD state changed to %d",
            (int)state
        );
    }
}

esp_err_t storage_sd_service_get_state(
    storage_sd_state_t *state
)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *state = STORAGE_SD_STATE_UNAVAILABLE;

    const esp_err_t result =
        storage_sd_service_mutex_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!s_started) {
        storage_sd_service_mutex_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    *state = s_state;

    storage_sd_service_mutex_unlock();

    return ESP_OK;
}

esp_err_t storage_sd_service_mount(void)
{
    const esp_err_t lock_result =
        storage_sd_service_mutex_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    if (!s_started) {
        storage_sd_service_mutex_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result =
        sd_card_driver_mount();

    if (result == ESP_OK) {
        storage_sd_service_set_state_locked(
            STORAGE_SD_STATE_MOUNTED
        );
    } else {
        storage_sd_service_set_state_locked(
            STORAGE_SD_STATE_UNAVAILABLE
        );
    }

    storage_sd_service_mutex_unlock();

    if (result != ESP_OK) {
        return result;
    }

    app_settings_t settings;

    const esp_err_t settings_result =
        settings_model_get(
            &settings
        );

    if (settings_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to read logging settings: %s",
            esp_err_to_name(settings_result)
        );
    } else if (settings.logging.sd_enabled) {
        const esp_err_t logging_result =
            logging_service_enable_file();

        if (logging_result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Failed to enable SD logging: %s",
                esp_err_to_name(logging_result)
            );
        }
    }

    return ESP_OK;
}

esp_err_t storage_sd_service_unmount(void)
{
    storage_sd_state_t current_state;

    const esp_err_t state_result =
        storage_sd_service_get_state(
            &current_state
        );

    if (state_result != ESP_OK) {
        return state_result;
    }

    (void)current_state;

    /*
     * Logging service may call storage_sd_service_flush/close,
     * so it must be disabled before taking s_mutex.
     */
    const esp_err_t logging_result =
        logging_service_disable_file();

    if (logging_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to disable SD logging: %s",
            esp_err_to_name(logging_result)
        );

        return logging_result;
    }

    const esp_err_t lock_result =
        storage_sd_service_mutex_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    if (!s_started) {
        storage_sd_service_mutex_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    if (s_open_file_count != 0U) {
        ESP_LOGW(
            TAG,
            "Cannot unmount SD card: %u file(s) are still open",
            (unsigned int)s_open_file_count
        );

        storage_sd_service_mutex_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result =
        sd_card_driver_unmount();

    if (result == ESP_OK) {
        storage_sd_service_set_state_locked(
            STORAGE_SD_STATE_UNAVAILABLE
        );
    } else {
        storage_sd_service_set_state_locked(
            STORAGE_SD_STATE_MOUNTED
        );
    }

    storage_sd_service_mutex_unlock();

    return result;
}

esp_err_t storage_sd_service_start(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();

        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    const esp_err_t lock_result =
        storage_sd_service_mutex_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    if (s_started) {
        storage_sd_service_mutex_unlock();
        return ESP_OK;
    }

    const esp_err_t init_result =
        sd_card_driver_init();

    if (init_result != ESP_OK) {
        storage_sd_service_mutex_unlock();
        return init_result;
    }

    s_started = true;

    storage_sd_service_mutex_unlock();

    /*
     * Absence of a card during startup is not fatal.
     */
    const esp_err_t mount_result =
        storage_sd_service_mount();

    if (mount_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Initial SD card mount failed: %s",
            esp_err_to_name(mount_result)
        );
    }

    return ESP_OK;
}
