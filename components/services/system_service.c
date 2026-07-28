#include "system_service.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "board_config.h"
#include "system_model.h"

#define SYSTEM_TASK_STACK_SIZE      3072
#define SYSTEM_TASK_PRIORITY        2
#define SYSTEM_UPDATE_INTERVAL_MS   1000

static const char *TAG = "system_service";

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

static uint8_t system_service_get_cpu_usage(void)
{
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS

    const UBaseType_t task_count =
        uxTaskGetNumberOfTasks();

    TaskStatus_t *task_status =
        calloc(
            task_count,
            sizeof(TaskStatus_t)
        );

    if (task_status == NULL) {
        return 0;
    }

    configRUN_TIME_COUNTER_TYPE total_run_time = 0;

    const UBaseType_t received_task_count =
        uxTaskGetSystemState(
            task_status,
            task_count,
            &total_run_time
        );

    if (received_task_count == 0 ||
        total_run_time == 0) {

        free(task_status);
        return 0;
    }

    uint64_t idle_run_time = 0;

    for (UBaseType_t i = 0;
         i < received_task_count;
         i++) {

        if (strncmp(
                task_status[i].pcTaskName,
                "IDLE",
                4
            ) == 0) {

            idle_run_time +=
                task_status[i].ulRunTimeCounter;
        }
    }

    free(task_status);

    /*
     * ESP32-S3 has two CPU cores. Each core contributes its own
     * available runtime, so total CPU capacity is twice the timer
     * runtime.
     */
    const uint64_t total_cpu_time =
        (uint64_t)total_run_time *
        CONFIG_FREERTOS_NUMBER_OF_CORES;

    if (idle_run_time >= total_cpu_time) {
        return 0;
    }

    const uint64_t active_run_time =
        total_cpu_time -
        idle_run_time;

    uint32_t cpu_usage =
        (uint32_t)(
            active_run_time * 100ULL /
            total_cpu_time
        );

    if (cpu_usage > 100) {
        cpu_usage = 100;
    }

    return (uint8_t)cpu_usage;

#else

    return 0;

#endif
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

    model.cpu_usage =
        system_service_get_cpu_usage();

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

    system_model_get_snapshot(&model);

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
        SPECTRA_APP_TARGET,
        sizeof(model.device_id)
    );

    strlcpy(
        model.device_name,
        SPECTRA_APP_NAME,
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
