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

typedef enum
{
    UDS_ROUTINE_CONTROL_START = 0x01,
    UDS_ROUTINE_CONTROL_STOP = 0x02,
    UDS_ROUTINE_CONTROL_REQUEST_RESULTS = 0x03,

} uds_routine_control_type_t;

#define UDS_SECURITY_ACCESS_LEVEL_MIN  (0x01U)
#define UDS_SECURITY_ACCESS_LEVEL_MAX  (0x7DU)

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

esp_err_t uds_request_encode_write_data_by_identifier(
    uint16_t identifier,
    const uint8_t *data,
    size_t data_length,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);

esp_err_t uds_request_encode_read_dtc_information(
    uint8_t subfunction,
    uint8_t status_mask,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);

esp_err_t uds_request_encode_clear_diagnostic_information(
    uint32_t group_of_dtc,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);

esp_err_t uds_request_encode_routine_control(
    uint8_t control_type,
    uint16_t routine_identifier,
    const uint8_t *option_record,
    size_t option_record_length,
    bool suppress_positive_response,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);

esp_err_t uds_request_encode_security_access_request_seed(
    uint8_t security_level,
    const uint8_t *data_record,
    size_t data_record_length,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);

esp_err_t uds_request_encode_security_access_send_key(
    uint8_t security_level,
    const uint8_t *key,
    size_t key_length,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);

esp_err_t uds_request_encode_request_download(
    uint8_t data_format_identifier,
    uint64_t memory_address,
    uint8_t memory_address_length,
    uint64_t memory_size,
    uint8_t memory_size_length,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);

esp_err_t uds_request_encode_transfer_data(
    uint8_t block_sequence_counter,
    const uint8_t *data,
    size_t data_length,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);

esp_err_t uds_request_encode_request_transfer_exit(
    const uint8_t *parameter_record,
    size_t parameter_record_length,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);

#ifdef __cplusplus
}
#endif
