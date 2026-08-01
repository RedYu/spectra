#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "display_driver.h"
#include "touch_driver.h"
#include "system_model.h"
#include "system_service.h"
#include "storage_service.h"
#include "storage_sd_service.h"
#include "settings_service.h"
#include "gui_service.h"
#include "logging_service.h"
#include "usb_network_service.h"

#define STARTUP_TASK_STACK_SIZE  (6144U)
#define STARTUP_TASK_PRIORITY    (5U)

static const char *TAG = "app_main";

static esp_err_t start_service(
    const char *name,
    esp_err_t (*start)(void)
)
{
    if ((name == NULL) ||
        (start == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(
        TAG,
        "Starting %s service",
        name
    );

    const esp_err_t result =
        start();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to start %s service: %s",
            name,
            esp_err_to_name(result)
        );

        return result;
    }

    ESP_LOGI(
        TAG,
        "%s service started",
        name
    );

    return ESP_OK;
}

static void startup_task(
    void *argument
)
{
    (void)argument;

    esp_err_t result =
        storage_service_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Internal storage initialization failed: %s",
            esp_err_to_name(result)
        );

        vTaskDelete(NULL);
        return;
    }

    result = start_service(
        "System",
        system_service_start
    );

    if (result != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    /*
     * Display and touch must be ready before the GUI task initializes
     * the LVGL port.
     */
    result = display_driver_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Display initialization failed: %s",
            esp_err_to_name(result)
        );

        vTaskDelete(NULL);
        return;
    }

    result = touch_driver_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Touch initialization failed: %s",
            esp_err_to_name(result)
        );

        vTaskDelete(NULL);
        return;
    }

    result = gui_service_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize GUI service: %s",
            esp_err_to_name(result)
        );

        vTaskDelete(NULL);
        return;
    }

    result = gui_service_set_boot_progress(
        10U,
        "Starting application"
    );

    if (result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to set boot progress: %s",
            esp_err_to_name(result)
        );
    }

    result = start_service(
        "GUI",
        gui_service_start
    );

    if (result != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    (void)gui_service_set_boot_progress(
        30U,
        "Storage ready"
    );

    vTaskDelay(
        pdMS_TO_TICKS(100U)
    );

    /*
     * Load the configuration after the GUI has started so loading
     * progress can be displayed on the splash screen.
     */
    result = settings_service_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Configuration initialization failed: %s",
            esp_err_to_name(result)
        );

        (void)gui_service_set_boot_progress(
            50U,
            "Configuration error"
        );

        vTaskDelete(NULL);
        return;
    }

    (void)gui_service_set_boot_progress(
        60U,
        "Configuration loaded"
    );

    vTaskDelay(
        pdMS_TO_TICKS(100U)
    );

    result = start_service(
        "Storage SD card",
        storage_sd_service_start
    );

    if (result != ESP_OK) {
        /*
         * Decide whether an SD-service initialization error is fatal.
         * Absence of a card should already be handled as ESP_OK by the
         * SD storage service.
         */
        (void)gui_service_set_boot_progress(
            80U,
            "SD card initialization failed"
        );

        vTaskDelete(NULL);
        return;
    }

    (void)gui_service_set_boot_progress(
        85U,
        "SD card interface ready"
    );

    vTaskDelay(
        pdMS_TO_TICKS(100U)
    );

    result = settings_service_apply();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to apply settings: %s",
            esp_err_to_name(result)
        );

        (void)gui_service_set_boot_progress(
            90U,
            "Settings application failed"
        );

        vTaskDelete(NULL);
        return;
    }

    (void)gui_service_set_boot_progress(
        100U,
        "Ready"
    );

    ESP_LOGI(
        TAG,
        "Application startup completed"
    );

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

    ESP_ERROR_CHECK(
        esp_netif_init()
    );

    ESP_ERROR_CHECK(
        esp_event_loop_create_default()
    );

    ESP_ERROR_CHECK(
        usb_network_service_init()
    );

    ESP_LOGI(TAG, "Application starting");

    const BaseType_t task_created =
        xTaskCreate(
            startup_task,
            "startup_task",
            STARTUP_TASK_STACK_SIZE,
            NULL,
            STARTUP_TASK_PRIORITY,
            NULL
        );

    if (task_created != pdPASS) {
        ESP_LOGE(
            TAG,
            "Failed to create startup task"
        );

        return;
    }

    ESP_LOGI(
        TAG,
        "Startup task created"
    );
}
