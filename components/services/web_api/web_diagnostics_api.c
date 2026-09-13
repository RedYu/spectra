/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "web_diagnostics_api.h"

#include <stdbool.h>
#include <stdlib.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "can_fd_service.h"
#include "can_logger_service.h"
#include "can_monitor_service.h"
#include "can_router.h"
#include "can_service.h"
#include "storage_sd_benchmark.h"
#include "web_api_common.h"
#include "web_can_stream_service.h"

static const char *TAG =
    "web_diagnostics_api";

static const char *web_diagnostics_api_task_state_name(
    eTaskState state
)
{
    switch (state) {
        case eRunning:
            return "running";

        case eReady:
            return "ready";

        case eBlocked:
            return "blocked";

        case eSuspended:
            return "suspended";

        case eDeleted:
            return "deleted";

        case eInvalid:
        default:
            return "invalid";
    }
}

static bool web_diagnostics_api_add_heap(
    cJSON *response
)
{
    cJSON *heap = cJSON_CreateObject();

    if (heap == NULL) {
        return false;
    }

    const uint32_t internal_caps =
        MALLOC_CAP_INTERNAL |
        MALLOC_CAP_8BIT;

    const uint32_t dma_caps =
        MALLOC_CAP_INTERNAL |
        MALLOC_CAP_DMA;

    const uint32_t psram_caps =
        MALLOC_CAP_SPIRAM |
        MALLOC_CAP_8BIT;

    bool valid = true;

#define ADD_HEAP_NUMBER(name, value) \
    valid = valid && \
        (cJSON_AddNumberToObject( \
            heap, \
            name, \
            (double)(value) \
        ) != NULL)

    ADD_HEAP_NUMBER(
        "internal_free",
        heap_caps_get_free_size(internal_caps)
    );
    ADD_HEAP_NUMBER(
        "internal_minimum_free",
        heap_caps_get_minimum_free_size(internal_caps)
    );
    ADD_HEAP_NUMBER(
        "internal_largest_block",
        heap_caps_get_largest_free_block(internal_caps)
    );
    ADD_HEAP_NUMBER(
        "dma_free",
        heap_caps_get_free_size(dma_caps)
    );
    ADD_HEAP_NUMBER(
        "dma_minimum_free",
        heap_caps_get_minimum_free_size(dma_caps)
    );
    ADD_HEAP_NUMBER(
        "dma_largest_block",
        heap_caps_get_largest_free_block(dma_caps)
    );
    ADD_HEAP_NUMBER(
        "psram_free",
        heap_caps_get_free_size(psram_caps)
    );
    ADD_HEAP_NUMBER(
        "psram_minimum_free",
        heap_caps_get_minimum_free_size(psram_caps)
    );
    ADD_HEAP_NUMBER(
        "psram_largest_block",
        heap_caps_get_largest_free_block(psram_caps)
    );

#undef ADD_HEAP_NUMBER

    if (!valid ||
        !cJSON_AddItemToObject(response, "heap", heap)) {

        cJSON_Delete(heap);
        return false;
    }

    return true;
}

