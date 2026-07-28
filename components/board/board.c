#include "board.h"

#include "esp_log.h"

#include "app_config.h"
#include "board_config.h"
#include "display_backlight.h"

static const char *TAG = "board";

static SemaphoreHandle_t s_bus_semaphore = NULL;

esp_err_t board_init(void)
{
    esp_err_t result = display_backlight_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize display backlight: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result = display_backlight_set_brightness(0U);

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to turn off display backlight: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result = board_spi_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize shared SPI bus: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    return ESP_OK;
}

esp_err_t board_spi_init(void)
{
    if (s_bus_semaphore != NULL) {
        return ESP_OK;
    }

    s_bus_semaphore = xSemaphoreCreateBinary();

    if (s_bus_semaphore == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create SPI bus semaphore"
        );

        return ESP_ERR_NO_MEM;
    }

    if (xSemaphoreGive(s_bus_semaphore) != pdTRUE) {
        ESP_LOGE(
            TAG,
            "Failed to initialize SPI bus semaphore"
        );

        vSemaphoreDelete(s_bus_semaphore);
        s_bus_semaphore = NULL;

        return ESP_FAIL;
    }

    const spi_bus_config_t bus_config = {
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = LCD_PIN_MISO,
        .sclk_io_num = LCD_PIN_SCLK,

        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,

        /*
         * Must be large enough for the largest transaction
         * performed by any device on this SPI bus.
         *
         * The LCD flush buffer is currently the largest one.
         */
        .max_transfer_sz =
            LCD_H_RES *
            LVGL_DRAW_BUFFER_LINES *
            sizeof(uint16_t),
    };

    const esp_err_t result = spi_bus_initialize(
        LCD_SD_SPI_HOST,
        &bus_config,
        SPI_DMA_CH_AUTO
    );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize shared SPI bus: %s",
            esp_err_to_name(result)
        );

        vSemaphoreDelete(s_bus_semaphore);
        s_bus_semaphore = NULL;

        return result;
    }

    ESP_LOGI(
        TAG,
        "Shared SPI bus initialized"
    );

    return ESP_OK;
}

bool board_spi_is_initialized(void)
{
    return s_bus_semaphore != NULL;
}

spi_host_device_t board_spi_get_host(void)
{
    return LCD_SD_SPI_HOST;
}

bool board_spi_lock(
    TickType_t timeout
)
{
    if (s_bus_semaphore == NULL) {
        return false;
    }

    return xSemaphoreTake(
        s_bus_semaphore,
        timeout
    ) == pdTRUE;
}

void board_spi_unlock(void)
{
    if (s_bus_semaphore == NULL) {
        return;
    }

    if (xSemaphoreGive(s_bus_semaphore) != pdTRUE) {
        ESP_LOGW(
            TAG,
            "SPI bus semaphore is already available"
        );
    }
}

void board_spi_unlock_from_isr(
    BaseType_t *higher_priority_task_woken
)
{
    if (s_bus_semaphore == NULL) {
        return;
    }

    (void)xSemaphoreGiveFromISR(
        s_bus_semaphore,
        higher_priority_task_woken
    );
}
