/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "isotp_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ISOTP_SESSION_DEFAULT_TIMEOUT_US  (1000000ULL)
#define ISOTP_SESSION_DEFAULT_MAX_WAIT    (3U)

/**
 * @brief ISO-TP session state.
 */
typedef enum
{
    ISOTP_SESSION_IDLE = 0,
    ISOTP_SESSION_RX_SENDING_FLOW_CONTROL,
    ISOTP_SESSION_RX_WAIT_CONSECUTIVE_FRAME,
    ISOTP_SESSION_RX_COMPLETE,
    ISOTP_SESSION_TX_SENDING_SINGLE_FRAME,
    ISOTP_SESSION_TX_SENDING_FIRST_FRAME,
    ISOTP_SESSION_TX_WAIT_FLOW_CONTROL,
    ISOTP_SESSION_TX_SENDING_CONSECUTIVE_FRAME,
    ISOTP_SESSION_TX_WAIT_ST_MIN,
    ISOTP_SESSION_TX_COMPLETE,
    ISOTP_SESSION_ERROR,

} isotp_session_state_t;

/**
 * @brief ISO-TP session error.
 */
typedef enum
{
    ISOTP_SESSION_ERROR_NONE = 0,
    ISOTP_SESSION_ERROR_UNEXPECTED_FRAME,
    ISOTP_SESSION_ERROR_SEQUENCE,
    ISOTP_SESSION_ERROR_BUFFER_OVERFLOW,
    ISOTP_SESSION_ERROR_FLOW_CONTROL_OVERFLOW,
    ISOTP_SESSION_ERROR_WAIT_LIMIT,
    ISOTP_SESSION_ERROR_TIMEOUT,
    ISOTP_SESSION_ERROR_PROTOCOL,

} isotp_session_error_t;

/**
 * @brief Action requested by the session state machine.
 */
typedef enum
{
    ISOTP_SESSION_ACTION_NONE = 0,
    ISOTP_SESSION_ACTION_SEND_FRAME,
    ISOTP_SESSION_ACTION_RECEIVE_COMPLETE,
    ISOTP_SESSION_ACTION_TRANSMIT_COMPLETE,
    ISOTP_SESSION_ACTION_ERROR,

} isotp_session_action_type_t;

/**
 * @brief Output produced by one session operation.
 */
typedef struct
{
    isotp_session_action_type_t type;

    /** Meaningful CAN payload bytes for SEND_FRAME. */
    uint8_t frame_data[ISOTP_FD_FRAME_DATA_MAX_LENGTH];
    uint8_t frame_data_length;

    /** Complete application payload length for completion actions. */
    size_t message_length;

    isotp_session_error_t error;

} isotp_session_action_t;

/**
 * @brief Static ISO-TP session configuration.
 */
typedef struct
{
    /** Maximum CAN payload length: 8 for Classical CAN, up to 64 for CAN FD. */
    uint8_t link_data_length;

    /** Flow Control Block Size advertised by this receiver; zero is unlimited. */
    uint8_t receive_block_size;

    /** Encoded STmin advertised by this receiver. */
    uint8_t receive_st_min;

    /** Maximum number of received Flow Control WAIT frames. */
    uint8_t maximum_wait_frames;

    /** Timeout while waiting for Flow Control. */
    uint64_t flow_control_timeout_us;

    /** Timeout while waiting for a Consecutive Frame. */
    uint64_t consecutive_frame_timeout_us;

} isotp_session_config_t;

/**
 * @brief Allocation-free ISO-TP session state.
 *
 * The application owns both buffers for the complete session lifetime.
 * Transmission data must remain unchanged until completion or cancellation.
 */
typedef struct
{
    isotp_session_config_t config;
    isotp_session_state_t state;
    isotp_session_error_t error;

    uint8_t *receive_buffer;
    size_t receive_capacity;
    size_t receive_size;
    size_t receive_expected_size;
    uint8_t receive_sequence_number;
    uint8_t receive_block_count;

    const uint8_t *transmit_buffer;
    size_t transmit_size;
    size_t transmit_offset;
    size_t transmit_pending_size;
    uint8_t transmit_sequence_number;
    uint8_t transmit_block_size;
    uint8_t transmit_block_count;
    uint8_t transmit_wait_count;
    uint32_t transmit_st_min_us;

    uint64_t deadline_us;

} isotp_session_t;

/**
 * @brief Initialize an idle session with an application-owned RX buffer.
 */
esp_err_t isotp_session_init(
    isotp_session_t *session,
    const isotp_session_config_t *config,
    uint8_t *receive_buffer,
    size_t receive_capacity
);

/**
 * @brief Reset an operation while retaining configuration and RX storage.
 */
void isotp_session_reset(
    isotp_session_t *session
);

/**
 * @brief Begin segmenting an application payload for transmission.
 *
 * The first CAN frame is returned through action. The payload remains owned
 * by the caller and must remain valid until the operation finishes.
 */
esp_err_t isotp_session_start_transmit(
    isotp_session_t *session,
    const uint8_t *payload,
    size_t payload_length,
    uint64_t now_us,
    isotp_session_action_t *action
);

/**
 * @brief Process one received CAN payload.
 */
esp_err_t isotp_session_receive_frame(
    isotp_session_t *session,
    const uint8_t *frame_data,
    size_t frame_data_length,
    uint64_t now_us,
    isotp_session_action_t *action
);

/**
 * @brief Confirm successful transmission of the frame in SEND_FRAME.
 *
 * The next frame or completion action may be returned immediately.
 */
esp_err_t isotp_session_frame_transmitted(
    isotp_session_t *session,
    uint64_t now_us,
    isotp_session_action_t *action
);

/**
 * @brief Poll STmin and protocol deadlines.
 */
esp_err_t isotp_session_poll(
    isotp_session_t *session,
    uint64_t now_us,
    isotp_session_action_t *action
);

#ifdef __cplusplus
}
#endif
