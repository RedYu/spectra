#include "system_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "board_config.h"
#include "system_model.h"

#define SYSTEM_TASK_STACK_SIZE      (3072U)
#define SYSTEM_TASK_PRIORITY        (2U)
#define SYSTEM_UPDATE_INTERVAL_MS   (1000U)
#define SYSTEM_TASK_STATUS_EXTRA_COUNT  (4U)

/*
 * TODO:
 * system_service_stop() requests asynchronous cooperative shutdown.
 * Add synchronized service state and completion acknowledgement before
 * supporting an immediate stop/start sequence.
 *
 * Extend system_model_t with separate PSRAM statistics:
 *
 *     uint32_t psram_free;
 *     uint32_t psram_minimum_free;
 *
 * Update these fields in system_service_update_runtime() using
 * MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT. Internal heap and PSRAM
 * statistics must remain separate because free PSRAM cannot replace
 * internal memory required by Wi-Fi, DMA and system services.
 */

static const char *TAG = "system_service";

static TaskHandle_t s_task_handle = NULL;

#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS

static configRUN_TIME_COUNTER_TYPE
    s_previous_total_run_time = 0U;

static configRUN_TIME_COUNTER_TYPE
    s_previous_idle_run_time[
        CONFIG_FREERTOS_NUMBER_OF_CORES
    ] = {0};

static bool s_previous_cpu_sample_valid = false;

#endif

static void system_service_reset_cpu_usage(void)
{
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS

    s_previous_total_run_time = 0U;

    memset(
        s_previous_idle_run_time,
        0,
        sizeof(s_previous_idle_run_time)
    );

    s_previous_cpu_sample_valid = false;

#endif
}

