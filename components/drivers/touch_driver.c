#include "touch_driver.h"

#include "driver/i2c_master.h"

#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"

#include "board.h"
#include "board_config.h"

static const char *TAG =
    "touch_driver";

static esp_lcd_touch_io_gt911_config_t
    s_gt911_config = {
        .dev_addr =
            ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
    };

static esp_lcd_panel_io_handle_t s_touch_io = NULL;
static esp_lcd_touch_handle_t s_touch = NULL;

static bool s_initialized = false;

static esp_err_t touch_driver_cleanup(void)
{
    if (s_touch != NULL) {
        const esp_err_t result =
            esp_lcd_touch_del(
                s_touch
            );

        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Failed to delete GT911 controller: %s",
                esp_err_to_name(result)
            );

            return result;
        }

        s_touch = NULL;
    }

    if (s_touch_io != NULL) {
        const esp_err_t result =
            esp_lcd_panel_io_del(
                s_touch_io
            );

        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Failed to delete GT911 panel IO: %s",
                esp_err_to_name(result)
            );

            return result;
        }

        s_touch_io = NULL;
    }

    /*
     * The shared I2C bus is owned by the board module and must not be
     * deleted by an individual device driver.
     */
    s_initialized = false;

    return ESP_OK;
}

static esp_err_t touch_panel_io_init(void)
{
    i2c_master_bus_handle_t i2c_bus =
        board_i2c_get_handle();

    if (i2c_bus == NULL) {
        ESP_LOGE(
            TAG,
            "Shared I2C bus is not initialized"
        );

        return ESP_ERR_INVALID_STATE;
    }

    esp_lcd_panel_io_i2c_config_t io_config =
        ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();

    /*
     * Set the device clock speed explicitly. The shared bus may contain
     * devices with different clock-speed limits.
     */
    io_config.scl_speed_hz =
        TOUCH_I2C_FREQ_HZ;

    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_i2c(
            i2c_bus,
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
    const esp_lcd_touch_config_t touch_config = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,

        .rst_gpio_num = TOUCH_PIN_RST,
        .int_gpio_num = TOUCH_PIN_INT,

        .levels = {
            .reset = 0U,
            .interrupt = 0U,
        },

        .flags = {
            .swap_xy = TOUCH_SWAP_XY,
            .mirror_x = TOUCH_MIRROR_X,
            .mirror_y = TOUCH_MIRROR_Y,
        },

        .driver_data =
            &s_gt911_config,
    };

    return esp_lcd_touch_new_i2c_gt911(
        s_touch_io,
        &touch_config,
        &s_touch
    );
}

esp_err_t touch_driver_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    if (!board_i2c_is_initialized()) {
        ESP_LOGE(
            TAG,
            "Cannot initialize GT911 before the board I2C bus"
        );

        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Clean resources that may remain after an earlier failed
     * initialization attempt.
     */
    if ((s_touch != NULL) ||
        (s_touch_io != NULL)) {

        const esp_err_t cleanup_result =
            touch_driver_cleanup();

        if (cleanup_result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Failed to clean previous GT911 state: %s",
                esp_err_to_name(cleanup_result)
            );

            return cleanup_result;
        }
    }

    ESP_LOGI(
        TAG,
        "Initializing GT911"
    );

    esp_err_t result =
        touch_panel_io_init();

    if (result == ESP_OK) {
        result =
            touch_controller_init();
    }

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "GT911 initialization failed: %s",
            esp_err_to_name(result)
        );

        const esp_err_t cleanup_result =
            touch_driver_cleanup();

        if (cleanup_result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "GT911 cleanup failed: %s",
                esp_err_to_name(cleanup_result)
            );
        }

        return result;
    }

    s_initialized = true;

    ESP_LOGI(
        TAG,
        "GT911 initialized"
    );

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
    if ((x == NULL) ||
        (y == NULL) ||
        (pressed == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *x = 0U;
    *y = 0U;
    *pressed = false;

    if (!s_initialized ||
        (s_touch == NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(
        esp_lcd_touch_read_data(
            s_touch
        ),
        TAG,
        "Failed to read GT911"
    );

    esp_lcd_touch_point_data_t point[1] = {0};
    uint8_t point_count = 0U;

    ESP_RETURN_ON_ERROR(
        esp_lcd_touch_get_data(
            s_touch,
            point,
            &point_count,
            1U
        ),
        TAG,
        "Failed to get touch point"
    );

    if (point_count > 0U) {
        *x = point[0].x;
        *y = point[0].y;
        *pressed = true;
    }

    return ESP_OK;
}
