#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_check.h"
#include "esp_log.h"

#include "board.h"
#include "board_config.h"
#include "display_driver.h"
#include "touch_driver.h"
#include "lvgl_port.h"

#include "system_model.h"

#include "app_config.h"
#include "system_service.h"
#include "storage_service.h"
#include "settings_service.h"
#include "gui_service.h"


static const char *TAG = "app_main";

static void print_task_list(void);

static esp_err_t start_service(const char *name, esp_err_t (*start)(void))
{
    ESP_LOGI(TAG, "starting %s service...", name);

    esp_err_t err = start();
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "%s started with code %d", name, err);

    return err;
}

void app_main(void)
{
    ESP_ERROR_CHECK(board_init());

    ESP_ERROR_CHECK(system_model_init());

    ESP_LOGI(TAG, "Application starting");

    esp_err_t err = storage_service_init();

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Internal storage is unavailable, continuing with defaults"
        );
    }

    start_service("System", system_service_start);

    err = settings_service_init();

    if (err != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Configuration loading failed, defaults are active"
        );
    }

    ESP_ERROR_CHECK(display_driver_init());
    ESP_ERROR_CHECK(touch_driver_init());
    ESP_ERROR_CHECK(lvgl_port_init());

    start_service("GUI", gui_service_start);

    ESP_LOGI(TAG, "Application started");

    print_task_list();
}

static void print_task_list(void)
{
    char *buffer = malloc(1024);

    if (buffer == NULL) {
        return;
    }

    vTaskList(buffer);

    ESP_LOGI(TAG,
             "\n"
             "Name          State    Prio    Stack  Num     Core\n"
             "-----------------------------------\n"
             "%s",
             buffer);

    free(buffer);
}