static bool web_diagnostics_api_add_can(
    cJSON *response
)
{
    cJSON *can = cJSON_CreateObject();

    if (can == NULL) {
        return false;
    }

    can_router_statistics_t router = {0};
    can_monitor_service_statistics_t monitor = {0};
    can_service_queue_statistics_t primary = {0};
    can_fd_service_statistics_t secondary = {0};
    can_twai_driver_info_t primary_driver = {0};
    can_fd_mcp2518fd_info_t secondary_driver = {0};

    const bool router_available =
        can_router_get_statistics(&router) == ESP_OK;
    const bool monitor_available =
        can_monitor_service_get_statistics(&monitor) == ESP_OK;
    const bool primary_available =
        can_service_get_queue_statistics(&primary) == ESP_OK;
    const bool secondary_available =
        can_fd_service_get_statistics(&secondary) == ESP_OK;
    const bool primary_driver_available =
        can_service_get_info(&primary_driver) == ESP_OK;
    const bool secondary_driver_available =
        can_fd_service_get_info(&secondary_driver) == ESP_OK;

    bool valid = true;

#define ADD_CAN_NUMBER(name, value) \
    valid = valid && \
        (cJSON_AddNumberToObject( \
            can, \
            name, \
            (double)(value) \
        ) != NULL)

    valid = valid &&
        (cJSON_AddBoolToObject(
            can,
            "router_available",
            router_available
        ) != NULL);
    ADD_CAN_NUMBER("router_queue_current", router.queue_current);
    ADD_CAN_NUMBER("router_queue_peak", router.queue_peak);
    ADD_CAN_NUMBER("router_queue_capacity", router.queue_capacity);
    ADD_CAN_NUMBER("router_dropped_events", router.dropped_events);
    ADD_CAN_NUMBER("router_received_frames", router.received_frames);
    ADD_CAN_NUMBER("router_transmitted_frames", router.transmitted_frames);

    valid = valid &&
        (cJSON_AddBoolToObject(
            can,
            "monitor_available",
            monitor_available
        ) != NULL);
    ADD_CAN_NUMBER("monitor_queue_current", monitor.input_queue_current);
    ADD_CAN_NUMBER("monitor_queue_peak", monitor.input_queue_peak);
    ADD_CAN_NUMBER("monitor_queue_capacity", monitor.input_queue_capacity);
    ADD_CAN_NUMBER("monitor_dropped_events", monitor.dropped_input_events);
    ADD_CAN_NUMBER("monitor_identifiers", monitor.tracked_identifiers);
    ADD_CAN_NUMBER("monitor_identifier_capacity", monitor.identifier_capacity);
    ADD_CAN_NUMBER(
        "primary_received_frames",
        monitor.buses[CAN_BUS_PRIMARY].received_frames
    );
    ADD_CAN_NUMBER(
        "primary_transmitted_frames",
        monitor.buses[CAN_BUS_PRIMARY].completed_transmissions
    );
    ADD_CAN_NUMBER(
        "secondary_received_frames",
        monitor.buses[CAN_BUS_SECONDARY].received_frames
    );
    ADD_CAN_NUMBER(
        "secondary_transmitted_frames",
        monitor.buses[CAN_BUS_SECONDARY].completed_transmissions
    );

    valid = valid &&
        (cJSON_AddBoolToObject(
            can,
            "primary_running",
            can_service_is_running()
        ) != NULL);
    valid = valid &&
        (cJSON_AddBoolToObject(
            can,
            "primary_statistics_available",
            primary_available
        ) != NULL);
    ADD_CAN_NUMBER(
        "primary_confirmation_queue_current",
        primary.confirmation_queue_current
    );
    ADD_CAN_NUMBER(
        "primary_confirmation_queue_peak",
        primary.confirmation_queue_peak
    );
    ADD_CAN_NUMBER(
        "primary_confirmation_queue_capacity",
        primary.confirmation_queue_capacity
    );
    ADD_CAN_NUMBER(
        "primary_dropped_confirmations",
        primary.dropped_tx_confirmations
    );
    valid = valid &&
        (cJSON_AddBoolToObject(
            can,
            "primary_driver_available",
            primary_driver_available
        ) != NULL);
    ADD_CAN_NUMBER("primary_driver_state", primary_driver.state);
    ADD_CAN_NUMBER("primary_rx_queue_current", primary_driver.rx_queue_current);
    ADD_CAN_NUMBER("primary_rx_queue_peak", primary_driver.rx_queue_peak);
    ADD_CAN_NUMBER("primary_rx_queue_capacity", primary_driver.rx_queue_capacity);
    ADD_CAN_NUMBER("primary_tx_slots_used", primary_driver.tx_slots_used);
    ADD_CAN_NUMBER("primary_tx_slots_peak", primary_driver.tx_slots_peak);
    ADD_CAN_NUMBER("primary_tx_slots_capacity", primary_driver.tx_slots_capacity);
    ADD_CAN_NUMBER("primary_dropped_rx_frames", primary_driver.dropped_rx_frames);
    ADD_CAN_NUMBER("primary_bus_errors", primary_driver.bus_error_count);
    ADD_CAN_NUMBER("primary_ack_errors", primary_driver.acknowledgement_error_count);
    ADD_CAN_NUMBER("primary_arbitration_lost", primary_driver.arbitration_lost_count);
    ADD_CAN_NUMBER("primary_tx_error_count", primary_driver.transmit_error_count);
    ADD_CAN_NUMBER("primary_rx_error_count", primary_driver.receive_error_count);

    valid = valid &&
        (cJSON_AddBoolToObject(
            can,
            "secondary_running",
            can_fd_service_is_running()
        ) != NULL);
    valid = valid &&
        (cJSON_AddBoolToObject(
            can,
            "secondary_statistics_available",
            secondary_available
        ) != NULL);
    ADD_CAN_NUMBER(
        "secondary_delivered_rx_frames",
        secondary.delivered_rx_frames
    );
    ADD_CAN_NUMBER(
        "secondary_delivered_tx_confirmations",
        secondary.delivered_tx_confirmations
    );
    ADD_CAN_NUMBER("secondary_receive_errors", secondary.receive_errors);
    ADD_CAN_NUMBER("secondary_tx_event_errors", secondary.tx_event_errors);
    valid = valid &&
        (cJSON_AddBoolToObject(
            can,
            "secondary_driver_available",
            secondary_driver_available
        ) != NULL);
    ADD_CAN_NUMBER("secondary_driver_state", secondary_driver.state);
    ADD_CAN_NUMBER("secondary_dropped_rx_frames", secondary_driver.dropped_rx_frames);
    ADD_CAN_NUMBER("secondary_rx_overflows", secondary_driver.receive_overflow_count);
    ADD_CAN_NUMBER(
        "secondary_tef_overflows",
        secondary_driver.transmit_event_overflow_count
    );
    ADD_CAN_NUMBER("secondary_bus_errors", secondary_driver.bus_error_count);
    ADD_CAN_NUMBER("secondary_tx_failures", secondary_driver.transmit_failures);
    ADD_CAN_NUMBER("secondary_tx_error_count", secondary_driver.transmit_error_count);
    ADD_CAN_NUMBER("secondary_rx_error_count", secondary_driver.receive_error_count);

#undef ADD_CAN_NUMBER

    if (!valid ||
        !cJSON_AddItemToObject(response, "can", can)) {

        cJSON_Delete(can);
        return false;
    }

    return true;
}

