#include <stdbool.h>
#include "touch_driver.h"
#include "board_config.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"

static const char *TAG = "touch_driver";

static i2c_master_bus_handle_t s_i2c_bus;
static esp_lcd_panel_io_handle_t s_touch_io;
static esp_lcd_touch_handle_t s_touch;

static bool s_initialized = false;

static esp_err_t touch_i2c_init(void)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = TOUCH_I2C_PORT,
        .sda_io_num = TOUCH_PIN_SDA,
        .scl_io_num = TOUCH_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,

        /*
        * Internal pull-up resistors can be useful for testing,
        * but external pull-up resistors (typically 2.2–4.7 kΩ)
        * are recommended for production hardware.
        */
        .flags.enable_internal_pullup = true,
    };

    ESP_RETURN_ON_ERROR(
        i2c_new_master_bus(
            &bus_config,
            &s_i2c_bus
        ),
        TAG,
        "Failed to create I2C bus"
    );

    return ESP_OK;
}

static esp_err_t touch_panel_io_init(void)
{
    esp_lcd_panel_io_i2c_config_t io_config =
        ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();

    /*
    * Set the I2C clock speed explicitly if needed.
    * The scl_speed_hz field depends on the ESP-IDF version.
    */
    io_config.scl_speed_hz = TOUCH_I2C_FREQ_HZ;

    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_i2c(
            s_i2c_bus,
            &io_config,
            &s_touch_io
        ),
        TAG,
        "Failed to create GT911 panel IO"
    );

    return ESP_OK;
}

static esp_err_t touch_controller_init(void)
{
    /*
    * In newer versions of the driver,
    * the context is passed through driver_data.
    */
    esp_lcd_panel_io_i2c_config_t default_io =
        ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();

    esp_lcd_touch_io_gt911_config_t gt911_config = {
        .dev_addr = default_io.dev_addr,
    };

    const esp_lcd_touch_config_t touch_config = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,

        .rst_gpio_num = TOUCH_PIN_RST,
        .int_gpio_num = TOUCH_PIN_INT,

        .levels = {
            .reset = 0,
            .interrupt = 0,
        },

        .flags = {
            .swap_xy = TOUCH_SWAP_XY,
            .mirror_x = TOUCH_MIRROR_X,
            .mirror_y = TOUCH_MIRROR_Y,
        },

        .driver_data = &gt911_config,
    };

    ESP_RETURN_ON_ERROR(
        esp_lcd_touch_new_i2c_gt911(
            s_touch_io,
            &touch_config,
            &s_touch
        ),
        TAG,
        "Failed to initialize GT911"
    );

    return ESP_OK;
}

esp_err_t touch_driver_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing GT911");

    ESP_RETURN_ON_ERROR(
        touch_i2c_init(),
        TAG,
        "I2C init failed"
    );

    ESP_RETURN_ON_ERROR(
        touch_panel_io_init(),
        TAG,
        "Touch IO init failed"
    );

    ESP_RETURN_ON_ERROR(
        touch_controller_init(),
        TAG,
        "GT911 init failed"
    );

    s_initialized = true;

    ESP_LOGI(TAG, "GT911 initialized");

    return ESP_OK;
}

esp_lcd_touch_handle_t touch_driver_get_handle(void)
{
    return s_touch;
}


esp_err_t touch_driver_read(
    uint16_t *x,
    uint16_t *y,
    bool *pressed
)
{
    if (
        x == NULL ||
        y == NULL ||
        pressed == NULL
    ) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_touch == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(
        esp_lcd_touch_read_data(s_touch),
        TAG,
        "Failed to read GT911"
    );

    esp_lcd_touch_point_data_t point[1];
    uint8_t point_count = 0;

    ESP_RETURN_ON_ERROR(
        esp_lcd_touch_get_data(
            s_touch,
            point,
            &point_count,
            1
        ),
        TAG,
        "Failed to get touch point"
    );

    if (point_count > 0) {
        *x = point[0].x;
        *y = point[0].y;
        *pressed = true;
    } else {
        *pressed = false;
    }

    return ESP_OK;
}