static esp_err_t system_service_generate_serial(
    char *buffer,
    size_t buffer_size
)
{
    if ((buffer == NULL) ||
        (buffer_size == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    buffer[0] = '\0';

    uint8_t mac[6] = {0};

    const esp_err_t result =
        esp_read_mac(
            mac,
            ESP_MAC_WIFI_STA
        );

    if (result != ESP_OK) {
        return result;
    }

    const int written = snprintf(
        buffer,
        buffer_size,
        "SP-%02X%02X%02X%02X%02X%02X",
        (unsigned int)mac[0],
        (unsigned int)mac[1],
        (unsigned int)mac[2],
        (unsigned int)mac[3],
        (unsigned int)mac[4],
        (unsigned int)mac[5]
    );

    if (written < 0) {
        buffer[0] = '\0';
        return ESP_FAIL;
    }

    if ((size_t)written >= buffer_size) {
        buffer[buffer_size - 1U] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static uint8_t system_service_get_cpu_usage(void)
{
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS

    const UBaseType_t task_count =
        uxTaskGetNumberOfTasks();

    if (task_count == 0U) {
        return 0U;
    }

    if (task_count >
        (UBaseType_t)(
            SIZE_MAX / sizeof(TaskStatus_t) -
            SYSTEM_TASK_STATUS_EXTRA_COUNT
        )) {

        return 0U;
    }

    const UBaseType_t task_capacity =
        task_count +
        SYSTEM_TASK_STATUS_EXTRA_COUNT;

    TaskStatus_t *task_status =
        heap_caps_calloc(
            task_capacity,
            sizeof(*task_status),
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

    if (task_status == NULL) {
        return 0U;
    }

    configRUN_TIME_COUNTER_TYPE total_run_time = 0U;

    const UBaseType_t received_task_count =
        uxTaskGetSystemState(
            task_status,
            task_capacity,
            &total_run_time
        );

    if ((received_task_count == 0U) ||
        (total_run_time == 0U)) {

        free(task_status);
        return 0U;
    }

    configRUN_TIME_COUNTER_TYPE current_idle_run_time[
        CONFIG_FREERTOS_NUMBER_OF_CORES
    ] = {0};

    bool idle_task_found[
        CONFIG_FREERTOS_NUMBER_OF_CORES
    ] = {false};

    for (UBaseType_t i = 0U;
         i < received_task_count;
         ++i) {

        const char *task_name =
            task_status[i].pcTaskName;

        if ((task_name == NULL) ||
            (strncmp(task_name, "IDLE", 4U) != 0)) {

            continue;
        }

        UBaseType_t core_index = 0U;

#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1

        /*
         * ESP-IDF normally names the idle tasks IDLE0 and IDLE1.
         */
        const char core_character =
            task_name[4];

        if ((core_character >= '0') &&
            (core_character <= '9')) {

            core_index =
                (UBaseType_t)(
                    core_character - '0'
                );
        } else {
            /*
             * Fall back to the first available core slot.
             */
            while ((core_index <
                    CONFIG_FREERTOS_NUMBER_OF_CORES) &&
                   idle_task_found[core_index]) {

                ++core_index;
            }
        }

#endif

        if (core_index <
            CONFIG_FREERTOS_NUMBER_OF_CORES) {

            current_idle_run_time[core_index] =
                task_status[i].ulRunTimeCounter;

            idle_task_found[core_index] = true;
        }
    }

    free(task_status);

    for (UBaseType_t core = 0U;
         core < CONFIG_FREERTOS_NUMBER_OF_CORES;
         ++core) {

        if (!idle_task_found[core]) {
            /*
             * The sample is incomplete, so do not publish an
             * unreliable CPU usage value.
             */
            s_previous_cpu_sample_valid = false;
            return 0U;
        }
    }

    if (!s_previous_cpu_sample_valid) {
        s_previous_total_run_time =
            total_run_time;

        for (UBaseType_t core = 0U;
             core < CONFIG_FREERTOS_NUMBER_OF_CORES;
             ++core) {

            s_previous_idle_run_time[core] =
                current_idle_run_time[core];
        }

        s_previous_cpu_sample_valid = true;

        /*
         * Two samples are required to calculate an interval.
         */
        return 0U;
    }

    /*
     * Unsigned subtraction also handles runtime counter wraparound.
     */
    const configRUN_TIME_COUNTER_TYPE
        total_run_time_delta =
            (configRUN_TIME_COUNTER_TYPE)(
                total_run_time -
                s_previous_total_run_time
            );

    uint64_t idle_run_time_delta = 0ULL;

    for (UBaseType_t core = 0U;
         core < CONFIG_FREERTOS_NUMBER_OF_CORES;
         ++core) {

        const configRUN_TIME_COUNTER_TYPE
            core_idle_delta =
                (configRUN_TIME_COUNTER_TYPE)(
                    current_idle_run_time[core] -
                    s_previous_idle_run_time[core]
                );

        idle_run_time_delta +=
            (uint64_t)core_idle_delta;

        s_previous_idle_run_time[core] =
            current_idle_run_time[core];
    }

    s_previous_total_run_time =
        total_run_time;

    const uint64_t total_cpu_time_delta =
        (uint64_t)total_run_time_delta *
        (uint64_t)CONFIG_FREERTOS_NUMBER_OF_CORES;

    if ((total_cpu_time_delta == 0ULL) ||
        (idle_run_time_delta >= total_cpu_time_delta)) {

        return 0U;
    }

    const uint64_t active_run_time_delta =
        total_cpu_time_delta -
        idle_run_time_delta;

    uint64_t cpu_usage =
        (active_run_time_delta * 100ULL) /
        total_cpu_time_delta;

    if (cpu_usage > 100ULL) {
        cpu_usage = 100ULL;
    }

    return (uint8_t)cpu_usage;

#else

    return 0U;

#endif
}

static esp_err_t system_service_update_runtime(void)
{
    const uint32_t uptime_sec =
        (uint32_t)(
            esp_timer_get_time() /
            1000000LL
        );

    const uint32_t free_heap =
        (uint32_t)heap_caps_get_free_size(
            MALLOC_CAP_INTERNAL |
            MALLOC_CAP_8BIT
        );

    const uint32_t minimum_free_heap =
        (uint32_t)heap_caps_get_minimum_free_size(
            MALLOC_CAP_INTERNAL |
            MALLOC_CAP_8BIT
        );

    const uint8_t cpu_usage =
        system_service_get_cpu_usage();

    return system_model_set_runtime(
        uptime_sec,
        free_heap,
        minimum_free_heap,
        cpu_usage
    );
}

static void system_service_task(
    void *argument
)
{
    (void)argument;

    while (true) {
        /*
         * Check for a stop request before starting another update.
         */
        if (ulTaskNotifyTake(
                pdTRUE,
                0U
            ) > 0U) {

            break;
        }

        const esp_err_t result =
            system_service_update_runtime();

        if (result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Failed to update runtime model: %s",
                esp_err_to_name(result)
            );
        }

        /*
         * Wait for either the next update interval or a stop
         * notification.
         */
        if (ulTaskNotifyTake(
                pdTRUE,
                pdMS_TO_TICKS(
                    SYSTEM_UPDATE_INTERVAL_MS
                )
            ) > 0U) {

            break;
        }
    }

    ESP_LOGI(
        TAG,
        "System service stopped"
    );

    /*
     * Clear the handle before deleting the current task so the
     * service can be started again.
     */
    s_task_handle = NULL;

    vTaskDelete(NULL);
}

esp_err_t system_service_start(void)
{
    if (s_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    system_model_t model = {0};

    esp_err_t result =
        system_model_get_snapshot(
            &model
        );

    if (result != ESP_OK) {
        return result;
    }

    const esp_app_desc_t *app =
        esp_app_get_description();

    ESP_LOGI(
        TAG,
        "Firmware version: %s",
        app->version
    );

    (void)strlcpy(
        model.firmware_version,
        app->version,
        sizeof(model.firmware_version)
    );

    (void)strlcpy(
        model.hardware_version,
        BOARD_HARDWARE_VERSION,
        sizeof(model.hardware_version)
    );

    (void)strlcpy(
        model.device_id,
        SPECTRA_APP_TARGET,
        sizeof(model.device_id)
    );

    (void)strlcpy(
        model.device_name,
        SPECTRA_APP_NAME,
        sizeof(model.device_name)
    );

    const esp_err_t serial_result =
        system_service_generate_serial(
            model.serial_number,
            sizeof(model.serial_number)
        );

    if (serial_result != ESP_OK) {
        (void)strlcpy(
            model.serial_number,
            "UNKNOWN",
            sizeof(model.serial_number)
        );
    }

    model.free_heap =
        (uint32_t)heap_caps_get_free_size(
            MALLOC_CAP_INTERNAL |
            MALLOC_CAP_8BIT
        );

    model.minimum_free_heap =
        (uint32_t)heap_caps_get_minimum_free_size(
            MALLOC_CAP_INTERNAL |
            MALLOC_CAP_8BIT
        );

    result = system_model_set(
        &model
    );

    if (result != ESP_OK) {
        return result;
    }

    system_service_reset_cpu_usage();

    const BaseType_t task_result =
        xTaskCreate(
            system_service_task,
            "system_task",
            SYSTEM_TASK_STACK_SIZE,
            NULL,
            SYSTEM_TASK_PRIORITY,
            &s_task_handle
        );

    if (task_result != pdPASS) {
        s_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "System service started");

    return ESP_OK;
}

void system_service_stop(void)
{
    TaskHandle_t task_handle =
        s_task_handle;

    if (task_handle == NULL) {
        return;
    }

    /*
     * Wake the task and request cooperative termination.
     */
    (void)xTaskNotifyGive(
        task_handle
    );
}
