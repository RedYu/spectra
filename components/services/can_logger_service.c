/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "can_logger_service.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_task_priorities.h"
#include "can_router.h"
#include "storage_sd_service.h"
#include "can_logger_binary_format.h"
#include "time_service.h"

#define CAN_LOGGER_EVENT_QUEUE_LENGTH       (2048U)
#define CAN_LOGGER_COMMAND_QUEUE_LENGTH     (4U)
#define CAN_LOGGER_WRITE_BUFFER_SIZE        (16U * 1024U)
#define CAN_LOGGER_SD_WRITE_CHUNK_SIZE      (4U * 1024U)
#define CAN_LOGGER_TASK_STACK_SIZE          (6144U)
#define CAN_LOGGER_TASK_PRIORITY            APP_TASK_PRIORITY_CAN_LOGGER
#define CAN_LOGGER_SYNC_INTERVAL_MS         (5000U)
#define CAN_LOGGER_COMMAND_TIMEOUT_MS       (100U)
#define CAN_LOGGER_STOP_TIMEOUT_MS          (10000U)
#define CAN_LOGGER_TASK_POLL_MS             (20U)
#define CAN_LOGGER_LINE_MAX_LENGTH          (512U)
#define CAN_LOGGER_PROCESS_BATCH_MAX        (32U)

#define CAN_LOGGER_EVENT_RECORDING_STOPPED  BIT0
#define CAN_LOGGER_EVENT_TASK_STOPPED       BIT1

typedef enum
{
    CAN_LOGGER_COMMAND_START = 0,
    CAN_LOGGER_COMMAND_STOP,
    CAN_LOGGER_COMMAND_SHUTDOWN,

} can_logger_command_type_t;

typedef struct
{
    can_logger_command_type_t type;
    can_logger_recording_config_t config;

} can_logger_command_t;

typedef struct
{
    can_event_t event;
    uint64_t captured_at_us;

} can_logger_queue_item_t;

typedef struct
{
    FILE *file;
    char *buffer;
    size_t buffer_used;

    can_logger_format_t format;

    uint64_t session_started_us;
    TickType_t last_sync_tick;

} can_logger_writer_t;

static const char *TAG = "can_logger_service";

static TaskHandle_t s_task = NULL;
static QueueHandle_t s_event_queue = NULL;
static QueueHandle_t s_command_queue = NULL;
static SemaphoreHandle_t s_mutex = NULL;
static EventGroupHandle_t s_events = NULL;

static StaticQueue_t s_event_queue_control;
static void *s_event_queue_storage = NULL;

static uint32_t s_router_subscription_id =
    CAN_ROUTER_SUBSCRIPTION_ID_NONE;

static can_logger_info_t s_info;

static atomic_bool s_running = ATOMIC_VAR_INIT(false);
static atomic_bool s_accepting_events = ATOMIC_VAR_INIT(false);
static atomic_uint_fast32_t s_bus_mask = ATOMIC_VAR_INIT(0U);
static atomic_uint_fast32_t s_direction_mask = ATOMIC_VAR_INIT(0U);
static atomic_uint_fast32_t s_callback_count = ATOMIC_VAR_INIT(0U);

static atomic_uint_fast64_t s_received_events = ATOMIC_VAR_INIT(0U);
static atomic_uint_fast64_t s_filtered_events = ATOMIC_VAR_INIT(0U);
static atomic_uint_fast64_t s_queued_events = ATOMIC_VAR_INIT(0U);
static atomic_uint_fast64_t s_dropped_events = ATOMIC_VAR_INIT(0U);
static atomic_uint_fast64_t s_written_events = ATOMIC_VAR_INIT(0U);
static atomic_uint_fast64_t s_written_bytes = ATOMIC_VAR_INIT(0U);
static atomic_uint_fast64_t s_serialization_failures = ATOMIC_VAR_INIT(0U);
static atomic_uint_fast64_t s_write_failures = ATOMIC_VAR_INIT(0U);
static atomic_uint_fast64_t s_sync_failures = ATOMIC_VAR_INIT(0U);
static atomic_uint_fast32_t s_queue_peak = ATOMIC_VAR_INIT(0U);

static esp_err_t can_logger_writer_append(
    can_logger_writer_t *writer,
    const void *data,
    size_t size
);

static esp_err_t can_logger_lock(void)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_mutex,
            pdMS_TO_TICKS(CAN_LOGGER_COMMAND_TIMEOUT_MS)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void can_logger_unlock(void)
{
    if (s_mutex != NULL) {
        (void)xSemaphoreGive(s_mutex);
    }
}

static void can_logger_update_queue_peak(void)
{
    if (s_event_queue == NULL) {
        return;
    }

    const uint_fast32_t current =
        (uint_fast32_t)uxQueueMessagesWaiting(s_event_queue);

    uint_fast32_t observed = atomic_load_explicit(
        &s_queue_peak,
        memory_order_relaxed
    );

    while (current > observed) {
        if (atomic_compare_exchange_weak_explicit(
                &s_queue_peak,
                &observed,
                current,
                memory_order_relaxed,
                memory_order_relaxed
            )) {

            break;
        }
    }
}

