#include "lvgl_port.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_config.h"
#include "board.h"
#include "board_config.h"
#include "display_driver.h"
#include "touch_driver.h"

#define LVGL_SPI_LOCK_TIMEOUT_MS  (5000U)

static const char *TAG = "lvgl_port";

static lv_display_t *s_display = NULL;
static lv_indev_t *s_touch_indev = NULL;

static lv_color_t *s_draw_buffer_1 = NULL;
static lv_color_t *s_draw_buffer_2 = NULL;

static esp_timer_handle_t s_tick_timer = NULL;

static bool s_initialized = false;
static bool s_lvgl_initialized = false;

static void lvgl_tick_timer_cb(void *arg)
{
    (void)arg;

    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static esp_err_t lvgl_tick_init(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = lvgl_tick_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lvgl_tick",
        .skip_unhandled_events = true,
    };

    esp_err_t result = esp_timer_create(
        &timer_args,
        &s_tick_timer
    );

    if (result != ESP_OK) {
        return result;
    }

    result = esp_timer_start_periodic(
        s_tick_timer,
        (uint64_t)LVGL_TICK_PERIOD_MS * 1000ULL
    );

    if (result != ESP_OK) {
        (void)esp_timer_delete(s_tick_timer);
        s_tick_timer = NULL;

        return result;
    }

    return ESP_OK;
}

static void lvgl_display_flush_cb(
    lv_display_t *display,
    const lv_area_t *area,
    uint8_t *pixel_map
)
{
    if (!board_spi_lock(
            pdMS_TO_TICKS(LVGL_SPI_LOCK_TIMEOUT_MS)
        )) {

        ESP_LOGE(
            TAG,
            "Failed to lock SPI bus for LCD flush"
        );

        lv_display_flush_ready(
            display
        );

        return;
    }

    esp_lcd_panel_handle_t panel =
        display_driver_get_panel();

    if (panel == NULL) {
        board_spi_unlock();
        lv_display_flush_ready(display);
        return;
    }

    /*
     * LVGL uses inclusive end coordinates.
     * esp_lcd uses exclusive end coordinates.
     */
    esp_err_t err = esp_lcd_panel_draw_bitmap(
        panel,
        area->x1,
        area->y1,
        area->x2 + 1,
        area->y2 + 1,
        pixel_map
    );

    if (err != ESP_OK) {
        board_spi_unlock();

        ESP_LOGE(
            TAG,
            "Display flush failed: %s",
            esp_err_to_name(err)
        );

        /*
         * No transfer-complete callback will be generated
         * if the transfer was not queued successfully.
         */
        lv_display_flush_ready(display);
        return;
    }

    /*
     * lv_display_flush_ready() will be called from
     * lvgl_color_transfer_done_cb() after DMA completes.
     */
}

static bool lvgl_color_transfer_done_cb(
    esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_io_event_data_t *event_data,
    void *user_ctx
)
{
    (void)panel_io;
    (void)event_data;

    BaseType_t higher_priority_task_woken = pdFALSE;

    board_spi_unlock_from_isr(
        &higher_priority_task_woken
    );

    lv_display_t *display = user_ctx;

    if (display != NULL) {
        lv_display_flush_ready(display);
    }

    return higher_priority_task_woken == pdTRUE;
}

static esp_err_t lvgl_display_register(void)
{
    esp_lcd_panel_io_handle_t panel_io =
        display_driver_get_panel_io();

    if (panel_io == NULL) {
        ESP_LOGE(TAG, "Display panel IO is NULL");
        return ESP_ERR_INVALID_STATE;
    }

    s_display = lv_display_create(
        LCD_H_RES,
        LCD_V_RES
    );

    if (s_display == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL display");
        return ESP_ERR_NO_MEM;
    }

    lv_display_set_color_format(
        s_display,
        LV_COLOR_FORMAT_RGB565
    );

    const uint32_t bytes_per_pixel =
        lv_color_format_get_size(
            LV_COLOR_FORMAT_RGB565
        );

    const size_t pixel_count =
        (size_t)LCD_H_RES *
        (size_t)LVGL_DRAW_BUFFER_LINES;

    const size_t buffer_size =
        pixel_count *
        (size_t)bytes_per_pixel;

    s_draw_buffer_1 = heap_caps_malloc(
        buffer_size,
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL
    );

    s_draw_buffer_2 = heap_caps_malloc(
        buffer_size, 
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL
    );

    if ((s_draw_buffer_1 == NULL) ||
        (s_draw_buffer_2 == NULL)) {

        ESP_LOGE(
            TAG,
            "Failed to allocate LVGL draw buffers"
        );

        if (s_draw_buffer_1 != NULL) {
            heap_caps_free(s_draw_buffer_1);
            s_draw_buffer_1 = NULL;
        }

        if (s_draw_buffer_2 != NULL) {
            heap_caps_free(s_draw_buffer_2);
            s_draw_buffer_2 = NULL;
        }

        lv_display_delete(s_display);
        s_display = NULL;

        return ESP_ERR_NO_MEM;
    }

    /*
     * In LVGL 9, the buffer size is specified in bytes.
     */
    lv_display_set_buffers(
        s_display,
        s_draw_buffer_1,
        s_draw_buffer_2,
        buffer_size,
        LV_DISPLAY_RENDER_MODE_PARTIAL
    );

    lv_display_set_flush_cb(
        s_display,
        lvgl_display_flush_cb
    );

    /*
     * This callback is invoked when the SPI DMA transfer completes.
     * It notifies LVGL that the draw buffer can be reused.
     */
    const esp_lcd_panel_io_callbacks_t io_callbacks = {
        .on_color_trans_done =
            lvgl_color_transfer_done_cb,
    };

    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_io_register_event_callbacks(
            panel_io,
            &io_callbacks,
            s_display
        ),
        TAG,
        "Failed to register LCD transfer callback"
    );

    lv_display_set_default(s_display);

    ESP_LOGI(
        TAG,
        "LVGL display registered: %dx%d",
        LCD_H_RES,
        LCD_V_RES
    );

    return ESP_OK;
}

