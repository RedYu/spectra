/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "system_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "driver/temperature_sensor.h"

#include "app_config.h"
#include "app_task_priorities.h"
#include "board_config.h"
#include "system_model.h"
#include "shutdown_service.h"
#include "can_fd_service.h"

#define SYSTEM_RESTART_DELAY_MAX_MS     (60000U)

#define SYSTEM_TEMPERATURE_MIN_C             (10)
#define SYSTEM_TEMPERATURE_MAX_C             (80)
#define SYSTEM_TEMPERATURE_UPDATE_INTERVAL   (5U)

#define SYSTEM_CAN_FD_PROFILE_INTERVAL  (10U)

#define SYSTEM_TASK_STACK_SIZE          (3072U)
#define SYSTEM_TASK_PRIORITY \
    APP_TASK_PRIORITY_SYSTEM
#define SYSTEM_UPDATE_INTERVAL_MS       (1000U)
#define SYSTEM_TASK_STATUS_EXTRA_COUNT  (4U)

#define SYSTEM_STOP_TIMEOUT_MS          (2000U)

#define SYSTEM_EVENT_STOPPED            BIT0

static const char *TAG = "system_service";

static TaskHandle_t s_task_handle = NULL;
static EventGroupHandle_t s_events = NULL;

static temperature_sensor_handle_t
    s_temperature_sensor = NULL;

static uint32_t s_temperature_update_counter = 0U;

#if CAN_FD_MCP2518FD_ENABLE_PROFILING

static can_fd_mcp2518fd_profile_t
    s_previous_can_fd_profile;

static int64_t
    s_previous_can_fd_profile_time_us = 0;

static uint32_t
    s_can_fd_profile_update_counter = 0U;

static bool
    s_previous_can_fd_profile_valid = false;

#endif

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

static void system_service_update_can_fd_profile(void)
{
#if CAN_FD_MCP2518FD_ENABLE_PROFILING

    ++s_can_fd_profile_update_counter;

    if (s_can_fd_profile_update_counter <
        SYSTEM_CAN_FD_PROFILE_INTERVAL) {

        return;
    }

    s_can_fd_profile_update_counter = 0U;

    if (!can_fd_service_is_running()) {
        s_previous_can_fd_profile_valid = false;
        s_previous_can_fd_profile_time_us = 0;
        return;
    }

    can_fd_mcp2518fd_profile_t current;

    const esp_err_t result =
        can_fd_service_get_driver_profile(
            &current
        );

    if (result != ESP_OK) {
        if ((result != ESP_ERR_INVALID_STATE) &&
            (result != ESP_ERR_NOT_SUPPORTED)) {

            ESP_LOGW(
                TAG,
                "Failed to read MCP2518FD profile: %s",
                esp_err_to_name(result)
            );
        }

        s_previous_can_fd_profile_valid = false;
        s_previous_can_fd_profile_time_us = 0;
        return;
    }

    const int64_t current_time_us =
        esp_timer_get_time();

    /*
     * The first sample establishes the baseline. Also establish a new
     * baseline if the driver was restarted and its counters reset.
     */
    const bool profile_reset =
        (current.interrupt_count <
        s_previous_can_fd_profile.interrupt_count) ||
        (current.received_frames <
        s_previous_can_fd_profile.received_frames) ||
        (current.interrupt_time_us <
        s_previous_can_fd_profile.interrupt_time_us) ||
        (current.fifo_status_time_us <
        s_previous_can_fd_profile.fifo_status_time_us) ||
        (current.object_read_time_us <
        s_previous_can_fd_profile.object_read_time_us) ||
        (current.fifo_increment_time_us <
        s_previous_can_fd_profile.fifo_increment_time_us) ||
        (current.decode_time_us <
        s_previous_can_fd_profile.decode_time_us);

    if (!s_previous_can_fd_profile_valid ||
        profile_reset) {

        s_previous_can_fd_profile = current;
        s_previous_can_fd_profile_time_us =
            current_time_us;
        s_previous_can_fd_profile_valid = true;

        return;
    }

    const uint32_t interrupt_delta =
        current.interrupt_count -
        s_previous_can_fd_profile.interrupt_count;

    const uint32_t frame_delta =
        current.received_frames -
        s_previous_can_fd_profile.received_frames;

    const uint64_t interrupt_time_delta =
        current.interrupt_time_us -
        s_previous_can_fd_profile.interrupt_time_us;

    const uint64_t status_time_delta =
        current.fifo_status_time_us -
        s_previous_can_fd_profile.fifo_status_time_us;

    const uint64_t read_time_delta =
        current.object_read_time_us -
        s_previous_can_fd_profile.object_read_time_us;

    const uint64_t increment_time_delta =
        current.fifo_increment_time_us -
        s_previous_can_fd_profile.fifo_increment_time_us;

    const uint64_t decode_time_delta =
        current.decode_time_us -
        s_previous_can_fd_profile.decode_time_us;

    const int64_t interval_us =
        current_time_us -
        s_previous_can_fd_profile_time_us;

    const uint32_t frames_per_second =
        (interval_us > 0)
            ? (uint32_t)(
                ((uint64_t)frame_delta * 1000000ULL) /
                (uint64_t)interval_us
            )
            : 0U;

    const uint64_t average_irq_time_us =
        (interrupt_delta > 0U)
            ? interrupt_time_delta /
              (uint64_t)interrupt_delta
            : 0ULL;

    const uint64_t average_read_time_us =
        (frame_delta > 0U)
            ? read_time_delta /
              (uint64_t)frame_delta
            : 0ULL;

    ESP_LOGI(
        TAG,
        "MCP2518FD profile: "
        "RX=%lu (%lu frame/s), IRQ=%lu, "
        "IRQ avg=%llu us, read avg=%llu us, "
        "status=%llu us, UINC=%llu us, decode=%llu us",
        (unsigned long)frame_delta,
        (unsigned long)frames_per_second,
        (unsigned long)interrupt_delta,
        (unsigned long long)average_irq_time_us,
        (unsigned long long)average_read_time_us,
        (unsigned long long)status_time_delta,
        (unsigned long long)increment_time_delta,
        (unsigned long long)decode_time_delta
    );

    s_previous_can_fd_profile = current;
    s_previous_can_fd_profile_time_us =
        current_time_us;

#endif
}