static void can_logger_reset_statistics_internal(void)
{
    atomic_store(&s_received_events, 0U);
    atomic_store(&s_filtered_events, 0U);
    atomic_store(&s_queued_events, 0U);
    atomic_store(&s_dropped_events, 0U);
    atomic_store(&s_written_events, 0U);
    atomic_store(&s_written_bytes, 0U);
    atomic_store(&s_serialization_failures, 0U);
    atomic_store(&s_write_failures, 0U);
    atomic_store(&s_sync_failures, 0U);
    atomic_store(&s_queue_peak, 0U);
}

static bool can_logger_config_valid(
    const can_logger_recording_config_t *config
)
{
    if (config == NULL) {
        return false;
    }

    if ((config->format < CAN_LOGGER_FORMAT_ASC) ||
        (config->format >= CAN_LOGGER_FORMAT_COUNT)) {

        return false;
    }

    if ((!config->filter.primary &&
         !config->filter.secondary) ||
        (!config->filter.rx &&
         !config->filter.tx)) {

        return false;
    }

    const char *terminator = memchr(
        config->file_path,
        '\0',
        sizeof(config->file_path)
    );

    if ((terminator == NULL) ||
        (config->file_path[0] != '/') ||
        (strstr(config->file_path, "..") != NULL)) {

        return false;
    }

    return true;
}

static uint32_t can_logger_filter_bus_mask(
    const can_logger_filter_t *filter
)
{
    uint32_t mask = 0U;

    if (filter->primary) {
        mask |= CAN_ROUTER_BUS_MASK(CAN_BUS_PRIMARY);
    }

    if (filter->secondary) {
        mask |= CAN_ROUTER_BUS_MASK(CAN_BUS_SECONDARY);
    }

    return mask;
}

static uint32_t can_logger_filter_direction_mask(
    const can_logger_filter_t *filter
)
{
    uint32_t mask = 0U;

    if (filter->rx) {
        mask |= 1UL << (uint32_t)CAN_FRAME_DIRECTION_RX;
    }

    if (filter->tx) {
        mask |= 1UL << (uint32_t)CAN_FRAME_DIRECTION_TX;
    }

    return mask;
}

static void can_logger_router_callback(
    const can_event_t *event,
    void *context
)
{
    (void)context;

    (void)atomic_fetch_add_explicit(
        &s_callback_count,
        1U,
        memory_order_acquire
    );

    if ((event == NULL) ||
        !atomic_load_explicit(
            &s_accepting_events,
            memory_order_acquire
        )) {

        (void)atomic_fetch_sub_explicit(
            &s_callback_count,
            1U,
            memory_order_release
        );
        return;
    }

    (void)atomic_fetch_add_explicit(
        &s_received_events,
        1U,
        memory_order_relaxed
    );

    const uint32_t bus_bit =
        CAN_ROUTER_BUS_MASK(event->frame.bus);

    const uint32_t direction_bit =
        1UL << (uint32_t)event->direction;

    if (((atomic_load_explicit(
              &s_bus_mask,
              memory_order_relaxed
          ) & bus_bit) == 0U) ||
        ((atomic_load_explicit(
              &s_direction_mask,
              memory_order_relaxed
          ) & direction_bit) == 0U)) {

        (void)atomic_fetch_add_explicit(
            &s_filtered_events,
            1U,
            memory_order_relaxed
        );

        (void)atomic_fetch_sub_explicit(
            &s_callback_count,
            1U,
            memory_order_release
        );
        return;
    }

    const can_logger_queue_item_t item = {
        .event = *event,
        .captured_at_us = (uint64_t)esp_timer_get_time(),
    };

    if ((s_event_queue == NULL) ||
        (xQueueSend(s_event_queue, &item, 0U) != pdTRUE)) {

        (void)atomic_fetch_add_explicit(
            &s_dropped_events,
            1U,
            memory_order_relaxed
        );
    } else {
        const uint_fast64_t queued =
            atomic_fetch_add_explicit(
                &s_queued_events,
                1U,
                memory_order_relaxed
            ) + 1U;

        if ((queued == 1U) ||
            ((queued % CAN_LOGGER_PROCESS_BATCH_MAX) == 0U)) {

            can_logger_update_queue_peak();
        }
    }

    (void)atomic_fetch_sub_explicit(
        &s_callback_count,
        1U,
        memory_order_release
    );
}

