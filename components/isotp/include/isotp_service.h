/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "can_frame.h"
#include "isotp_session.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ISOTP_SERVICE_CHANNEL_COUNT       (4U)
#define ISOTP_SERVICE_CHANNEL_ID_NONE     (0U)

typedef enum
{
    ISOTP_SERVICE_EVENT_RECEIVED = 0,
    ISOTP_SERVICE_EVENT_TRANSMITTED,
    ISOTP_SERVICE_EVENT_ERROR,

} isotp_service_event_type_t;

typedef struct
{
    isotp_service_event_type_t type;
    uint32_t channel_id;
    const uint8_t *payload;
    size_t payload_length;
    esp_err_t result;
    isotp_session_error_t session_error;

} isotp_service_event_t;

/**
 * @brief ISO-TP channel event callback.
 *
 * The callback runs in the ISO-TP service task. Payload data remains valid
 * only until the channel buffer is reused. The callback should copy data
 * before returning when it must retain the message.
 */
typedef void (*isotp_service_event_cb_t)(
    const isotp_service_event_t *event,
    void *context
);

typedef struct
{
    uint32_t queue_depth;
    uint32_t transmit_timeout_ms;

} isotp_service_config_t;

typedef struct
{
    can_bus_id_t bus;
    uint32_t receive_identifier;
    uint32_t transmit_identifier;
    bool extended_identifier;
    bool can_fd;
    bool bit_rate_switch;

    isotp_session_config_t session;

    uint8_t *receive_buffer;
    size_t receive_capacity;
    uint8_t *transmit_buffer;
    size_t transmit_capacity;

    isotp_service_event_cb_t callback;
    void *callback_context;

} isotp_service_channel_config_t;

typedef struct
{
    bool open;
    bool busy;
    isotp_session_state_t state;
    isotp_session_error_t error;
    size_t receive_size;
    size_t transmit_size;
    uint32_t pending_transaction_id;

} isotp_service_channel_info_t;

typedef struct
{
    uint32_t current;
    uint32_t peak;
    uint32_t capacity;
    uint64_t dropped;

} isotp_service_queue_statistics_t;

/**
 * @brief Start the ISO-TP transport service after the CAN router.
 */
esp_err_t isotp_service_start(
    const isotp_service_config_t *config
);

/**
 * @brief Stop the service and release its task and queue.
 */
esp_err_t isotp_service_stop(void);

/**
 * @brief Check whether the ISO-TP transport service is running.
 */
bool isotp_service_is_running(void);

/**
 * @brief Get ISO-TP command queue statistics.
 */
esp_err_t isotp_service_get_queue_statistics(
    isotp_service_queue_statistics_t *statistics
);

/**
 * @brief Open one ISO-TP addressing channel.
 *
 * The receive and transmit buffers remain owned by the caller and must
 * remain valid until the channel is closed.
 */
esp_err_t isotp_service_open_channel(
    const isotp_service_channel_config_t *config,
    uint32_t *channel_id
);

/**
 * @brief Close an idle ISO-TP channel.
 */
esp_err_t isotp_service_close_channel(
    uint32_t channel_id
);

/**
 * @brief Queue one complete ISO-TP payload for transmission.
 *
 * The payload is copied into the channel transmit buffer before this
 * function returns.
 */
esp_err_t isotp_service_send(
    uint32_t channel_id,
    const uint8_t *payload,
    size_t payload_length
);

/**
 * @brief Copy the current state of an open ISO-TP channel.
 */
esp_err_t isotp_service_get_channel_info(
    uint32_t channel_id,
    isotp_service_channel_info_t *info
);

#ifdef __cplusplus
}
#endif
