#include <stdbool.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "board.h"
#include "display_driver.h"
#include "touch_driver.h"
#include "system_model.h"
#include "system_service.h"
#include "storage_service.h"
#include "storage_sd_service.h"
#include "settings_model.h"
#include "settings_service.h"
#include "wifi_credentials_service.h"
#include "gui_service.h"
#include "logging_service.h"
#include "network_service.h"
#include "usb_network_service.h"
#include "web_service.h"
#include "mdns_service.h"
#include "internet_service.h"
#include "crash_dump_service.h"

#define STARTUP_TASK_STACK_SIZE  (6144U)
#define STARTUP_TASK_PRIORITY    (5U)

typedef enum
{
    SERVICE_OPTIONAL = 0,
    SERVICE_REQUIRED

} service_requirement_t;

static const char *TAG = "app_main";

static void log_heap_region(
    const char *name,
    uint32_t capabilities
)
{
    if (name == NULL) {
        return;
    }

    multi_heap_info_t info = {0};

    heap_caps_get_info(
        &info,
        capabilities
    );

    const size_t total =
        info.total_allocated_bytes +
        info.total_free_bytes;

    ESP_LOGI(
        "memory",
        "%s: tot=%u, usd=%u, fr=%u, "
        "min=%u, larg=%u, "
        "alloc_b=%u, free_b=%u",
        name,
        (unsigned int)total,
        (unsigned int)
            info.total_allocated_bytes,
        (unsigned int)
            info.total_free_bytes,
        (unsigned int)
            info.minimum_free_bytes,
        (unsigned int)
            info.largest_free_block,
        (unsigned int)
            info.allocated_blocks,
        (unsigned int)
            info.free_blocks
    );
}

static void log_memory_status(
    const char *stage
)
{
    ESP_LOGI(
        "memory",
        "Memory status: %s",
        stage != NULL
            ? stage
            : "unknown"
    );

    log_heap_region(
        "Internal",
        MALLOC_CAP_INTERNAL |
        MALLOC_CAP_8BIT
    );

    log_heap_region(
        "DMA",
        MALLOC_CAP_DMA |
        MALLOC_CAP_8BIT
    );

    log_heap_region(
        "PSRAM",
        MALLOC_CAP_SPIRAM |
        MALLOC_CAP_8BIT
    );

    log_heap_region(
        "Default",
        MALLOC_CAP_DEFAULT
    );
}

static esp_err_t initialize_nvs(void)
{
    esp_err_t result =
        nvs_flash_init();

    if ((result ==
         ESP_ERR_NVS_NO_FREE_PAGES) ||
        (result ==
         ESP_ERR_NVS_NEW_VERSION_FOUND)) {

        /*
         * The NVS partition cannot be used in its current format.
         * Erase it and create a new compatible NVS structure.
         */
        result = nvs_flash_erase();

        if (result != ESP_OK) {
            return result;
        }

        result = nvs_flash_init();
    }

    return result;
}

