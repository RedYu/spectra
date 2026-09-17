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
#include "xcp_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XCP_SERVICE_SESSION_COUNT       (4U)
#define XCP_SERVICE_SESSION_ID_NONE     (0U)
#define XCP_SERVICE_RESPONSE_MAX_SIZE   XCP_CAN_FD_CTO_MAX_SIZE

/**
 * @brief XCP master-session state.
 */
typedef enum
{
    XCP_SERVICE_SESSION_CLOSED = 0,
    XCP_SERVICE_SESSION_DISCONNECTED,
    XCP_SERVICE_SESSION_CONNECTING,
    XCP_SERVICE_SESSION_CONNECTED,
    XCP_SERVICE_SESSION_COMMAND_PENDING,
    XCP_SERVICE_SESSION_DISCONNECTING,
    XCP_SERVICE_SESSION_ERROR,

} xcp_service_session_state_t;

/**
 * @brief Unsolicited XCP packet received by a session.
 */
typedef struct
{
    uint32_t session_id;
    xcp_packet_type_t type;
    uint8_t packet_identifier;
    uint8_t code;
    const uint8_t *payload;
    size_t payload_length;

} xcp_service_event_t;

/**
 * @brief Unsolicited XCP event callback.
 *
 * The callback runs in the XCP service task. The payload remains valid only
 * until the callback returns and must be copied when it needs to be retained.
 */
typedef void (*xcp_service_event_cb_t)(
    const xcp_service_event_t *event,
    void *context
);

/**
 * @brief XCP service configuration.
 */
typedef struct
{
    uint32_t queue_depth;
    uint32_t transmit_timeout_ms;

} xcp_service_config_t;

/**
 * @brief CAN transport and timeout configuration for one XCP slave.
 */
typedef struct
{
    can_bus_id_t bus;
    uint32_t command_identifier;
    uint32_t response_identifier;
    uint32_t stim_identifier;
    bool extended_identifier;
    bool can_fd;
    bool bit_rate_switch;

    uint8_t transmit_data_length;
    uint8_t padding_byte;
    uint32_t response_timeout_ms;

    xcp_service_event_cb_t callback;
    void *callback_context;

} xcp_service_session_config_t;

/**
 * @brief Current XCP session state and counters.
 */
typedef struct
{
    bool open;
    bool connected;
    xcp_service_session_state_t state;
    xcp_service_session_config_t config;

    xcp_connect_response_t slave;

    uint32_t pending_transaction_id;
    uint8_t last_xcp_error;
    esp_err_t last_error;

    uint64_t transmitted_commands;
    uint64_t received_responses;
    uint64_t received_errors;
    uint64_t received_events;
    uint64_t timeouts;
    uint64_t unexpected_packets;

} xcp_service_session_info_t;

/**
 * @brief XCP service command-queue statistics.
 */
typedef struct
{
    uint32_t current;
    uint32_t peak;
    uint32_t capacity;
    uint64_t dropped;

} xcp_service_queue_statistics_t;

/**
 * @brief Start the XCP master service after the CAN router.
 *
 * @param[in] config Service task and queue configuration.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t xcp_service_start(
    const xcp_service_config_t *config
);

/**
 * @brief Stop all XCP sessions and release service resources.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t xcp_service_stop(void);

/**
 * @brief Check whether the XCP service is running.
 */
bool xcp_service_is_running(void);

/**
 * @brief Open one XCP master session.
 *
 * Opening a session reserves its CAN identifiers but does not transmit
 * CONNECT. The supplied callback context must remain valid until the session
 * is closed.
 *
 * @param[in] config Session transport configuration.
 * @param[out] session_id Allocated non-zero session identifier.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t xcp_service_open_session(
    const xcp_service_session_config_t *config,
    uint32_t *session_id
);

/**
 * @brief Close an idle XCP session.
 *
 * A connected session is disconnected before its resources are released.
 *
 * @param[in] session_id Open session identifier.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t xcp_service_close_session(
    uint32_t session_id
);

/**
 * @brief Connect to the configured XCP slave.
 *
 * The function blocks the calling task until the slave returns RES or ERR,
 * the response timeout expires, or the operation is cancelled.
 *
 * @param[in] session_id Open session identifier.
 * @param[in] mode CONNECT mode byte.
 *
 * @return ESP_OK on success, ESP_ERR_TIMEOUT on timeout, or another ESP-IDF
 * error code describing the transport or protocol failure.
 */
esp_err_t xcp_service_connect(
    uint32_t session_id,
    uint8_t mode
);

/**
 * @brief Disconnect an established XCP session.
 *
 * @param[in] session_id Open session identifier.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t xcp_service_disconnect(
    uint32_t session_id
);

/**
 * @brief Execute one encoded XCP CTO command.
 *
 * Only one command can be pending per session. The command buffer contains
 * the command PID followed by its parameters. The returned response contains
 * the complete RES or ERR packet, including its PID.
 *
 * @param[in] session_id Connected session identifier.
 * @param[in] command Encoded CTO command.
 * @param[in] command_size Number of valid command bytes.
 * @param[out] response Destination response buffer.
 * @param[in] response_capacity Destination capacity.
 * @param[out] response_size Number of returned response bytes.
 *
 * @return ESP_OK on RES, ESP_ERR_INVALID_RESPONSE on ERR,
 * ESP_ERR_TIMEOUT when no response is received, or another ESP-IDF error.
 */
esp_err_t xcp_service_execute_cto(
    uint32_t session_id,
    const uint8_t *command,
    size_t command_size,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_size
);

/**
 * @brief Transmit one DTO packet for STIM without waiting for a CTO response.
 *
 * The packet must fit the configured CAN payload and negotiated MAX_DTO.
 */
esp_err_t xcp_service_transmit_dto(
    uint32_t session_id,
    const uint8_t *data,
    size_t data_size,
    uint32_t *transaction_id
);

/**
 * @brief Queue one CTO packet without waiting for a response.
 *
 * Used for intermediate DOWNLOAD_NEXT and PROGRAM_NEXT packets in XCP
 * slave block mode. The final packet must be executed with
 * xcp_service_execute_cto() so that the block response is collected.
 */
esp_err_t xcp_service_transmit_cto(
    uint32_t session_id,
    const uint8_t *data,
    size_t data_size,
    uint32_t *transaction_id
);

/**
 * @brief Cancel the command currently pending in a session.
 *
 * @param[in] session_id Open session identifier.
 *
 * @return ESP_OK when cancellation was requested, otherwise an ESP-IDF error.
 */
esp_err_t xcp_service_cancel(
    uint32_t session_id
);

/**
 * @brief Copy the current state of an XCP session.
 *
 * @param[in] session_id Open session identifier.
 * @param[out] info Destination session information.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t xcp_service_get_session_info(
    uint32_t session_id,
    xcp_service_session_info_t *info
);

/**
 * @brief Copy the current XCP service queue statistics.
 */
esp_err_t xcp_service_get_queue_statistics(
    xcp_service_queue_statistics_t *statistics
);

#ifdef __cplusplus
}
#endif