static bool web_diagnostics_api_add_consumers(
    cJSON *response
)
{
    cJSON *consumers = cJSON_CreateObject();

    if (consumers == NULL) {
        return false;
    }

    web_can_stream_service_statistics_t stream = {0};
    can_logger_info_t logger = {0};

    const bool stream_available =
        web_can_stream_service_get_statistics(&stream) == ESP_OK;
    const bool logger_available =
        can_logger_service_get_info(&logger) == ESP_OK;

    bool valid = true;

#define ADD_CONSUMER_NUMBER(name, value) \
    valid = valid && \
        (cJSON_AddNumberToObject( \
            consumers, \
            name, \
            (double)(value) \
        ) != NULL)

    valid = valid &&
        (cJSON_AddBoolToObject(
            consumers,
            "stream_available",
            stream_available
        ) != NULL);
    valid = valid &&
        (cJSON_AddBoolToObject(
            consumers,
            "stream_client_connected",
            stream.client_connected
        ) != NULL);
    ADD_CONSUMER_NUMBER("stream_queue_current", stream.queue_current);
    ADD_CONSUMER_NUMBER("stream_queue_peak", stream.queue_peak);
    ADD_CONSUMER_NUMBER("stream_queue_capacity", stream.queue_capacity);
    ADD_CONSUMER_NUMBER("stream_dropped_events", stream.dropped_events);
    ADD_CONSUMER_NUMBER("stream_send_failures", stream.send_failures);

    valid = valid &&
        (cJSON_AddBoolToObject(
            consumers,
            "logger_available",
            logger_available
        ) != NULL);
    valid = valid &&
        (cJSON_AddBoolToObject(
            consumers,
            "logger_running",
            logger.service_running
        ) != NULL);
    ADD_CONSUMER_NUMBER("logger_state", logger.state);
    ADD_CONSUMER_NUMBER(
        "logger_queue_current",
        logger.statistics.queue_current
    );
    ADD_CONSUMER_NUMBER(
        "logger_queue_peak",
        logger.statistics.queue_peak
    );
    ADD_CONSUMER_NUMBER(
        "logger_queue_capacity",
        logger.statistics.queue_capacity
    );
    ADD_CONSUMER_NUMBER(
        "logger_dropped_events",
        logger.statistics.dropped_events
    );
    ADD_CONSUMER_NUMBER(
        "logger_written_events",
        logger.statistics.written_events
    );
    ADD_CONSUMER_NUMBER(
        "logger_written_bytes",
        logger.statistics.written_bytes
    );
    ADD_CONSUMER_NUMBER(
        "logger_write_failures",
        logger.statistics.write_failures
    );
    ADD_CONSUMER_NUMBER(
        "logger_sync_failures",
        logger.statistics.sync_failures
    );

#undef ADD_CONSUMER_NUMBER

    if (!valid ||
        !cJSON_AddItemToObject(response, "consumers", consumers)) {

        cJSON_Delete(consumers);
        return false;
    }

    return true;
}

