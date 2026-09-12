/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    UDS_DIAGNOSTIC_SESSION_DEFAULT = 0x01,
    UDS_DIAGNOSTIC_SESSION_PROGRAMMING = 0x02,
    UDS_DIAGNOSTIC_SESSION_EXTENDED = 0x03,
    UDS_DIAGNOSTIC_SESSION_SAFETY_SYSTEM = 0x04,

} uds_diagnostic_session_type_t;

typedef enum
{
    UDS_RESET_HARD = 0x01,
    UDS_RESET_KEY_OFF_ON = 0x02,
    UDS_RESET_SOFT = 0x03,
    UDS_RESET_ENABLE_RAPID_POWER_SHUTDOWN = 0x04,
    UDS_RESET_DISABLE_RAPID_POWER_SHUTDOWN = 0x05,

} uds_reset_type_t;

esp_err_t uds_request_encode_diagnostic_session_control(
    uint8_t session_type,
    bool suppress_positive_response,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);

esp_err_t uds_request_encode_ecu_reset(
    uint8_t reset_type,
    bool suppress_positive_response,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);

esp_err_t uds_request_encode_tester_present(
    bool suppress_positive_response,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);

esp_err_t uds_request_encode_read_data_by_identifier(
    const uint16_t *identifiers,
    size_t identifier_count,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);

#ifdef __cplusplus
}
#endif
