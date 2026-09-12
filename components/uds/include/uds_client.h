/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "isotp_service.h"
#include "uds_protocol.h"
#include "uds_requests.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UDS_CLIENT_DEFAULT_P2_TIMEOUT_US       (1000000ULL)
#define UDS_CLIENT_DEFAULT_P2_STAR_TIMEOUT_US  (5000000ULL)
#define UDS_CLIENT_WRITE_DATA_MAX_LENGTH       (256U)

typedef enum
{
    UDS_CLIENT_CLOSED = 0,
    UDS_CLIENT_IDLE,
    UDS_CLIENT_TRANSMITTING,
    UDS_CLIENT_WAIT_RESPONSE,
    UDS_CLIENT_RESPONSE_PENDING,
    UDS_CLIENT_COMPLETE,
    UDS_CLIENT_NEGATIVE_RESPONSE,
    UDS_CLIENT_ERROR,

} uds_client_state_t;

typedef enum
{
    UDS_CLIENT_EVENT_TRANSMITTED = 0,
    UDS_CLIENT_EVENT_RESPONSE,
    UDS_CLIENT_EVENT_NEGATIVE_RESPONSE,
    UDS_CLIENT_EVENT_RESPONSE_PENDING,
    UDS_CLIENT_EVENT_TIMEOUT,
    UDS_CLIENT_EVENT_TRANSPORT_ERROR,
    UDS_CLIENT_EVENT_PROTOCOL_ERROR,

} uds_client_event_type_t;

typedef struct
{
    uds_client_event_type_t type;
    uds_response_t response;
    esp_err_t result;
    isotp_session_error_t transport_error;

} uds_client_event_t;

typedef void (*uds_client_event_cb_t)(
    const uds_client_event_t *event,
    void *context
);

typedef struct
{
    isotp_service_channel_config_t transport;
    uint64_t p2_timeout_us;
    uint64_t p2_star_timeout_us;
    uds_client_event_cb_t callback;
    void *callback_context;

} uds_client_config_t;

typedef struct
{
    uds_client_config_t config;
    uint32_t channel_id;
    uds_client_state_t state;
    uint8_t request_service_id;
    bool response_expected;
    uint64_t deadline_us;
    esp_err_t last_result;
    uint8_t last_negative_response_code;

} uds_client_t;

esp_err_t uds_client_open(
    uds_client_t *client,
    const uds_client_config_t *config
);

esp_err_t uds_client_close(
    uds_client_t *client
);

esp_err_t uds_client_request(
    uds_client_t *client,
    uint8_t service_id,
    const uint8_t *parameters,
    size_t parameter_length,
    uint64_t now_us
);

esp_err_t uds_client_diagnostic_session_control(
    uds_client_t *client,
    uint8_t session_type,
    bool suppress_positive_response,
    uint64_t now_us
);

esp_err_t uds_client_ecu_reset(
    uds_client_t *client,
    uint8_t reset_type,
    bool suppress_positive_response,
    uint64_t now_us
);

esp_err_t uds_client_tester_present(
    uds_client_t *client,
    bool suppress_positive_response,
    uint64_t now_us
);

esp_err_t uds_client_read_data_by_identifier(
    uds_client_t *client,
    uint16_t identifier,
    uint64_t now_us
);

esp_err_t uds_client_write_data_by_identifier(
    uds_client_t *client,
    uint16_t identifier,
    const uint8_t *data,
    size_t data_length,
    uint64_t now_us
);

esp_err_t uds_client_read_dtc_information(
    uds_client_t *client,
    uint8_t subfunction,
    uint8_t status_mask,
    uint64_t now_us
);

esp_err_t uds_client_clear_diagnostic_information(
    uds_client_t *client,
    uint32_t group_of_dtc,
    uint64_t now_us
);

esp_err_t uds_client_poll(
    uds_client_t *client,
    uint64_t now_us
);

bool uds_client_busy(
    const uds_client_t *client
);

#ifdef __cplusplus
}
#endif
