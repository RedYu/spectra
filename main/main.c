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
#include "storage_sd_service.h"
#include "settings_service.h"
#include "gui_service.h"

#include "logging_service.h"

static const char *TAG = "app_main";

static esp_err_t start_service(const char *name, esp_err_t (*start)(void))
{
    ESP_LOGI(TAG, "starting %s service...", name);

    esp_err_t err = start();
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "%s started with code %d", name, err);

    return err;
}

static void startup_task(
    void *argument
)
{
    (void)argument;

    esp_err_t result;

    gui_service_init();

    gui_service_set_boot_progress(
        5,
        "Starting system"
    );

    /*
     * Mount the storage.
     */
    result = storage_service_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "[%s] Internal storage is unavailable, continuing with defaults",
            esp_err_to_name(result)
        );

        gui_service_set_boot_progress(
            5,
            "Storage initialization failed"
        );

        vTaskDelete(NULL);
        return;
    }

    start_service("System", system_service_start);

    gui_service_set_boot_progress(
        30,
        "Storage ready"
    );

    /*
     * Load the configuration.
     */
    result = settings_service_init();

    if (result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "[%s] Configuration loading failed, defaults are active",
            esp_err_to_name(result)
        );

        gui_service_set_boot_progress(
            30,
            "Configuration error"
        );

        vTaskDelete(NULL);
        return;
    }

    gui_service_set_boot_progress(
        50,
        "Configuration loaded"
    );

    start_service("Storage SD card", storage_sd_service_start);

    gui_service_set_boot_progress(
        65,
        "SD card interface ready"
    );

    /*
     * Initialize display driver.
     */
    result = display_driver_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Display initialization failed: %s",
            esp_err_to_name(result)
        );

        gui_service_set_boot_progress(
            75,
            "Display initialization failed"
        );

        vTaskDelete(NULL);
        return;
    }

    gui_service_set_boot_progress(
        75,
        "Display interface ready"
    );

    /*
     * Initialize touch driver.
     */
    result = touch_driver_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Touch initialization failed: %s",
            esp_err_to_name(result)
        );

        gui_service_set_boot_progress(
            80,
            "Touch initialization failed"
        );

        vTaskDelete(NULL);
        return;
    }

    gui_service_set_boot_progress(
        80,
        "Touch interface ready"
    );

    /*
     * Initialize lvgl port.
     */
    result = lvgl_port_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "LVGL port initialization failed: %s",
            esp_err_to_name(result)
        );

        gui_service_set_boot_progress(
            85,
            "LVGL port initialization failed"
        );

        vTaskDelete(NULL);
        return;
    }

    start_service("GUI", gui_service_start);

    gui_service_set_boot_progress(
            100,
            "Good :)"
        );

    vTaskDelay(pdMS_TO_TICKS(100));

    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(
        logging_service_init()
    );

    ESP_LOGI(
        TAG,
        "Logging service initialized"
    );

    ESP_ERROR_CHECK(board_init());

    ESP_ERROR_CHECK(system_model_init());

    ESP_LOGI(TAG, "Application starting");

    const BaseType_t task_created =
        xTaskCreate(
            startup_task,
            "startup_task",
            6144,
            NULL,
            5,
            NULL
        );

    if (task_created != pdPASS) {
        ESP_LOGE(
            TAG,
            "Failed to create startup task"
        );
    }

    ESP_LOGI(TAG, "Application started");
}
