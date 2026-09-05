/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "battery_service.h"

#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_task_priorities.h"
#include "battery_adc_driver.h"

#define BATTERY_SERVICE_UPDATE_INTERVAL_MS  (5000U)
#define BATTERY_SERVICE_LOCK_TIMEOUT_MS     (100U)
#define BATTERY_SERVICE_STOP_TIMEOUT_MS     (2000U)

#define BATTERY_SERVICE_TASK_STACK_SIZE     (2304U)
#define BATTERY_SERVICE_TASK_PRIORITY \
    APP_TASK_PRIORITY_BATTERY

#define BATTERY_SERVICE_PRESENT_MIN_MV      (2500U)

#define BATTERY_SERVICE_FILTER_OLD_WEIGHT   (3U)
#define BATTERY_SERVICE_FILTER_NEW_WEIGHT   (1U)
#define BATTERY_SERVICE_FILTER_WEIGHT_SUM   \
    (                                           \
        BATTERY_SERVICE_FILTER_OLD_WEIGHT +    \
        BATTERY_SERVICE_FILTER_NEW_WEIGHT      \
    )

#define BATTERY_SERVICE_STOP_REQUESTED_BIT  BIT0
#define BATTERY_SERVICE_STOPPED_BIT         BIT1

_Static_assert(
    BATTERY_SERVICE_FILTER_WEIGHT_SUM > 0U,
    "Battery filter weight sum must be greater than zero"
);

typedef struct
{
    uint16_t voltage_mv;
    uint8_t level_percent;

} battery_level_point_t;

static const battery_level_point_t s_level_table[] = {
    { 2850U,   0U },
    { 3000U,   2U },
    { 3200U,   5U },
    { 3300U,  10U },
    { 3400U,  15U },
    { 3500U,  22U },
    { 3600U,  30U },
    { 3700U,  42U },
    { 3800U,  55U },
    { 3900U,  70U },
    { 4000U,  82U },
    { 4100U,  92U },
    { 4200U, 100U },
};

_Static_assert(
    sizeof(s_level_table) /
    sizeof(s_level_table[0]) >= 2U,
    "Battery level table must contain at least two points"
);

static const char *TAG =
    "battery_service";

static SemaphoreHandle_t s_mutex = NULL;
static EventGroupHandle_t s_events = NULL;
static TaskHandle_t s_task = NULL;

static atomic_bool s_running =
    ATOMIC_VAR_INIT(false);

static battery_service_info_t s_info;

static esp_err_t battery_service_lock(void)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_mutex,
            pdMS_TO_TICKS(
                BATTERY_SERVICE_LOCK_TIMEOUT_MS
            )
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void battery_service_unlock(void)
{
    (void)xSemaphoreGive(
        s_mutex
    );
}

static uint64_t battery_service_get_time_ms(void)
{
    return (uint64_t)(
        esp_timer_get_time() /
        1000LL
    );
}

static uint8_t battery_service_interpolate_level(
    uint16_t voltage_mv,
    const battery_level_point_t *lower,
    const battery_level_point_t *upper
)
{
    if ((lower == NULL) ||
        (upper == NULL) ||
        (upper->voltage_mv <= lower->voltage_mv)) {

        return 0U;
    }

    const uint32_t voltage_offset =
        (uint32_t)voltage_mv -
        (uint32_t)lower->voltage_mv;

    const uint32_t voltage_range =
        (uint32_t)upper->voltage_mv -
        (uint32_t)lower->voltage_mv;

    const uint32_t level_range =
        (uint32_t)upper->level_percent -
        (uint32_t)lower->level_percent;

    return (uint8_t)(
        (uint32_t)lower->level_percent +
        (
            (voltage_offset * level_range) /
            voltage_range
        )
    );
}

static uint8_t battery_service_calculate_level(
    uint16_t voltage_mv
)
{
    const size_t point_count =
        sizeof(s_level_table) /
        sizeof(s_level_table[0]);

    if (voltage_mv <=
        s_level_table[0].voltage_mv) {

        return s_level_table[0].level_percent;
    }

    if (voltage_mv >=
        s_level_table[
            point_count - 1U
        ].voltage_mv) {

        return s_level_table[
            point_count - 1U
        ].level_percent;
    }

    for (size_t index = 1U;
         index < point_count;
         ++index) {

        if (voltage_mv <=
            s_level_table[index].voltage_mv) {

            return battery_service_interpolate_level(
                voltage_mv,
                &s_level_table[index - 1U],
                &s_level_table[index]
            );
        }
    }

    return 100U;
}

static uint16_t battery_service_filter_voltage(
    uint16_t previous_mv,
    uint16_t measured_mv
)
{
    const uint32_t filtered =
        (
            ((uint32_t)previous_mv *
             BATTERY_SERVICE_FILTER_OLD_WEIGHT) +
            ((uint32_t)measured_mv *
             BATTERY_SERVICE_FILTER_NEW_WEIGHT) +
            (
                BATTERY_SERVICE_FILTER_WEIGHT_SUM /
                2U
            )
        ) /
        BATTERY_SERVICE_FILTER_WEIGHT_SUM;

    return (uint16_t)filtered;
}