static esp_err_t can_logger_writer_flush_buffer(
    can_logger_writer_t *writer
)
{
    if ((writer == NULL) ||
        (writer->file == NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    if (writer->buffer_used == 0U) {
        return ESP_OK;
    }

    size_t offset = 0U;

    while (offset < writer->buffer_used) {
        const size_t remaining =
            writer->buffer_used - offset;

        const size_t chunk_size =
            remaining >
            CAN_LOGGER_SD_WRITE_CHUNK_SIZE
                ? CAN_LOGGER_SD_WRITE_CHUNK_SIZE
                : remaining;

        size_t written = 0U;

        const esp_err_t result =
            storage_sd_service_write(
                writer->file,
                writer->buffer + offset,
                chunk_size,
                &written
            );

        if (result != ESP_OK) {
            (void)atomic_fetch_add_explicit(
                &s_write_failures,
                1U,
                memory_order_relaxed
            );

            return result;
        }

        if (written != chunk_size) {
            (void)atomic_fetch_add_explicit(
                &s_write_failures,
                1U,
                memory_order_relaxed
            );

            return ESP_FAIL;
        }

        offset += written;

        if (offset < writer->buffer_used) {
            /*
             * Temporarily block the logger so lower-priority LCD work can
             * acquire the shared SPI bus between SD write blocks.
             */
            vTaskDelay(1U);
        }
    }

    (void)atomic_fetch_add_explicit(
        &s_written_bytes,
        (uint_fast64_t)writer->buffer_used,
        memory_order_relaxed
    );

    writer->buffer_used = 0U;

    return ESP_OK;
}

static esp_err_t can_logger_write_scl_header(
    can_logger_writer_t *writer
)
{
    if (writer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t start_unix_us = 0U;

    if (time_service_time_valid()) {
        const time_t now = time(NULL);

        if (now > 0) {
            start_unix_us =
                (uint64_t)now * UINT64_C(1000000);
        }
    }

    uint8_t header[
        CAN_LOGGER_SCL_FILE_HEADER_SIZE
    ];

    size_t encoded_size = 0U;

    esp_err_t result =
        can_logger_scl_encode_file_header(
            header,
            sizeof(header),
            start_unix_us,
            writer->session_started_us,
            &encoded_size
        );

    if (result != ESP_OK) {
        return result;
    }

    return can_logger_writer_append(
        writer,
        header,
        encoded_size
    );
}

static esp_err_t can_logger_serialize_scl_event(
    can_logger_writer_t *writer,
    const can_logger_queue_item_t *item
)
{
    if ((writer == NULL) ||
        (item == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    uint8_t record[
        CAN_LOGGER_SCL_EVENT_MAX_SIZE
    ];

    size_t encoded_size = 0U;

    esp_err_t result =
        can_logger_scl_encode_event(
            &item->event,
            item->captured_at_us,
            writer->session_started_us,
            record,
            sizeof(record),
            &encoded_size
        );

    if (result != ESP_OK) {
        return result;
    }

    return can_logger_writer_append(
        writer,
        record,
        encoded_size
    );
}

static esp_err_t can_logger_writer_append(
    can_logger_writer_t *writer,
    const void *data,
    size_t size
)
{
    if ((writer == NULL) ||
        (data == NULL) ||
        (size == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (size > CAN_LOGGER_WRITE_BUFFER_SIZE) {
        esp_err_t result =
            can_logger_writer_flush_buffer(writer);

        if (result != ESP_OK) {
            return result;
        }

        size_t written = 0U;
        result = storage_sd_service_write(
            writer->file,
            data,
            size,
            &written
        );

        if (result != ESP_OK) {
            (void)atomic_fetch_add_explicit(
                &s_write_failures, 
                1U, 
                memory_order_relaxed
            );

            return result;
        }

        (void)atomic_fetch_add_explicit(
            &s_written_bytes,
            (uint_fast64_t)written,
            memory_order_relaxed
        );
        return ESP_OK;
    }

    if ((writer->buffer_used + size) >
        CAN_LOGGER_WRITE_BUFFER_SIZE) {

        const esp_err_t result =
            can_logger_writer_flush_buffer(writer);

        if (result != ESP_OK) {
            return result;
        }
    }

    memcpy(
        writer->buffer + writer->buffer_used,
        data,
        size
    );
    writer->buffer_used += size;

    return ESP_OK;
}

static const char *can_logger_event_name(
    can_event_type_t type
)
{
    switch (type) {
        case CAN_EVENT_RX:
            return "RX";
        case CAN_EVENT_TX_QUEUED:
            return "TX_QUEUED";
        case CAN_EVENT_TX_COMPLETED:
            return "TX_COMPLETED";
        case CAN_EVENT_TX_FAILED:
            return "TX_FAILED";
        case CAN_EVENT_TX_ABORTED:
            return "TX_ABORTED";
        default:
            return "UNKNOWN";
    }
}

static esp_err_t can_logger_serialize_asc_event(
    can_logger_writer_t *writer,
    const can_logger_queue_item_t *item
)
{
    if ((writer == NULL) || (item == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    const can_event_t *event = &item->event;
    const can_frame_t *frame = &event->frame;

    char line[CAN_LOGGER_LINE_MAX_LENGTH];
    size_t used = 0U;

    const uint64_t elapsed_us =
        (item->captured_at_us >= writer->session_started_us)
            ? item->captured_at_us - writer->session_started_us
            : 0U;

    const unsigned long seconds =
        (unsigned long)(elapsed_us / 1000000U);
    const unsigned long microseconds =
        (unsigned long)(elapsed_us % 1000000U);
    const unsigned int channel =
        (frame->bus == CAN_BUS_PRIMARY) ? 1U : 2U;
    const bool extended =
        (frame->flags & CAN_FRAME_FLAG_EXTENDED_ID) != 0U;
    const bool remote =
        (frame->flags & CAN_FRAME_FLAG_REMOTE) != 0U;
    const bool fd =
        (frame->flags & CAN_FRAME_FLAG_FD) != 0U;
    const char *direction =
        (event->direction == CAN_FRAME_DIRECTION_RX) ? "Rx" : "Tx";

    int result = 0;

    if ((event->type == CAN_EVENT_TX_FAILED) ||
        (event->type == CAN_EVENT_TX_ABORTED) ||
        (event->type == CAN_EVENT_TX_QUEUED)) {

        result = snprintf(
            line,
            sizeof(line),
            "// %lu.%06lu %s bus=%u id=%X%s transaction=%" PRIu32
            " native=%" PRIu32 " result=%ld\n",
            seconds,
            microseconds,
            can_logger_event_name(event->type),
            channel,
            (unsigned int)frame->identifier,
            extended ? "x" : "",
            event->transaction_id,
            event->native_sequence,
            (long)event->result
        );
    } else if (fd) {
        const unsigned int brs =
            (frame->flags & CAN_FRAME_FLAG_BRS) != 0U ? 1U : 0U;
        const unsigned int esi =
            (frame->flags & CAN_FRAME_FLAG_ESI) != 0U ? 1U : 0U;

        result = snprintf(
            line,
            sizeof(line),
            "%lu.%06lu CANFD %u %s %X%s %u %u %u %u",
            seconds,
            microseconds,
            channel,
            direction,
            (unsigned int)frame->identifier,
            extended ? "x" : "",
            brs,
            esi,
            (unsigned int)frame->dlc,
            (unsigned int)frame->data_length
        );
    } else {
        result = snprintf(
            line,
            sizeof(line),
            "%lu.%06lu %u %X%s %s %c %u",
            seconds,
            microseconds,
            channel,
            (unsigned int)frame->identifier,
            extended ? "x" : "",
            direction,
            remote ? 'r' : 'd',
            (unsigned int)frame->dlc
        );
    }

    if ((result < 0) || ((size_t)result >= sizeof(line))) {
        return ESP_ERR_INVALID_SIZE;
    }

    used = (size_t)result;

    if (!remote &&
        (event->type != CAN_EVENT_TX_QUEUED) &&
        (event->type != CAN_EVENT_TX_FAILED) &&
        (event->type != CAN_EVENT_TX_ABORTED)) {

        for (uint8_t index = 0U;
             index < frame->data_length;
             ++index) {

            result = snprintf(
                line + used,
                sizeof(line) - used,
                " %02X",
                frame->data[index]
            );

            if ((result < 0) ||
                ((size_t)result >= (sizeof(line) - used))) {

                return ESP_ERR_INVALID_SIZE;
            }

            used += (size_t)result;
        }
    }

    if ((used + 1U) >= sizeof(line)) {
        return ESP_ERR_INVALID_SIZE;
    }

    line[used++] = '\n';

    return can_logger_writer_append(writer, line, used);
}

static esp_err_t can_logger_write_asc_header(
    can_logger_writer_t *writer
)
{
    time_t now = time(NULL);
    struct tm time_info;
    char date[64] = "Thu Jan 01 00:00:00 1970";

    if (localtime_r(&now, &time_info) != NULL) {
        (void)strftime(
            date,
            sizeof(date),
            "%a %b %d %H:%M:%S %Y",
            &time_info
        );
    }

    char header[256];
    const int length = snprintf(
        header,
        sizeof(header),
        "date %s\n"
        "base hex  timestamps absolute\n"
        "internal events logged\n"
        "Begin Triggerblock\n",
        date
    );

    if ((length < 0) || ((size_t)length >= sizeof(header))) {
        return ESP_ERR_INVALID_SIZE;
    }

    return can_logger_writer_append(
        writer,
        header,
        (size_t)length
    );
}

static esp_err_t can_logger_writer_flush(
    can_logger_writer_t *writer
)
{
    esp_err_t result =
        can_logger_writer_flush_buffer(
            writer
        );

    if (result == ESP_OK) {
        result =
            storage_sd_service_flush(
                writer->file
            );
    }

    if (result != ESP_OK) {
        (void)atomic_fetch_add_explicit(
            &s_sync_failures,
            1U,
            memory_order_relaxed
        );
    } else {
        writer->last_sync_tick =
            xTaskGetTickCount();
    }

    return result;
}

static void can_logger_set_terminal_state(
    can_logger_state_t state,
    esp_err_t error
)
{
    if (can_logger_lock() == ESP_OK) {
        s_info.state = state;
        s_info.last_error = error;
        s_info.started_at_us = 0U;
        s_info.duration_ms = 0U;

        if (state == CAN_LOGGER_STATE_IDLE) {
            s_info.file_path[0] = '\0';
        }

        can_logger_unlock();
    }

    if (s_events != NULL) {
        (void)xEventGroupSetBits(
            s_events,
            CAN_LOGGER_EVENT_RECORDING_STOPPED
        );
    }
}

static esp_err_t can_logger_writer_open(
    can_logger_writer_t *writer,
    const can_logger_recording_config_t *config
)
{
    esp_err_t result =
        storage_sd_service_ensure_directory(
            "/logs/can"
        );

    if (result != ESP_OK) {
        return result;
    }

    result =
        storage_sd_service_open(
            config->file_path,
            "wx",
            &writer->file
        );

    if (result != ESP_OK) {
        return result;
    }

    writer->buffer_used = 0U;
    writer->format = config->format;
    writer->session_started_us =
        (uint64_t)esp_timer_get_time();
    writer->last_sync_tick =
        xTaskGetTickCount();

    switch (writer->format) {
        case CAN_LOGGER_FORMAT_ASC:
            result =
                can_logger_write_asc_header(
                    writer
                );
            break;

        case CAN_LOGGER_FORMAT_SCL:
            result =
                can_logger_write_scl_header(
                    writer
                );
            break;

        default:
            result = ESP_ERR_NOT_SUPPORTED;
            break;
    }

    if (result != ESP_OK) {
        (void)storage_sd_service_close(
            &writer->file
        );

        (void)storage_sd_service_remove(
            config->file_path
        );

        return result;
    }

    result = can_logger_lock();

    if (result != ESP_OK) {
        (void)storage_sd_service_close(
            &writer->file
        );

        (void)storage_sd_service_remove(
            config->file_path
        );

        return result;
    }

    s_info.state =
        CAN_LOGGER_STATE_RECORDING;

    s_info.started_at_us =
        writer->session_started_us;

    s_info.last_error = ESP_OK;

    can_logger_unlock();

    atomic_store(
        &s_bus_mask,
        can_logger_filter_bus_mask(&config->filter)
    );
    atomic_store(
        &s_direction_mask,
        can_logger_filter_direction_mask(&config->filter)
    );
    atomic_store_explicit(
        &s_accepting_events,
        true,
        memory_order_release
    );

    ESP_LOGI(
        TAG,
        "CAN recording started: path=%s, format=%s",
        config->file_path,
        config->format == CAN_LOGGER_FORMAT_SCL
            ? "SCL"
            : "ASC"
    );

    return ESP_OK;
}

static esp_err_t can_logger_writer_process_item(
    can_logger_writer_t *writer,
    const can_logger_queue_item_t *item
)
{
    if ((writer == NULL) ||
        (item == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result;

    switch (writer->format) {
        case CAN_LOGGER_FORMAT_ASC:
            result =
                can_logger_serialize_asc_event(
                    writer,
                    item
                );
            break;

        case CAN_LOGGER_FORMAT_SCL:
            result =
                can_logger_serialize_scl_event(
                    writer,
                    item
                );
            break;

        default:
            result = ESP_ERR_NOT_SUPPORTED;
            break;
    }

    if (result != ESP_OK) {
        (void)atomic_fetch_add_explicit(
            &s_serialization_failures,
            1U,
            memory_order_relaxed
        );

        return result;
    }

    (void)atomic_fetch_add_explicit(
        &s_written_events,
        1U,
        memory_order_relaxed
    );

    return ESP_OK;
}

static esp_err_t can_logger_writer_close(
    can_logger_writer_t *writer
)
{
    if ((writer == NULL) ||
        (writer->file == NULL)) {

        return ESP_OK;
    }

    esp_err_t result = ESP_OK;

    if (writer->format ==
        CAN_LOGGER_FORMAT_ASC) {

        static const char trailer[] =
            "End TriggerBlock\n";

        result =
            can_logger_writer_append(
                writer,
                trailer,
                sizeof(trailer) - 1U
            );
    }

    if (result == ESP_OK) {
        result =
            can_logger_writer_flush_buffer(
                writer
            );
    }

    if (result == ESP_OK) {
        result =
            storage_sd_service_sync(
                writer->file
            );
    }

    const esp_err_t close_result =
        storage_sd_service_close(
            &writer->file
        );

    if (result == ESP_OK) {
        result = close_result;
    }

    writer->buffer_used = 0U;
    writer->format = CAN_LOGGER_FORMAT_ASC;

    return result;
}

static void can_logger_wait_callbacks(void)
{
    while (atomic_load_explicit(
               &s_callback_count,
               memory_order_acquire
           ) != 0U) {

        taskYIELD();
    }
}

static esp_err_t can_logger_finish_recording(
    can_logger_writer_t *writer
)
{
    atomic_store_explicit(
        &s_accepting_events,
        false,
        memory_order_release
    );

    can_logger_wait_callbacks();

    can_logger_queue_item_t item;
    esp_err_t first_error = ESP_OK;

    while (xQueueReceive(s_event_queue, &item, 0U) == pdTRUE) {
        const esp_err_t result =
            can_logger_writer_process_item(writer, &item);

        if ((first_error == ESP_OK) && (result != ESP_OK)) {
            first_error = result;
        }
    }

    const esp_err_t close_result =
        can_logger_writer_close(writer);

    if (first_error == ESP_OK) {
        first_error = close_result;
    }

    if (first_error == ESP_OK) {
        can_logger_set_terminal_state(
            CAN_LOGGER_STATE_IDLE,
            ESP_OK
        );
        ESP_LOGI(TAG, "CAN recording stopped");
    } else {
        can_logger_set_terminal_state(
            CAN_LOGGER_STATE_ERROR,
            first_error
        );
        ESP_LOGE(
            TAG,
            "CAN recording failed: %s",
            esp_err_to_name(first_error)
        );
    }

    return first_error;
}

static void can_logger_fail_recording(
    can_logger_writer_t *writer,
    esp_err_t error
)
{
    atomic_store(&s_accepting_events, false);
    can_logger_wait_callbacks();
    (void)xQueueReset(s_event_queue);

    if (writer->file != NULL) {
        (void)storage_sd_service_close(&writer->file);
    }

    writer->buffer_used = 0U;
    can_logger_set_terminal_state(CAN_LOGGER_STATE_ERROR, error);

    ESP_LOGE(TAG, "CAN logger session error: %s", esp_err_to_name(error));
}

static void can_logger_task(void *argument)
{
    (void)argument;

    can_logger_writer_t writer = {
        .buffer = heap_caps_malloc(
            CAN_LOGGER_WRITE_BUFFER_SIZE,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        ),
    };

    if (writer.buffer == NULL) {
        can_logger_set_terminal_state(
            CAN_LOGGER_STATE_ERROR,
            ESP_ERR_NO_MEM
        );
    }

    bool shutdown = false;

    while (!shutdown) {
        can_logger_command_t command;
        const bool file_open = writer.file != NULL;

        if (xQueueReceive(
                s_command_queue,
                &command,
                file_open ? 0U : portMAX_DELAY
            ) == pdTRUE) {

            switch (command.type) {
                case CAN_LOGGER_COMMAND_START: {
                    if (writer.buffer == NULL) {
                        can_logger_set_terminal_state(
                            CAN_LOGGER_STATE_ERROR,
                            ESP_ERR_NO_MEM
                        );
                        break;
                    }

                    (void)xQueueReset(s_event_queue);

                    const esp_err_t result =
                        can_logger_writer_open(
                            &writer,
                            &command.config
                        );

                    if (result != ESP_OK) {
                        can_logger_fail_recording(&writer, result);
                    }
                    break;
                }

                case CAN_LOGGER_COMMAND_STOP:
                    if (writer.file != NULL) {
                        (void)can_logger_finish_recording(&writer);
                    }
                    break;

                case CAN_LOGGER_COMMAND_SHUTDOWN:
                    if (writer.file != NULL) {
                        (void)can_logger_finish_recording(&writer);
                    }
                    shutdown = true;
                    break;

                default:
                    break;
            }

            continue;
        }

        if (writer.file == NULL) {
            continue;
        }

        bool recording_failed = false;

        for (size_t index = 0U;
            index < CAN_LOGGER_PROCESS_BATCH_MAX;
            ++index) {

            can_logger_queue_item_t item;

            const TickType_t wait_ticks =
                index == 0U
                    ? pdMS_TO_TICKS(
                        CAN_LOGGER_TASK_POLL_MS
                    )
                    : 0U;

            if (xQueueReceive(
                    s_event_queue,
                    &item,
                    wait_ticks
                ) != pdTRUE) {

                break;
            }

            const esp_err_t result =
                can_logger_writer_process_item(
                    &writer,
                    &item
                );

            if (result != ESP_OK) {
                can_logger_fail_recording(
                    &writer,
                    result
                );

                recording_failed = true;
                break;
            }
        }

        if (recording_failed) {
            continue;
        }

        const TickType_t now = xTaskGetTickCount();

        if ((now - writer.last_sync_tick) >=
            pdMS_TO_TICKS(CAN_LOGGER_SYNC_INTERVAL_MS)) {

            const esp_err_t result =
                can_logger_writer_flush(
                    &writer
                );

            if (result != ESP_OK) {
                can_logger_fail_recording(&writer, result);
            }
        }
    }

    free(writer.buffer);

    if (s_events != NULL) {
        (void)xEventGroupSetBits(
            s_events,
            CAN_LOGGER_EVENT_TASK_STOPPED
        );
    }

    /*
     * The task stack was allocated by xTaskCreateWithCaps().
     */
    vTaskDeleteWithCaps(NULL);
}

static void can_logger_release_resources(void)
{
    if (s_event_queue != NULL) {
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
    }

    free(s_event_queue_storage);
    s_event_queue_storage = NULL;

    if (s_command_queue != NULL) {
        vQueueDelete(s_command_queue);
        s_command_queue = NULL;
    }

    if (s_events != NULL) {
        vEventGroupDelete(s_events);
        s_events = NULL;
    }
}

esp_err_t can_logger_service_start(void)
{
    if (!can_router_is_running()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();

        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t result = can_logger_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (atomic_load(&s_running)) {
        can_logger_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_info, 0, sizeof(s_info));
    s_info.state = CAN_LOGGER_STATE_IDLE;
    s_info.format = CAN_LOGGER_FORMAT_ASC;
    s_info.last_error = ESP_OK;
    can_logger_reset_statistics_internal();

    const size_t queue_storage_size =
        CAN_LOGGER_EVENT_QUEUE_LENGTH *
        sizeof(can_logger_queue_item_t);

    s_event_queue_storage = heap_caps_malloc(
        queue_storage_size,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (s_event_queue_storage == NULL) {
        result = ESP_ERR_NO_MEM;
        goto failed;
    }

    s_event_queue = xQueueCreateStatic(
        CAN_LOGGER_EVENT_QUEUE_LENGTH,
        sizeof(can_logger_queue_item_t),
        s_event_queue_storage,
        &s_event_queue_control
    );

    s_command_queue = xQueueCreate(
        CAN_LOGGER_COMMAND_QUEUE_LENGTH,
        sizeof(can_logger_command_t)
    );
    s_events = xEventGroupCreate();

    if ((s_event_queue == NULL) ||
        (s_command_queue == NULL) ||
        (s_events == NULL)) {

        result = ESP_ERR_NO_MEM;
        goto failed;
    }

    const can_router_subscription_t subscription = {
        .bus_mask = CAN_ROUTER_ALL_BUSES_MASK,
        .event_mask = CAN_ROUTER_ALL_EVENTS_MASK,
        .callback = can_logger_router_callback,
        .context = NULL,
    };

    result = can_router_subscribe(
        &subscription,
        &s_router_subscription_id
    );

    if (result != ESP_OK) {
        goto failed;
    }

    atomic_store(&s_running, true);

    const BaseType_t task_result = xTaskCreateWithCaps(
        can_logger_task,
        "can_logger",
        CAN_LOGGER_TASK_STACK_SIZE,
        NULL,
        CAN_LOGGER_TASK_PRIORITY,
        &s_task,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (task_result != pdPASS) {
        atomic_store(&s_running, false);
        (void)can_router_unsubscribe(s_router_subscription_id);
        s_router_subscription_id = CAN_ROUTER_SUBSCRIPTION_ID_NONE;
        result = ESP_ERR_NO_MEM;
        goto failed;
    }

    s_info.service_running = true;
    can_logger_unlock();

    ESP_LOGI(
        TAG,
        "CAN logger service started: queue=%u, buffer=%u bytes",
        (unsigned int)CAN_LOGGER_EVENT_QUEUE_LENGTH,
        (unsigned int)CAN_LOGGER_WRITE_BUFFER_SIZE
    );

    return ESP_OK;

failed:
    can_logger_release_resources();
    can_logger_unlock();
    return result;
}

esp_err_t can_logger_service_stop(void)
{
    esp_err_t result = can_logger_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(&s_running)) {
        can_logger_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    atomic_store(&s_accepting_events, false);

    if (s_router_subscription_id !=
        CAN_ROUTER_SUBSCRIPTION_ID_NONE) {

        result = can_router_unsubscribe(
            s_router_subscription_id
        );

        if (result != ESP_OK) {
            can_logger_unlock();
            return result;
        }

        s_router_subscription_id =
            CAN_ROUTER_SUBSCRIPTION_ID_NONE;
    }

    can_logger_wait_callbacks();

    const can_logger_command_t command = {
        .type = CAN_LOGGER_COMMAND_SHUTDOWN,
    };

    if (xQueueSend(
            s_command_queue,
            &command,
            pdMS_TO_TICKS(CAN_LOGGER_COMMAND_TIMEOUT_MS)
        ) != pdTRUE) {

        can_logger_unlock();
        return ESP_ERR_TIMEOUT;
    }

    can_logger_unlock();

    const EventBits_t bits = xEventGroupWaitBits(
        s_events,
        CAN_LOGGER_EVENT_TASK_STOPPED,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(CAN_LOGGER_STOP_TIMEOUT_MS)
    );

    if ((bits & CAN_LOGGER_EVENT_TASK_STOPPED) == 0U) {
        return ESP_ERR_TIMEOUT;
    }

    result = can_logger_lock();

    if (result != ESP_OK) {
        return result;
    }

    const esp_err_t stop_result =
        s_info.last_error;

    s_task = NULL;
    s_info.service_running = false;
    atomic_store(&s_running, false);
    can_logger_release_resources();
    can_logger_unlock();

    ESP_LOGI(TAG, "CAN logger service stopped");
    return stop_result;
}

esp_err_t can_logger_service_start_recording(
    const can_logger_recording_config_t *config
)
{
    if (!can_logger_config_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = can_logger_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(&s_running)) {
        can_logger_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    if ((s_info.state == CAN_LOGGER_STATE_STARTING) ||
        (s_info.state == CAN_LOGGER_STATE_RECORDING) ||
        (s_info.state == CAN_LOGGER_STATE_STOPPING)) {

        can_logger_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    can_logger_command_t command = {
        .type = CAN_LOGGER_COMMAND_START,
        .config = *config,
    };

    s_info.state = CAN_LOGGER_STATE_STARTING;
    s_info.format = config->format;
    s_info.filter = config->filter;
    s_info.last_error = ESP_OK;
    s_info.started_at_us = 0U;
    s_info.duration_ms = 0U;
    (void)strlcpy(
        s_info.file_path,
        config->file_path,
        sizeof(s_info.file_path)
    );

    (void)xEventGroupClearBits(
        s_events,
        CAN_LOGGER_EVENT_RECORDING_STOPPED
    );

    if (xQueueSend(
            s_command_queue,
            &command,
            pdMS_TO_TICKS(CAN_LOGGER_COMMAND_TIMEOUT_MS)
        ) != pdTRUE) {

        s_info.state = CAN_LOGGER_STATE_IDLE;
        s_info.file_path[0] = '\0';
        can_logger_unlock();
        return ESP_ERR_TIMEOUT;
    }

    can_logger_unlock();
    return ESP_OK;
}

esp_err_t can_logger_service_stop_recording(void)
{
    esp_err_t result = can_logger_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(&s_running) ||
        ((s_info.state != CAN_LOGGER_STATE_STARTING) &&
         (s_info.state != CAN_LOGGER_STATE_RECORDING))) {

        can_logger_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    const can_logger_state_t previous_state =
        s_info.state;

    atomic_store_explicit(
        &s_accepting_events,
        false,
        memory_order_release
    );
    s_info.state = CAN_LOGGER_STATE_STOPPING;

    const can_logger_command_t command = {
        .type = CAN_LOGGER_COMMAND_STOP,
    };

    if (xQueueSend(
            s_command_queue,
            &command,
            pdMS_TO_TICKS(CAN_LOGGER_COMMAND_TIMEOUT_MS)
        ) != pdTRUE) {

        s_info.state = previous_state;

        if (previous_state == CAN_LOGGER_STATE_RECORDING) {
            atomic_store(&s_accepting_events, true);
        }

        can_logger_unlock();
        return ESP_ERR_TIMEOUT;
    }

    can_logger_unlock();
    return ESP_OK;
}

esp_err_t can_logger_service_wait_stopped(uint32_t timeout_ms)
{
    if (!atomic_load(&s_running) || (s_events == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    const EventBits_t bits = xEventGroupWaitBits(
        s_events,
        CAN_LOGGER_EVENT_RECORDING_STOPPED,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(timeout_ms)
    );

    if ((bits & CAN_LOGGER_EVENT_RECORDING_STOPPED) == 0U) {
        can_logger_info_t info;
        const esp_err_t result =
            can_logger_service_get_info(&info);

        if ((result == ESP_OK) &&
            ((info.state == CAN_LOGGER_STATE_IDLE) ||
             (info.state == CAN_LOGGER_STATE_ERROR))) {

            return info.last_error;
        }

        return ESP_ERR_TIMEOUT;
    }

    can_logger_info_t info;
    const esp_err_t result =
        can_logger_service_get_info(&info);

    if (result != ESP_OK) {
        return result;
    }

    return info.last_error;
}

esp_err_t can_logger_service_get_info(can_logger_info_t *info)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = can_logger_lock();

    if (result != ESP_OK) {
        return result;
    }

    *info = s_info;

    if ((s_info.started_at_us != 0U) &&
        ((s_info.state == CAN_LOGGER_STATE_RECORDING) ||
         (s_info.state == CAN_LOGGER_STATE_STOPPING))) {

        const uint64_t now_us =
            (uint64_t)esp_timer_get_time();

        info->duration_ms =
            (now_us >= s_info.started_at_us)
                ? (now_us - s_info.started_at_us) / 1000U
                : 0U;
    }

    info->statistics.received_events =
        atomic_load(&s_received_events);
    info->statistics.filtered_events =
        atomic_load(&s_filtered_events);
    info->statistics.queued_events =
        atomic_load(&s_queued_events);
    info->statistics.dropped_events =
        atomic_load(&s_dropped_events);
    info->statistics.written_events =
        atomic_load(&s_written_events);
    info->statistics.written_bytes =
        atomic_load(&s_written_bytes);
    info->statistics.serialization_failures =
        atomic_load(&s_serialization_failures);
    info->statistics.write_failures =
        atomic_load(&s_write_failures);
    info->statistics.sync_failures =
        atomic_load(&s_sync_failures);
    info->statistics.queue_current =
        (s_event_queue != NULL)
            ? uxQueueMessagesWaiting(s_event_queue)
            : 0U;
    info->statistics.queue_peak =
        atomic_load(&s_queue_peak);
    info->statistics.queue_capacity =
        (s_event_queue != NULL)
            ? CAN_LOGGER_EVENT_QUEUE_LENGTH
            : 0U;

    can_logger_unlock();
    return ESP_OK;
}

esp_err_t can_logger_service_reset_statistics(void)
{
    esp_err_t result = can_logger_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(&s_running) ||
        (s_info.state != CAN_LOGGER_STATE_IDLE)) {

        can_logger_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    can_logger_reset_statistics_internal();
    can_logger_unlock();
    return ESP_OK;
}

bool can_logger_service_is_running(void)
{
    return atomic_load_explicit(
        &s_running,
        memory_order_acquire
    );
}

bool can_logger_service_is_recording(void)
{
    if (can_logger_lock() != ESP_OK) {
        return false;
    }

    const bool recording =
        (s_info.state == CAN_LOGGER_STATE_STARTING) ||
        (s_info.state == CAN_LOGGER_STATE_RECORDING) ||
        (s_info.state == CAN_LOGGER_STATE_STOPPING);

    can_logger_unlock();
    return recording;
}
