#include "board.h"

#include "board_config.h"
#include "display_backlight.h"

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "board";

static SemaphoreHandle_t s_bus_semaphore = NULL;

esp_err_t board_init(void)
{
    ESP_ERROR_CHECK(
        display_backlight_init()
    );

    ESP_ERROR_CHECK(
        display_backlight_set_brightness(
            0
        )
    );

    board_spi_init();

    return ESP_OK;
}

esp_err_t board_spi_init(void)
{
    if (s_bus_semaphore != NULL) {
        return ESP_OK;
    }

    s_bus_semaphore =
        xSemaphoreCreateBinary();

    if (s_bus_semaphore == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create SPI bus semaphore"
        );

        return ESP_ERR_NO_MEM;
    }

    /*
     * The semaphore is initially available.
     */
    xSemaphoreGive(
        s_bus_semaphore
    );

    /*
     * Initialize the shared SPI bus here.
     */

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
            LCD_DRAW_BUFFER_LINES *
            sizeof(uint16_t),
    };

    const esp_err_t result = spi_bus_initialize(
            LCD_SD_SPI_HOST,
            &bus_config,
            SPI_DMA_CH_AUTO
    );

    ESP_RETURN_ON_ERROR(
        result,
        TAG,
        "Failed to initialize shared SPI bus"
    );

    if (result != ESP_OK) {
        vSemaphoreDelete(
            s_bus_semaphore
        );

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

    xSemaphoreGive(
        s_bus_semaphore
    );
}

void board_spi_unlock_from_isr(
    BaseType_t *higher_priority_task_woken
)
{
    if (s_bus_semaphore == NULL) {
        return;
    }

    xSemaphoreGiveFromISR(
        s_bus_semaphore,
        higher_priority_task_woken
    );
}