static void battery_service_update(void)
{
    uint16_t measured_voltage_mv = 0U;

    const esp_err_t result =
        battery_adc_driver_get_voltage_mv(
            &measured_voltage_mv
        );

    if (result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to read battery voltage: %s",
            esp_err_to_name(result)
        );

        return;
    }

    const esp_err_t lock_result =
        battery_service_lock();

    if (lock_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to update battery information: %s",
            esp_err_to_name(lock_result)
        );

        return;
    }

    if (!s_info.measurement_valid) {
        s_info.voltage_mv =
            measured_voltage_mv;
    } else {
        s_info.voltage_mv =
            battery_service_filter_voltage(
                s_info.voltage_mv,
                measured_voltage_mv
            );
    }

    s_info.measurement_valid = true;

    s_info.battery_present =
        s_info.voltage_mv >=
        BATTERY_SERVICE_PRESENT_MIN_MV;

    if (s_info.battery_present) {
        s_info.level_percent =
            battery_service_calculate_level(
                s_info.voltage_mv
            );
    } else {
        s_info.level_percent = 0U;
    }

    s_info.last_update_ms =
        battery_service_get_time_ms();

    battery_service_unlock();
}

static void battery_service_task(
    void *argument
)
{
    (void)argument;

    while (true) {
        battery_service_update();

        const UBaseType_t watermark =
            uxTaskGetStackHighWaterMark(
                NULL
            );

        if (watermark < 512U) {
            ESP_LOGW(
                TAG,
                "Battery task stack is low: %u bytes",
                (unsigned int)watermark
            );
        }

        const EventBits_t bits =
            xEventGroupWaitBits(
                s_events,
                BATTERY_SERVICE_STOP_REQUESTED_BIT,
                pdFALSE,
                pdFALSE,
                pdMS_TO_TICKS(
                    BATTERY_SERVICE_UPDATE_INTERVAL_MS
                )
            );

        if ((bits &
             BATTERY_SERVICE_STOP_REQUESTED_BIT) != 0U) {

            break;
        }
    }

    /*
     * No ADC or shared service resources may be accessed after the
     * stopped bit is published. The stopping task may release them
     * immediately after receiving this notification.
     */
    (void)xEventGroupSetBits(
        s_events,
        BATTERY_SERVICE_STOPPED_BIT
    );

    vTaskDeleteWithCaps(
        NULL
    );
}

static esp_err_t battery_service_create_resources(void)
{
    if (s_mutex == NULL) {
        s_mutex =
            xSemaphoreCreateMutex();

        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_events == NULL) {
        s_events =
            xEventGroupCreate();

        if (s_events == NULL) {
            /*
             * Keep an existing mutex because the service may be
             * started again later.
             */
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

esp_err_t battery_service_start(void)
{
    if (atomic_load(&s_running)) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result =
        battery_service_create_resources();

    if (result != ESP_OK) {
        return result;
    }

    memset(
        &s_info,
        0,
        sizeof(s_info)
    );

    (void)xEventGroupClearBits(
        s_events,
        BATTERY_SERVICE_STOP_REQUESTED_BIT |
        BATTERY_SERVICE_STOPPED_BIT
    );

    result =
        battery_adc_driver_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize battery ADC: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    const BaseType_t task_result =
        xTaskCreateWithCaps(
            battery_service_task,
            "battery_service",
            BATTERY_SERVICE_TASK_STACK_SIZE,
            NULL,
            BATTERY_SERVICE_TASK_PRIORITY,
            &s_task,
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

    if (task_result != pdPASS) {
        s_task = NULL;

        (void)battery_adc_driver_deinit();

        return ESP_ERR_NO_MEM;
    }

    atomic_store(
        &s_running,
        true
    );

    ESP_LOGI(
        TAG,
        "Battery monitoring started"
    );

    return ESP_OK;
}

esp_err_t battery_service_stop(void)
{
    if (!atomic_load(&s_running) ||
        (s_task == NULL) ||
        (s_events == NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    (void)xEventGroupSetBits(
        s_events,
        BATTERY_SERVICE_STOP_REQUESTED_BIT
    );

    const EventBits_t bits =
        xEventGroupWaitBits(
            s_events,
            BATTERY_SERVICE_STOPPED_BIT,
            pdFALSE,
            pdTRUE,
            pdMS_TO_TICKS(
                BATTERY_SERVICE_STOP_TIMEOUT_MS
            )
        );

    if ((bits &
         BATTERY_SERVICE_STOPPED_BIT) == 0U) {

        ESP_LOGE(
            TAG,
            "Timed out waiting for battery task"
        );

        return ESP_ERR_TIMEOUT;
    }

    const esp_err_t result =
        battery_adc_driver_deinit();

    s_task = NULL;

    atomic_store(
        &s_running,
        false
    );

    if (result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to deinitialize battery ADC: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    ESP_LOGI(
        TAG,
        "Battery monitoring stopped"
    );

    return ESP_OK;
}

esp_err_t battery_service_get_info(
    battery_service_info_t *info
)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(
        info,
        0,
        sizeof(*info)
    );

    if (!atomic_load(&s_running)) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result =
        battery_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    *info = s_info;

    battery_service_unlock();

    return ESP_OK;
}

bool battery_service_is_running(void)
{
    return atomic_load(
        &s_running
    );
}
