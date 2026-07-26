#include "sd_card_driver.h"

#include "board_config.h"
#include "board.h"

#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_log.h"

static const char *TAG = "sd_card_driver";

static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;

static SemaphoreHandle_t s_mutex = NULL;

#define SD_CARD_PROBE_SECTOR 0

DMA_ATTR static uint8_t s_probe_buffer[512];

esp_err_t sd_card_driver_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();

        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

esp_err_t sd_card_driver_mount(void)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_mutex,
            pdMS_TO_TICKS(1000)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    if (s_mounted) {
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    host.slot = board_spi_get_host();
    host.max_freq_khz = LCD_SD_SPI_CLOCK_HZ;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();

    slot_config.host_id = board_spi_get_host();
    slot_config.gpio_cs = SD_PIN_CS;
    slot_config.gpio_cd = SDSPI_SLOT_NO_CD;
    slot_config.gpio_wp = SDSPI_SLOT_NO_WP;

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };

    if (!board_spi_lock(
            pdMS_TO_TICKS(3000)
        )) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_TIMEOUT;
    }

    const esp_err_t result =
        esp_vfs_fat_sdspi_mount(
            SD_CARD_MOUNT_POINT,
            &host,
            &slot_config,
            &mount_config,
            &s_card
        );

    if (result == ESP_OK) {
        s_mounted = true;

        ESP_LOGI(
            TAG,
            "SD card mounted"
        );

        sdmmc_card_print_info(
            stdout,
            s_card
        );
    } 
    else {
        s_card = NULL;
        s_mounted = false;

        ESP_LOGW(
            TAG,
            "SD card mount failed: %s",
            esp_err_to_name(result)
        );
    }

    board_spi_unlock();

    xSemaphoreGive(s_mutex);

    return result;
}

esp_err_t sd_card_driver_check(void)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_mutex,
            pdMS_TO_TICKS(500)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    if (!s_mounted || s_card == NULL) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    if (!board_spi_lock(
            pdMS_TO_TICKS(2000)
        )) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_TIMEOUT;
    }

    const esp_err_t result =
        sdmmc_read_sectors(
            s_card,
            s_probe_buffer,
            SD_CARD_PROBE_SECTOR,
            1
        );

    board_spi_unlock();

    xSemaphoreGive(s_mutex);

    return result;
}

esp_err_t sd_card_driver_unmount(void)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_mutex,
            pdMS_TO_TICKS(1000)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    if (!s_mounted) {
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    if (!board_spi_lock(
            pdMS_TO_TICKS(3000)
        )) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_TIMEOUT;
    }

    const esp_err_t result =
        esp_vfs_fat_sdcard_unmount(
            SD_CARD_MOUNT_POINT,
            s_card
        );

    board_spi_unlock();

    s_card = NULL;
    s_mounted = false;

    ESP_LOGI(
        TAG,
        "SD card unmounted"
    );

    xSemaphoreGive(s_mutex);

    return result;
}

bool sd_card_driver_is_mounted(void)
{
    return s_mounted;
}

bool sd_card_get_info_text(
    char *buffer,
    size_t buffer_size
)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_mutex,
            pdMS_TO_TICKS(1000)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    if (!s_mounted) {
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    if (s_card == NULL ||
        buffer == NULL ||
        buffer_size == 0) {

        xSemaphoreGive(s_mutex);
        return false;
    }

    memset(
        buffer,
        0,
        buffer_size
    );

    FILE *stream =
        fmemopen(
            buffer,
            buffer_size - 1,
            "w"
        );

    if (stream == NULL) {
        return false;
    }

    sdmmc_card_print_info(
        stream,
        s_card
    );

    fflush(stream);
    fclose(stream);

    buffer[buffer_size - 1] = '\0';

    xSemaphoreGive(s_mutex);

    return true;
}