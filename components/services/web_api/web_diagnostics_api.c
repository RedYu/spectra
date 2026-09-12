/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "web_diagnostics_api.h"

#include <stdbool.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "can_fd_service.h"
#include "can_logger_service.h"
#include "can_monitor_service.h"
#include "can_router.h"
#include "can_service.h"
#include "web_api_common.h"
#include "web_can_stream_service.h"

static const char *TAG =
    "web_diagnostics_api";

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

    const bool router_available =
        can_router_get_statistics(&router) == ESP_OK;
    const bool monitor_available =
        can_monitor_service_get_statistics(&monitor) == ESP_OK;
    const bool primary_available =
        can_service_get_queue_statistics(&primary) == ESP_OK;
    const bool secondary_available =
        can_fd_service_get_statistics(&secondary) == ESP_OK;

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
        web_diagnostics_api_add_consumers(response);

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