static esp_err_t system_service_create_resources(void)
{
    if (s_events != NULL) {
        return ESP_OK;
    }

    s_events =
        xEventGroupCreate();

    if (s_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t system_service_temperature_init(void)
{
    if (s_temperature_sensor != NULL) {
        return ESP_OK;
    }

    const temperature_sensor_config_t config =
        TEMPERATURE_SENSOR_CONFIG_DEFAULT(
            SYSTEM_TEMPERATURE_MIN_C,
            SYSTEM_TEMPERATURE_MAX_C
        );

    esp_err_t result =
        temperature_sensor_install(
            &config,
            &s_temperature_sensor
        );

    if (result != ESP_OK) {
        s_temperature_sensor = NULL;
        return result;
    }

    result =
        temperature_sensor_enable(
            s_temperature_sensor
        );

    if (result != ESP_OK) {
        (void)temperature_sensor_uninstall(
            s_temperature_sensor
        );

        s_temperature_sensor = NULL;
        return result;
    }

    return ESP_OK;
}

static void system_service_temperature_deinit(void)
{
    if (s_temperature_sensor == NULL) {
        return;
    }

    (void)temperature_sensor_disable(
        s_temperature_sensor
    );

    (void)temperature_sensor_uninstall(
        s_temperature_sensor
    );

    s_temperature_sensor = NULL;
}

static esp_err_t system_service_update_temperature(void)
{
    if (s_temperature_sensor == NULL) {
        return system_model_set_chip_temperature(
            0.0F,
            false
        );
    }

    float temperature_celsius = 0.0F;

    const esp_err_t result =
        temperature_sensor_get_celsius(
            s_temperature_sensor,
            &temperature_celsius
        );

    if (result != ESP_OK) {
        (void)system_model_set_chip_temperature(
            0.0F,
            false
        );

        return result;
    }

    return system_model_set_chip_temperature(
        temperature_celsius,
        true
    );
}

static const char *system_service_chip_model_to_string(
    esp_chip_model_t model
)
{
    switch (model) {
        case CHIP_ESP32:
            return "ESP32";

        case CHIP_ESP32S2:
            return "ESP32-S2";

        case CHIP_ESP32S3:
            return "ESP32-S3";

        case CHIP_ESP32C3:
            return "ESP32-C3";

        default:
            return "Unknown";
    }
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

        heap_caps_free(
            task_status
        );
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

    heap_caps_free(
        task_status
    );

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

    uint32_t psram_free = 0U;
    uint32_t psram_minimum_free = 0U;

    if (esp_psram_is_initialized()) {
        psram_free =
            (uint32_t)heap_caps_get_free_size(
                MALLOC_CAP_SPIRAM |
                MALLOC_CAP_8BIT
            );

        psram_minimum_free =
            (uint32_t)heap_caps_get_minimum_free_size(
                MALLOC_CAP_SPIRAM |
                MALLOC_CAP_8BIT
            );
    }

    const uint8_t cpu_usage =
        system_service_get_cpu_usage();

    esp_err_t result =
        system_model_set_runtime(
            uptime_sec,
            free_heap,
            minimum_free_heap,
            psram_free,
            psram_minimum_free,
            cpu_usage
        );

    if (result != ESP_OK) {
        return result;
    }

    ++s_temperature_update_counter;

    if (s_temperature_update_counter >=
        SYSTEM_TEMPERATURE_UPDATE_INTERVAL) {

        s_temperature_update_counter = 0U;

        result =
            system_service_update_temperature();

        if (result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Failed to update chip temperature: %s",
                esp_err_to_name(result)
            );
        }
    }

    system_service_update_can_fd_profile();

    return ESP_OK;
}

static void system_service_task(
    void *argument
)
{
    (void)argument;

#if CAN_FD_MCP2518FD_ENABLE_PROFILING

    memset(
        &s_previous_can_fd_profile,
        0,
        sizeof(s_previous_can_fd_profile)
    );

    s_previous_can_fd_profile_time_us = 0;
    s_can_fd_profile_update_counter = 0U;
    s_previous_can_fd_profile_valid = false;

#endif

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

        const UBaseType_t watermark =
            uxTaskGetStackHighWaterMark(
                NULL
            );

        if (watermark < 512U) {
            ESP_LOGW(
                TAG,
                "System task stack is low: %u bytes",
                (unsigned int)watermark
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

    system_service_temperature_deinit();

    (void)system_model_set_chip_temperature(
        0.0F,
        false
    );

    ESP_LOGI(
        TAG,
        "System service stopped"
    );

    /*
     * Publish completion only after all service resources have been
     * released. The stopping task remains responsible for clearing
     * s_task_handle.
     */
    if (s_events != NULL) {
        (void)xEventGroupSetBits(
            s_events,
            SYSTEM_EVENT_STOPPED
        );
    }

    vTaskDeleteWithCaps(
        NULL
    );
}

esp_err_t system_service_start(void)
{
    if (s_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result =
        system_service_create_resources();

    if (result != ESP_OK) {
        return result;
    }

    (void)xEventGroupClearBits(
        s_events,
        SYSTEM_EVENT_STOPPED
    );

    system_model_t model = {0};

    result =
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

    esp_chip_info_t chip_info = {0};

    esp_chip_info(
        &chip_info
    );

    (void)strlcpy(
        model.chip_model,
        system_service_chip_model_to_string(
            chip_info.model
        ),
        sizeof(model.chip_model)
    );

    model.chip_cores =
        chip_info.cores;

    model.chip_revision =
        (uint16_t)chip_info.revision;

    model.cpu_frequency_mhz =
        CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;

    model.reset_reason =
        esp_reset_reason();

    uint32_t flash_size = 0U;

    const esp_err_t flash_result =
        esp_flash_get_size(
            NULL,
            &flash_size
        );

    if (flash_result == ESP_OK) {
        model.flash_size =
            flash_size;
    } else {
        ESP_LOGW(
            TAG,
            "Failed to read Flash size: %s",
            esp_err_to_name(flash_result)
        );
    }

    if (esp_psram_is_initialized()) {
        model.psram_size =
            (uint32_t)esp_psram_get_size();

        model.psram_free =
            (uint32_t)heap_caps_get_free_size(
                MALLOC_CAP_SPIRAM |
                MALLOC_CAP_8BIT
            );

        model.psram_minimum_free =
            (uint32_t)heap_caps_get_minimum_free_size(
                MALLOC_CAP_SPIRAM |
                MALLOC_CAP_8BIT
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

    const esp_err_t temperature_result =
        system_service_temperature_init();

    if (temperature_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Chip temperature sensor is unavailable: %s",
            esp_err_to_name(temperature_result)
        );

        (void)system_model_set_chip_temperature(
            0.0F,
            false
        );
    }

    s_temperature_update_counter =
        SYSTEM_TEMPERATURE_UPDATE_INTERVAL;

    system_service_reset_cpu_usage();

    const BaseType_t task_result =
        xTaskCreateWithCaps(
            system_service_task,
            "system_task",
            SYSTEM_TASK_STACK_SIZE,
            NULL,
            SYSTEM_TASK_PRIORITY,
            &s_task_handle,
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

    if (task_result != pdPASS) {
        s_task_handle = NULL;

        system_service_temperature_deinit();

        (void)system_model_set_chip_temperature(
            0.0F,
            false
        );

        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "System service started"
    );

    return ESP_OK;
}

esp_err_t system_service_stop(void)
{
    TaskHandle_t task_handle =
        s_task_handle;

    if ((task_handle == NULL) ||
        (s_events == NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    (void)xTaskNotifyGive(
        task_handle
    );

    const EventBits_t bits =
        xEventGroupWaitBits(
            s_events,
            SYSTEM_EVENT_STOPPED,
            pdTRUE,
            pdTRUE,
            pdMS_TO_TICKS(
                SYSTEM_STOP_TIMEOUT_MS
            )
        );

    if ((bits &
         SYSTEM_EVENT_STOPPED) == 0U) {

        ESP_LOGE(
            TAG,
            "Timed out waiting for system task"
        );

        return ESP_ERR_TIMEOUT;
    }

    /*
     * Keep the handle valid until shutdown is acknowledged so a
     * concurrent start request cannot create a second system task.
     */
    s_task_handle = NULL;

    return ESP_OK;
}

esp_err_t system_service_schedule_restart(
    uint32_t delay_ms
)
{
    if (delay_ms >
        SYSTEM_RESTART_DELAY_MAX_MS) {

        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t result =
        shutdown_service_schedule_restart(
            delay_ms
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to schedule graceful restart: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    ESP_LOGW(
        TAG,
        "Graceful device restart scheduled in %u ms",
        (unsigned int)delay_ms
    );

    return ESP_OK;
}