static bool web_diagnostics_api_add_queue(
    cJSON *queues,
    const char *name,
    const char *owner,
    bool available,
    uint32_t current,
    uint32_t peak,
    uint32_t capacity,
    uint64_t dropped
)
{
    cJSON *queue = cJSON_CreateObject();

    if (queue == NULL) {
        return false;
    }

    const bool valid =
        (cJSON_AddStringToObject(queue, "name", name) != NULL) &&
        (cJSON_AddStringToObject(queue, "owner", owner) != NULL) &&
        (cJSON_AddBoolToObject(queue, "available", available) != NULL) &&
        (cJSON_AddNumberToObject(queue, "current", current) != NULL) &&
        (cJSON_AddNumberToObject(queue, "peak", peak) != NULL) &&
        (cJSON_AddNumberToObject(queue, "capacity", capacity) != NULL) &&
        (cJSON_AddNumberToObject(queue, "dropped", (double)dropped) != NULL);

    if (!valid ||
        !cJSON_AddItemToArray(queues, queue)) {

        cJSON_Delete(queue);
        return false;
    }

    return true;
}

static bool web_diagnostics_api_add_queues(
    cJSON *response
)
{
    can_router_statistics_t router = {0};
    can_monitor_service_statistics_t monitor = {0};
    can_service_queue_statistics_t primary = {0};
    can_twai_driver_info_t primary_driver = {0};
    web_can_stream_service_statistics_t stream = {0};
    can_logger_info_t logger = {0};

    const bool router_available =
        can_router_get_statistics(&router) == ESP_OK;
    const bool monitor_available =
        can_monitor_service_get_statistics(&monitor) == ESP_OK;
    const bool primary_available =
        can_service_get_queue_statistics(&primary) == ESP_OK;
    const bool primary_driver_available =
        can_service_get_info(&primary_driver) == ESP_OK;
    const bool stream_available =
        web_can_stream_service_get_statistics(&stream) == ESP_OK;
    const bool logger_available =
        can_logger_service_get_info(&logger) == ESP_OK;

    cJSON *queues = cJSON_CreateArray();

    if (queues == NULL) {
        return false;
    }

    const bool valid =
        web_diagnostics_api_add_queue(
            queues,
            "Event input",
            "CAN router",
            router_available,
            router.queue_current,
            router.queue_peak,
            router.queue_capacity,
            router.dropped_events
        ) &&
        web_diagnostics_api_add_queue(
            queues,
            "Monitor input",
            "CAN monitor",
            monitor_available,
            monitor.input_queue_current,
            monitor.input_queue_peak,
            monitor.input_queue_capacity,
            monitor.dropped_input_events
        ) &&
        web_diagnostics_api_add_queue(
            queues,
            "RX frames",
            "Primary TWAI",
            primary_driver_available,
            primary_driver.rx_queue_current,
            primary_driver.rx_queue_peak,
            primary_driver.rx_queue_capacity,
            primary_driver.dropped_rx_frames
        ) &&
        web_diagnostics_api_add_queue(
            queues,
            "TX confirmations",
            "Primary CAN service",
            primary_available,
            primary.confirmation_queue_current,
            primary.confirmation_queue_peak,
            primary.confirmation_queue_capacity,
            primary.dropped_tx_confirmations
        ) &&
        web_diagnostics_api_add_queue(
            queues,
            "TX hardware slots",
            "Primary TWAI",
            primary_driver_available,
            primary_driver.tx_slots_used,
            primary_driver.tx_slots_peak,
            primary_driver.tx_slots_capacity,
            0U
        ) &&
        web_diagnostics_api_add_queue(
            queues,
            "Stream events",
            "CAN WebSocket",
            stream_available,
            stream.queue_current,
            stream.queue_peak,
            stream.queue_capacity,
            stream.dropped_events
        ) &&
        web_diagnostics_api_add_queue(
            queues,
            "Recording events",
            "CAN logger",
            logger_available,
            (uint32_t)logger.statistics.queue_current,
            (uint32_t)logger.statistics.queue_peak,
            (uint32_t)logger.statistics.queue_capacity,
            logger.statistics.dropped_events
        );

    if (!valid ||
        !cJSON_AddItemToObject(response, "queues", queues)) {

        cJSON_Delete(queues);
        return false;
    }

    return true;
}

