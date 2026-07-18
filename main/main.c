#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

#include "board_config.h"
#include "display_driver.h"
#include "touch_driver.h"
#include "lvgl_port.h"
#include "gui_service.h"

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_LOGI(TAG, "Application starting");

    ESP_ERROR_CHECK(display_driver_init());
    ESP_ERROR_CHECK(touch_driver_init());
    ESP_ERROR_CHECK(lvgl_port_init());
    ESP_ERROR_CHECK(gui_service_start());

    ESP_LOGI(TAG, "Application started");
}