static void lvgl_touch_read_cb(
    lv_indev_t *indev,
    lv_indev_data_t *data
)
{
    (void)indev;

    uint16_t x = 0;
    uint16_t y = 0;
    bool pressed = false;

    esp_err_t err = touch_driver_read(
        &x,
        &y,
        &pressed
    );

    if (err != ESP_OK) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if (!pressed) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    /*
     * Keep the touch coordinates inside the display boundaries
     * before applying the display orientation.
     */
    if (x >= LCD_H_RES) {
        x = LCD_H_RES - 1U;
    }

    if (y >= LCD_V_RES) {
        y = LCD_V_RES - 1U;
    }

    /*
     * Invert the Y coordinate.
     */
    y = (LCD_V_RES - 1U) - y;

    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
}

static esp_err_t lvgl_touch_register(void)
{
    s_touch_indev = lv_indev_create();

    if (s_touch_indev == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL input device");
        return ESP_ERR_NO_MEM;
    }

    lv_indev_set_type(
        s_touch_indev,
        LV_INDEV_TYPE_POINTER
    );

    lv_indev_set_read_cb(
        s_touch_indev,
        lvgl_touch_read_cb
    );

    lv_indev_set_display(
        s_touch_indev,
        s_display
    );

    ESP_LOGI(TAG, "LVGL touch registered");

    return ESP_OK;
}

static void lvgl_port_cleanup(void)
{
    /*
     * Stop the LVGL tick source first.
     */
    if (s_tick_timer != NULL) {
        esp_err_t result = esp_timer_stop(
            s_tick_timer
        );

        if ((result != ESP_OK) &&
            (result != ESP_ERR_INVALID_STATE)) {

            ESP_LOGW(
                TAG,
                "Failed to stop LVGL tick timer: %s",
                esp_err_to_name(result)
            );
        }

        result = esp_timer_delete(
            s_tick_timer
        );

        if (result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Failed to delete LVGL tick timer: %s",
                esp_err_to_name(result)
            );
        } else {
            s_tick_timer = NULL;
        }
    }

    /*
     * Remove the DMA callback before deleting the LVGL display.
     * The callback stores s_display in user_ctx.
     */
    esp_lcd_panel_io_handle_t panel_io =
        display_driver_get_panel_io();

    if (panel_io != NULL) {
        const esp_lcd_panel_io_callbacks_t callbacks = {
            .on_color_trans_done = NULL,
        };

        const esp_err_t result =
            esp_lcd_panel_io_register_event_callbacks(
                panel_io,
                &callbacks,
                NULL
            );

        if (result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Failed to unregister LCD transfer callback: %s",
                esp_err_to_name(result)
            );
        }
    }

    /*
     * The input device refers to s_display, so delete it first.
     */
    if (s_touch_indev != NULL) {
        lv_indev_delete(
            s_touch_indev
        );

        s_touch_indev = NULL;
    }

    /*
     * LVGL does not own the externally allocated draw buffers.
     */
    if (s_display != NULL) {
        lv_display_delete(
            s_display
        );

        s_display = NULL;
    }

    if (s_draw_buffer_1 != NULL) {
        heap_caps_free(
            s_draw_buffer_1
        );

        s_draw_buffer_1 = NULL;
    }

    if (s_draw_buffer_2 != NULL) {
        heap_caps_free(
            s_draw_buffer_2
        );

        s_draw_buffer_2 = NULL;
    }

    s_initialized = false;
}

esp_err_t lvgl_port_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    if (display_driver_get_panel() == NULL) {
        ESP_LOGE(
            TAG,
            "Display driver must be initialized first"
        );

        return ESP_ERR_INVALID_STATE;
    }

    if (touch_driver_get_handle() == NULL) {
        ESP_LOGE(
            TAG,
            "Touch driver must be initialized first"
        );

        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(
        TAG,
        "Initializing LVGL"
    );

    if (!s_lvgl_initialized) {
        lv_init();
        s_lvgl_initialized = true;
    }

    esp_err_t result = lvgl_display_register();

    if (result == ESP_OK) {
        result = lvgl_touch_register();
    }

    if (result == ESP_OK) {
        result = lvgl_tick_init();
    }

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "LVGL port initialization failed: %s",
            esp_err_to_name(result)
        );

        lvgl_port_cleanup();

        return result;
    }

    s_initialized = true;

    ESP_LOGI(
        TAG,
        "LVGL port initialized"
    );

    return ESP_OK;
}

uint32_t lvgl_port_handler(void)
{
    if (!s_initialized) {
        return LVGL_HANDLER_MAX_MS;
    }

    uint32_t delay_ms = lv_timer_handler();

    if (delay_ms < LVGL_HANDLER_MIN_MS) {
        delay_ms = LVGL_HANDLER_MIN_MS;
    }

    if (delay_ms > LVGL_HANDLER_MAX_MS) {
        delay_ms = LVGL_HANDLER_MAX_MS;
    }

    return delay_ms;
}

lv_display_t *lvgl_port_get_display(void)
{
    return s_display;
}

lv_indev_t *lvgl_port_get_touch(void)
{
    return s_touch_indev;
}