static bool web_diagnostics_api_add_storage_benchmark(
    cJSON *response
)
{
    storage_sd_benchmark_result_t result = {0};
    esp_err_t status = ESP_ERR_NOT_FOUND;

    const bool available =
        storage_sd_benchmark_get_last_result(
            &result,
            &status
        ) == ESP_OK;

    cJSON *benchmark = cJSON_CreateObject();

    if (benchmark == NULL) {
        return false;
    }

    bool valid = true;

#define ADD_BENCHMARK_NUMBER(name, value) \
    valid = valid && \
        (cJSON_AddNumberToObject( \
            benchmark, \
            name, \
            (double)(value) \
        ) != NULL)

    valid = valid &&
        (cJSON_AddBoolToObject(
            benchmark,
            "available",
            available
        ) != NULL);
    ADD_BENCHMARK_NUMBER("status", status);
    ADD_BENCHMARK_NUMBER("tested_bytes", result.tested_bytes);
    ADD_BENCHMARK_NUMBER("block_size", result.block_size);
    ADD_BENCHMARK_NUMBER(
        "write_bytes_per_second",
        result.write_speed_bytes_per_second
    );
    ADD_BENCHMARK_NUMBER(
        "read_bytes_per_second",
        result.read_speed_bytes_per_second
    );
    ADD_BENCHMARK_NUMBER(
        "raw_read_bytes_per_second",
        result.raw_read_speed_bytes_per_second
    );
    ADD_BENCHMARK_NUMBER("maximum_write_us", result.maximum_write_block_time_us);
    ADD_BENCHMARK_NUMBER("maximum_read_us", result.maximum_read_block_time_us);
    ADD_BENCHMARK_NUMBER("sync_us", result.sync_time_us);
    valid = valid &&
        (cJSON_AddBoolToObject(
            benchmark,
            "data_verified",
            result.data_verified
        ) != NULL);

#undef ADD_BENCHMARK_NUMBER

    if (!valid ||
        !cJSON_AddItemToObject(
            response,
            "storage_benchmark",
            benchmark
        )) {

        cJSON_Delete(benchmark);
        return false;
    }

    return true;
}