static esp_err_t start_service(
    const char *name,
    esp_err_t (*start)(void),
    service_requirement_t requirement
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
        if (requirement ==
            SERVICE_REQUIRED) {

            ESP_LOGE(
                TAG,
                "Failed to start required %s service: %s",
                name,
                esp_err_to_name(result)
            );

        } else {
            ESP_LOGW(
                TAG,
                "Failed to start optional %s service: %s",
                name,
                esp_err_to_name(result)
            );
        }

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

    bool startup_warning = false;

    log_memory_status(
        "Application start"
    );

    /*
     * Initialize the display stack first so the splash screen can
     * report the remaining startup stages.
     */
    esp_err_t result =
        display_driver_init();

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

    result = start_service(
        "GUI",
        gui_service_start,
        SERVICE_REQUIRED
    );

    if (result != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    /*
     * Give the GUI task an opportunity to create the splash screen.
     */
    vTaskDelay(
        pdMS_TO_TICKS(250U)
    );

    (void)gui_service_set_boot_progress(
        5U,
        "Initializing internal storage"
    );

    result = storage_service_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Internal storage initialization failed: %s",
            esp_err_to_name(result)
        );

        (void)gui_service_set_boot_progress(
            5U,
            "Internal storage failed"
        );

        vTaskDelete(NULL);
        return;
    }

    (void)gui_service_set_boot_progress(
        15U,
        "Starting system services"
    );

    result = start_service(
        "System",
        system_service_start,
        SERVICE_REQUIRED
    );

    if (result != ESP_OK) {
        (void)gui_service_set_boot_progress(
            15U,
            "System initialization failed"
        );

        vTaskDelete(NULL);
        return;
    }

    (void)gui_service_set_boot_progress(
        20U,
        "Initializing network stack"
    );

    result = network_service_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize network service: %s",
            esp_err_to_name(result)
        );

        (void)gui_service_set_boot_progress(
            20U,
            "Network initialization failed"
        );

        vTaskDelete(NULL);
        return;
    }

    (void)gui_service_set_boot_progress(
        25U,
        "Initializing Wi-Fi credentials"
    );

    result =
        wifi_credentials_service_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize Wi-Fi credentials service: %s",
            esp_err_to_name(result)
        );

        (void)gui_service_set_boot_progress(
            25U,
            "Wi-Fi credentials initialization failed"
        );

        vTaskDelete(NULL);
        return;
    }

    (void)gui_service_set_boot_progress(
        30U,
        "Loading configuration"
    );

    result = settings_service_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Configuration initialization failed: %s",
            esp_err_to_name(result)
        );

        (void)gui_service_set_boot_progress(
            30U,
            "Configuration loading failed"
        );

        vTaskDelete(NULL);
        return;
    }

    app_settings_t settings;

    result = settings_model_get(
        &settings
    );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to read application settings: %s",
            esp_err_to_name(result)
        );

        vTaskDelete(NULL);
        return;
    }

    (void)gui_service_set_boot_progress(
        40U,
        "Initializing SD card"
    );

    result = start_service(
        "Storage SD card",
        storage_sd_service_start,
        SERVICE_OPTIONAL
    );

    if (result != ESP_OK) {
        startup_warning = true;

        (void)gui_service_set_boot_progress(
            45U,
            "SD card service unavailable"
        );

    } else {
        storage_sd_state_t sd_state =
            STORAGE_SD_STATE_UNAVAILABLE;

        result = storage_sd_service_get_state(
            &sd_state
        );

        if ((result != ESP_OK) ||
            (sd_state != STORAGE_SD_STATE_MOUNTED)) {

            startup_warning = true;

            (void)gui_service_set_boot_progress(
                45U,
                "SD card unavailable"
            );

        } else {
            const esp_err_t dump_result =
                crash_dump_service_export_to_sd();

            if (dump_result != ESP_OK) {
                startup_warning = true;

                ESP_LOGW(
                    TAG,
                    "Failed to export crash dump: %s",
                    esp_err_to_name(dump_result)
                );

                (void)gui_service_set_boot_progress(
                    45U,
                    "Crash dump export failed"
                );

            } else {
                (void)gui_service_set_boot_progress(
                    45U,
                    "SD card ready"
                );
            }
        }
    }

    (void)gui_service_set_boot_progress(
        50U,
        "Applying settings"
    );

    result = settings_service_apply();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to apply settings: %s",
            esp_err_to_name(result)
        );

        (void)gui_service_set_boot_progress(
            50U,
            "Settings application failed"
        );

        vTaskDelete(NULL);
        return;
    }

    (void)gui_service_set_boot_progress(
        75U,
        settings.usb_rndis.enabled
            ? "Starting USB network"
            : "USB network disabled"
    );

    if (settings.usb_rndis.enabled) {
        result =
            usb_network_service_init();

        if (result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Failed to initialize USB network service: %s",
                esp_err_to_name(result)
            );

            startup_warning = true;

            (void)gui_service_set_boot_progress(
                85U,
                "USB network unavailable"
            );

        } else {
            (void)gui_service_set_boot_progress(
                85U,
                "USB network ready"
            );
        }
    } else {
        ESP_LOGI(
            TAG,
            "USB RNDIS is disabled in settings"
        );

        (void)gui_service_set_boot_progress(
            85U,
            "USB network disabled"
        );
    }

    (void)gui_service_set_boot_progress(
        90U,
        "Starting web interface"
    );

    result = web_service_start();

    if (result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to start web service: %s",
            esp_err_to_name(result)
        );

        startup_warning = true;

        (void)gui_service_set_boot_progress(
            95U,
            "Web interface unavailable"
        );

    } else {
        (void)gui_service_set_boot_progress(
            95U,
            "Web interface ready"
        );
    }

    result = start_service(
        "mDNS",
        mdns_service_start,
        SERVICE_OPTIONAL
    );

    if (result != ESP_OK) {
        startup_warning = true;

        ESP_LOGW(
            TAG,
            "Local mDNS discovery is unavailable: %s",
            esp_err_to_name(result)
        );
    }

    (void)gui_service_set_boot_progress(
        97U,
        "Starting connectivity monitor"
    );

    result = start_service(
        "Internet connectivity",
        internet_service_start,
        SERVICE_OPTIONAL
    );

    if (result != ESP_OK) {
        startup_warning = true;

        (void)gui_service_set_boot_progress(
            97U,
            "Connectivity monitor unavailable"
        );
    } else {
        (void)gui_service_set_boot_progress(
            97U,
            "Connectivity monitor ready"
        );
    }

    log_memory_status(
        "Startup complete"
    );

    (void)gui_service_set_boot_progress(
        100U,
        startup_warning
            ? "Ready with warnings"
            : "Ready"
    );

    if (startup_warning) {
        ESP_LOGW(
            TAG,
            "Application startup completed with warnings"
        );

    } else {
        ESP_LOGI(
            TAG,
            "Application startup completed"
        );
    }

    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(
        initialize_nvs()
    );

    ESP_LOGI(
        TAG,
        "NVS initialized"
    );

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
