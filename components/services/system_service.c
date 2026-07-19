#include "system_service.h"

#include "app_config.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_config.h"
#include "system_model.h"

static const char *TAG = "system_service";

#define SYSTEM_TASK_STACK_SIZE      3072
#define SYSTEM_TASK_PRIORITY        2
#define SYSTEM_UPDATE_INTERVAL_MS   1000

static TaskHandle_t s_task_handle;

static esp_err_t system_service_generate_serial(
    char *buffer,
    size_t buffer_size
)
{
    uint8_t mac[6];

    esp_err_t err = esp_read_mac(
        mac,
        ESP_MAC_WIFI_STA
    );

    if (err != ESP_OK) {
        return err;
    }

    snprintf(
        buffer,
        buffer_size,
        "SP-%02X%02X%02X%02X%02X%02X",
        mac[0],
        mac[1],
        mac[2],
        mac[3],
        mac[4],
        mac[5]
    );

    return ESP_OK;
}

static esp_err_t system_service_load_serial_number(
    char *buffer,
    size_t buffer_size
)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;

    esp_err_t err = nvs_open(
        "system",
        NVS_READONLY,
        &handle
    );

    if (err != ESP_OK) {
        strlcpy(buffer, "UNKNOWN", buffer_size);
        return err;
    }

    size_t required_size = buffer_size;

    err = nvs_get_str(
        handle,
        "serial",
        buffer,
        &required_size
    );

    nvs_close(handle);

    if (err != ESP_OK) {
        strlcpy(buffer, "UNKNOWN", buffer_size);
    }

    return err;
}

static void system_service_update_runtime(void)
{
    system_model_t model;

    system_model_get_snapshot(&model);

    model.uptime_sec =
        (uint32_t)(esp_timer_get_time() / 1000000ULL);

    model.free_heap =
        heap_caps_get_free_size(MALLOC_CAP_DEFAULT);

    model.minimum_free_heap =
        heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);

    system_model_set(&model);
}

static void system_service_task(void *argument)
{
    while (true) {
        system_service_update_runtime();

        vTaskDelay(
            pdMS_TO_TICKS(SYSTEM_UPDATE_INTERVAL_MS)
        );
    }
}

esp_err_t system_service_start(void)
{
    if (s_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    system_model_t model = {0};

    const esp_app_desc_t *app =
        esp_app_get_description();

    ESP_LOGI(
        TAG,
        "Firmware version: %s",
        app->version
    );

    strlcpy(
        model.firmware_version,
        app->version,
        sizeof(model.firmware_version)
    );

    strlcpy(
        model.hardware_version,
        BOARD_HARDWARE_VERSION,
        sizeof(model.hardware_version)
    );

    strlcpy(
        model.device_id,
        APP_TARGET,
        sizeof(model.device_id)
    );

    strlcpy(
        model.device_name,
        APP_NAME,
        sizeof(model.device_name)
    );

    strlcpy(
        model.serial_number,
        "UNKNOWN",
        sizeof(model.serial_number)
    );

    model.free_heap =
        heap_caps_get_free_size(MALLOC_CAP_DEFAULT);

    model.minimum_free_heap =
        heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);

    system_model_set(&model);

    BaseType_t result = xTaskCreate(
        system_service_task,
        "system_task",
        SYSTEM_TASK_STACK_SIZE,
        NULL,
        SYSTEM_TASK_PRIORITY,
        &s_task_handle
    );

    if (result != pdPASS) {
        s_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "System service started");

    return ESP_OK;
}

void system_service_stop(void)
{
    if (s_task_handle == NULL) {
        return;
    }

    vTaskDelete(s_task_handle);
    s_task_handle = NULL;
}