static bool web_diagnostics_api_add_tasks(
    cJSON *response
)
{
    cJSON *tasks = cJSON_CreateArray();

    if (tasks == NULL) {
        return false;
    }

#if CONFIG_FREERTOS_USE_TRACE_FACILITY

    const UBaseType_t capacity =
        uxTaskGetNumberOfTasks() + 8U;

    TaskStatus_t *statuses = heap_caps_calloc(
        capacity,
        sizeof(*statuses),
        MALLOC_CAP_SPIRAM |
        MALLOC_CAP_8BIT
    );

    if (statuses == NULL) {
        cJSON_Delete(tasks);
        return false;
    }

    configRUN_TIME_COUNTER_TYPE total_runtime = 0U;
    const UBaseType_t count =
        uxTaskGetSystemState(
            statuses,
            capacity,
            &total_runtime
        );

    bool valid = count > 0U;

    for (UBaseType_t index = 0U;
         valid && (index < count);
         ++index) {

        const TaskStatus_t *status =
            &statuses[index];

        cJSON *task = cJSON_CreateObject();

        if (task == NULL) {
            valid = false;
            break;
        }

        double runtime_percent = 0.0;

#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
        if (total_runtime > 0U) {
            runtime_percent =
                ((double)status->ulRunTimeCounter * 100.0) /
                (double)total_runtime;
        }
#endif

        bool task_valid =
            (cJSON_AddStringToObject(
                task,
                "name",
                status->pcTaskName
            ) != NULL) &&
            (cJSON_AddStringToObject(
                task,
                "state",
                web_diagnostics_api_task_state_name(
                    status->eCurrentState
                )
            ) != NULL) &&
            (cJSON_AddNumberToObject(
                task,
                "priority",
                status->uxCurrentPriority
            ) != NULL) &&
            (cJSON_AddNumberToObject(
                task,
                "base_priority",
                status->uxBasePriority
            ) != NULL) &&
            (cJSON_AddNumberToObject(
                task,
                "stack_high_watermark_bytes",
                status->usStackHighWaterMark
            ) != NULL) &&
            (cJSON_AddNumberToObject(
                task,
                "runtime_percent",
                runtime_percent
            ) != NULL);

#if configTASKLIST_INCLUDE_COREID == 1
        task_valid = task_valid &&
            (cJSON_AddNumberToObject(
                task,
                "core",
                status->xCoreID
            ) != NULL);
#else
        task_valid = task_valid &&
            (cJSON_AddNumberToObject(
                task,
                "core",
                -1
            ) != NULL);
#endif

        if (!task_valid ||
            !cJSON_AddItemToArray(tasks, task)) {

            cJSON_Delete(task);
            valid = false;
        }
    }

    free(statuses);

    if (!valid) {
        cJSON_Delete(tasks);
        return false;
    }

    if ((cJSON_AddBoolToObject(
            response,
            "tasks_available",
            true
        ) == NULL) ||
        (cJSON_AddNumberToObject(
            response,
            "task_count",
            count
        ) == NULL)) {

        cJSON_Delete(tasks);
        return false;
    }

#else

    if ((cJSON_AddBoolToObject(
            response,
            "tasks_available",
            false
        ) == NULL) ||
        (cJSON_AddNumberToObject(
            response,
            "task_count",
            0
        ) == NULL)) {

        cJSON_Delete(tasks);
        return false;
    }

#endif

    if (!cJSON_AddItemToObject(response, "tasks", tasks)) {
        cJSON_Delete(tasks);
        return false;
    }

    return true;
}

static esp_err_t web_diagnostics_api_get_handler(
    httpd_req_t *request
)
{
    if (request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *response = cJSON_CreateObject();

    if (response == NULL) {
        return web_api_send_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to create diagnostics response"
        );
    }

    const bool valid =
        web_diagnostics_api_add_heap(response) &&
        web_diagnostics_api_add_can(response) &&
        web_diagnostics_api_add_consumers(response) &&
        web_diagnostics_api_add_queues(response) &&
        web_diagnostics_api_add_storage_benchmark(response) &&
        web_diagnostics_api_add_tasks(response);

    if (!valid) {
        cJSON_Delete(response);

        return web_api_send_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to collect device diagnostics"
        );
    }

    const esp_err_t result =
        web_api_send_json(
            request,
            response
        );

    cJSON_Delete(response);

    return result;
}

esp_err_t web_diagnostics_api_register(
    httpd_handle_t server
)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    static const httpd_uri_t diagnostics_get_uri = {
        .uri = "/api/diagnostics",
        .method = HTTP_GET,
        .handler =
            web_diagnostics_api_get_handler,
        .user_ctx = NULL,
    };

    const esp_err_t result =
        httpd_register_uri_handler(
            server,
            &diagnostics_get_uri
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to register GET /api/diagnostics: %s",
            esp_err_to_name(result)
        );
    }

    return result;
}
