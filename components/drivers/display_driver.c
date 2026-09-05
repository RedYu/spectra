/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "display_driver.h"

#include <stdbool.h>


#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

#include "esp_lcd_ili9488.h"

#include "app_config.h"
#include "board.h"
#include "board_config.h"

static const char *TAG = "display_driver";

static esp_lcd_panel_io_handle_t s_panel_io = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;

static bool s_display_initialized = false;

static esp_err_t display_panel_io_init(void)
{
    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_PIN_CS,
        .dc_gpio_num = LCD_PIN_DC,

        .spi_mode = 0,
        .pclk_hz = LCD_SPI_CLOCK_HZ,

        .trans_queue_depth = 1,

        /*
         * The ILI9488 uses 8-bit commands
         * and 8-bit command parameters.
         */
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,

        /*
         * The flush completion callback
         * will be registered here later.
         */
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
    };

    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi(
            (esp_lcd_spi_bus_handle_t)board_spi_get_host(),
            &io_config,
            &s_panel_io
        ),
        TAG,
        "Failed to create LCD panel IO"
    );

    ESP_LOGI(TAG, "LCD panel IO created");

    return ESP_OK;
}

static void display_driver_cleanup(void)
{
    if (s_panel != NULL) {
        (void)esp_lcd_panel_del(s_panel);
        s_panel = NULL;
    }

    if (s_panel_io != NULL) {
        (void)esp_lcd_panel_io_del(s_panel_io);
        s_panel_io = NULL;
    }

    s_display_initialized = false;
}

static esp_err_t display_panel_init(void)
{
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,

        .bits_per_pixel = 18,

        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
    };

    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_ili9488(
            s_panel_io,
            &panel_config,
            LVGL_DRAW_BUFFER_PIXELS,
            &s_panel
        ),
        TAG,
        "Failed to create ILI9488 panel"
    );

    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_reset(s_panel),
        TAG,
        "Failed to reset ILI9488"
    );

    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_init(s_panel),
        TAG,
        "Failed to initialize ILI9488"
    );

    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_invert_color(s_panel, false),
        TAG,
        "Failed to set color inversion"
    );

    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_swap_xy(s_panel, LCD_SWAP_XY),
        TAG,
        "Failed to set pixel order"
    );

    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_mirror(s_panel, LCD_MIRROR_X, LCD_MIRROR_Y),
        TAG,
        "Failed to set panel mirror"
    );

    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_set_gap(s_panel, 0, 0),
        TAG,
        "Failed to set panel gap"
    );

    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_disp_on_off(s_panel, true),
        TAG,
        "Failed to turn on ILI9488"
    );

    ESP_LOGI(
        TAG,
        "ILI9488 initialized, resolution %dx%d",
        LCD_H_RES,
        LCD_V_RES
    );

    return ESP_OK;
}


esp_err_t display_driver_init(void)
{
    if (s_display_initialized) {
        ESP_LOGW(
            TAG,
            "Display is already initialized"
        );

        return ESP_OK;
    }

    if (!board_spi_is_initialized()) {
        ESP_LOGE(
            TAG,
            "Shared SPI bus is not initialized"
        );

        return ESP_ERR_INVALID_STATE;
    }

    if (!board_spi_lock(portMAX_DELAY)) {
        ESP_LOGE(
            TAG,
            "Failed to lock shared SPI bus"
        );

        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = display_panel_io_init();

    if (result == ESP_OK) {
        result = display_panel_init();
    }

    board_spi_unlock();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Display initialization failed: %s",
            esp_err_to_name(result)
        );

        display_driver_cleanup();

        return result;
    }

    s_display_initialized = true;

    ESP_LOGI(
        TAG,
        "Display driver initialized"
    );

    return ESP_OK;
}


esp_lcd_panel_handle_t display_driver_get_panel(void)
{
    return s_panel;
}


esp_lcd_panel_io_handle_t display_driver_get_panel_io(void)
{
    return s_panel_io;
}
