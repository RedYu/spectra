#include "display_driver.h"
#include "board_config.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

#include "esp_lcd_ili9488.h"

static const char *TAG = "display_driver";

static esp_lcd_panel_io_handle_t s_panel_io = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;

static bool s_spi_bus_initialized = false;
static bool s_display_initialized = false;


static esp_err_t display_backlight_init(void)
{
    const gpio_config_t backlight_config = {
        .pin_bit_mask = 1ULL << LCD_PIN_BACKLIGHT,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(
        gpio_config(&backlight_config),
        TAG,
        "Failed to configure backlight GPIO"
    );

    return gpio_set_level(
        LCD_PIN_BACKLIGHT,
        LCD_BACKLIGHT_OFF_LEVEL
    );
}


esp_err_t display_driver_set_backlight(bool enabled)
{
    return gpio_set_level(
        LCD_PIN_BACKLIGHT,
        enabled
            ? LCD_BACKLIGHT_ON_LEVEL
            : LCD_BACKLIGHT_OFF_LEVEL
    );
}


static esp_err_t display_spi_bus_init(void)
{
    if (s_spi_bus_initialized) {
        return ESP_OK;
    }

    const spi_bus_config_t bus_config = {
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = LCD_PIN_MISO,
        .sclk_io_num = LCD_PIN_SCLK,

        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,

        .max_transfer_sz = LCD_H_RES * LCD_DRAW_BUFFER_LINES * 3,
    };

    ESP_RETURN_ON_ERROR(
        spi_bus_initialize(
            LCD_SPI_HOST,
            &bus_config,
            SPI_DMA_CH_AUTO
        ),
        TAG,
        "Failed to initialize LCD SPI bus"
    );

    s_spi_bus_initialized = true;

    ESP_LOGI(TAG, "SPI bus initialized");

    return ESP_OK;
}


static esp_err_t display_panel_io_init(void)
{
    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_PIN_CS,
        .dc_gpio_num = LCD_PIN_DC,

        .spi_mode = 0,
        .pclk_hz = LCD_SPI_CLOCK_HZ,

        .trans_queue_depth = 10,

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
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &s_panel_io),
        TAG,
        "Failed to create LCD panel IO"
    );

    ESP_LOGI(TAG, "LCD panel IO created");

    return ESP_OK;
}


static esp_err_t display_panel_init(void)
{
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,

        .bits_per_pixel = 18,

        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
    };

    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_ili9488(s_panel_io, &panel_config, LV_BUFFER_SIZE, &s_panel),
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
        ESP_LOGW(TAG, "Display is already initialized");
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(
        display_backlight_init(),
        TAG,
        "Backlight initialization failed"
    );

    ESP_RETURN_ON_ERROR(
        display_spi_bus_init(),
        TAG,
        "SPI bus initialization failed"
    );

    ESP_RETURN_ON_ERROR(
        display_panel_io_init(),
        TAG,
        "Panel IO initialization failed"
    );

    ESP_RETURN_ON_ERROR(
        display_panel_init(),
        TAG,
        "ILI9488 initialization failed"
    );

    /*
    * Enable the backlight here for the initial display test.
    *
    * Once LVGL is integrated, it is recommended to enable the
    * backlight after the first screen has been created and the
    * initial frame has been rendered.
    */
    ESP_RETURN_ON_ERROR(
        display_driver_set_backlight(true),
        TAG,
        "Failed to enable backlight"
    );

    s_display_initialized = true;

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
