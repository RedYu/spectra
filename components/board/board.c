/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "board.h"

#include "esp_log.h"

#include "driver/i2c_master.h"
#include "driver/gpio.h"

#include "app_config.h"
#include "board_config.h"
#include "display_backlight.h"

static const char *TAG = "board";

static SemaphoreHandle_t s_bus_semaphore = NULL;

static i2c_master_bus_handle_t s_i2c_bus = NULL;

static bool s_gpio_isr_service_initialized = false;

esp_err_t board_init(void)
{
    esp_err_t result =
        board_gpio_isr_service_init();

    if (result != ESP_OK) {
        return result;
    }

    result =
        display_backlight_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize display backlight: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        display_backlight_set_brightness(
            0U
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to turn off display backlight: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        board_spi_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize shared SPI bus: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        board_i2c_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize shared I2C bus: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    ESP_LOGI(
        TAG,
        "Board initialized"
    );

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

esp_err_t board_i2c_init(void)
{
    if (s_i2c_bus != NULL) {
        return ESP_OK;
    }

    const i2c_master_bus_config_t config = {
        .i2c_port = TOUCH_I2C_PORT,
        .sda_io_num = TOUCH_PIN_SDA,
        .scl_io_num = TOUCH_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7U,

        /*
         * External pull-up resistors are recommended for a shared
         * production I2C bus.
         */
        .flags.enable_internal_pullup = true,
    };

    const esp_err_t result =
        i2c_new_master_bus(
            &config,
            &s_i2c_bus
        );

    if (result != ESP_OK) {
        s_i2c_bus = NULL;

        ESP_LOGE(
            TAG,
            "Failed to initialize shared I2C bus: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    ESP_LOGI(
        TAG,
        "Shared I2C bus initialized"
    );

    return ESP_OK;
}

bool board_i2c_is_initialized(void)
{
    return s_i2c_bus != NULL;
}

i2c_master_bus_handle_t board_i2c_get_handle(void)
{
    return s_i2c_bus;
}

esp_err_t board_gpio_isr_service_init(void)
{
    if (s_gpio_isr_service_initialized) {
        return ESP_OK;
    }

    /*
     * This service is shared by all GPIO interrupt users. Keep the
     * allocation flags at zero until every registered ISR handler has
     * been audited for complete IRAM safety.
     */
    const esp_err_t result =
        gpio_install_isr_service(
            0
        );

    if ((result != ESP_OK) &&
        (result != ESP_ERR_INVALID_STATE)) {

        ESP_LOGE(
            TAG,
            "Failed to install shared GPIO ISR service: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    /*
     * ESP_ERR_INVALID_STATE means that another component installed the
     * global service earlier. It is still available for shared use.
     */
    s_gpio_isr_service_initialized = true;

    if (result == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(
            TAG,
            "Shared GPIO ISR service was already initialized"
        );
    } else {
        ESP_LOGI(
            TAG,
            "Shared GPIO ISR service initialized"
        );
    }

    return ESP_OK;
}

bool board_gpio_isr_service_is_initialized(void)
{
    return s_gpio_isr_service_initialized;
}
