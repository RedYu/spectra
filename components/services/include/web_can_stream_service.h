/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file web_can_stream_service.h
 * @brief Live CAN event streaming over WebSocket.
 */

#define WEB_CAN_STREAM_CLIENT_NONE  (-1)

/**
 * @brief WebSocket CAN stream configuration.
 */
typedef struct
{
    /**
     * Number of CAN events waiting for WebSocket processing.
     */
    uint32_t queue_depth;

    /**
     * Interval between statistics events.
     *
     * A zero value disables periodic statistics.
     */
    uint32_t statistics_interval_ms;

} web_can_stream_service_config_t;

/**
 * @brief WebSocket CAN stream runtime statistics.
 */
typedef struct
{
    uint64_t queued_events;
    uint64_t sent_events;
    uint64_t dropped_events;
    uint64_t send_failures;

    uint32_t queue_current;
    uint32_t queue_peak;
    uint32_t queue_capacity;

    bool client_connected;

    uint64_t filtered_events;

    /**
     * Number of successfully transmitted binary event batches.
     */
    uint64_t sent_batches;

    /**
     * Total number of bytes in successfully transmitted binary batches.
     */
    uint64_t sent_binary_bytes;

    /**
     * Combined number of events contained in successful batches.
     */
    uint64_t batch_events_total;

    /**
     * Maximum number of events in one successful batch.
     */
    uint32_t batch_events_peak;

    /**
     * Average number of events per successful batch.
     */
    uint32_t batch_events_average;

} web_can_stream_service_statistics_t;

/**
 * @brief Start live CAN WebSocket streaming.
 *
 * Registers the /ws/can WebSocket handler, creates the event queue and
 * processing task, then subscribes to all CAN router events.
 *
 * The CAN router and HTTP server must already be running.
 *
 * @param[in] server Running HTTP server.
 * @param[in] config Stream configuration.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid
 * arguments, ESP_ERR_INVALID_STATE if the service is already running
 * or a required service is unavailable, ESP_ERR_NO_MEM when resources
 * cannot be allocated, otherwise an ESP-IDF error code.
 */
esp_err_t web_can_stream_service_start(
    httpd_handle_t server,
    const web_can_stream_service_config_t *config
);

/**
 * @brief Stop live CAN WebSocket streaming.
 *
 * Unsubscribes from the CAN router, closes the active WebSocket client,
 * stops the processing task and releases service resources.
 *
 * This function must be called before httpd_stop().
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not running, ESP_ERR_TIMEOUT if the task does not stop in time,
 * otherwise an ESP-IDF error code.
 */
esp_err_t web_can_stream_service_stop(void);

/**
 * @brief Copy current WebSocket stream statistics.
 */
esp_err_t web_can_stream_service_get_statistics(
    web_can_stream_service_statistics_t *statistics
);

/**
 * @brief Reset cumulative WebSocket stream statistics.
 *
 * Current queue usage, queue capacity and connection state are not
 * reset.
 */
esp_err_t web_can_stream_service_reset_statistics(void);

/**
 * @brief Check whether the WebSocket stream service is running.
 */
bool web_can_stream_service_is_running(void);

#ifdef __cplusplus
}
#endif
