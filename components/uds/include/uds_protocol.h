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

#define UDS_POSITIVE_RESPONSE_OFFSET     (0x40U)
#define UDS_NEGATIVE_RESPONSE_SID        (0x7FU)
#define UDS_SUPPRESS_POSITIVE_RESPONSE   (0x80U)
#define UDS_REQUEST_SERVICE_ID_MAX       (0xBFU)

typedef enum
{
    UDS_READ_DTC_REPORT_NUMBER_BY_STATUS_MASK = 0x01,
    UDS_READ_DTC_REPORT_BY_STATUS_MASK = 0x02,
    UDS_READ_DTC_REPORT_SUPPORTED = 0x0A,

} uds_read_dtc_subfunction_t;

typedef enum
{
    UDS_SERVICE_DIAGNOSTIC_SESSION_CONTROL = 0x10,
    UDS_SERVICE_ECU_RESET = 0x11,
    UDS_SERVICE_CLEAR_DIAGNOSTIC_INFORMATION = 0x14,
    UDS_SERVICE_READ_DTC_INFORMATION = 0x19,
    UDS_SERVICE_READ_DATA_BY_IDENTIFIER = 0x22,
    UDS_SERVICE_SECURITY_ACCESS = 0x27,
    UDS_SERVICE_COMMUNICATION_CONTROL = 0x28,
    UDS_SERVICE_WRITE_DATA_BY_IDENTIFIER = 0x2E,
    UDS_SERVICE_INPUT_OUTPUT_CONTROL_BY_IDENTIFIER = 0x2F,
    UDS_SERVICE_ROUTINE_CONTROL = 0x31,
    UDS_SERVICE_REQUEST_DOWNLOAD = 0x34,
    UDS_SERVICE_TRANSFER_DATA = 0x36,
    UDS_SERVICE_REQUEST_TRANSFER_EXIT = 0x37,
    UDS_SERVICE_TESTER_PRESENT = 0x3E,
    UDS_SERVICE_CONTROL_DTC_SETTING = 0x85,

} uds_service_id_t;

typedef enum
{
    UDS_NRC_GENERAL_REJECT = 0x10,
    UDS_NRC_SERVICE_NOT_SUPPORTED = 0x11,
    UDS_NRC_SUBFUNCTION_NOT_SUPPORTED = 0x12,
    UDS_NRC_INCORRECT_MESSAGE_LENGTH_OR_FORMAT = 0x13,
    UDS_NRC_RESPONSE_TOO_LONG = 0x14,
    UDS_NRC_BUSY_REPEAT_REQUEST = 0x21,
    UDS_NRC_CONDITIONS_NOT_CORRECT = 0x22,
    UDS_NRC_REQUEST_SEQUENCE_ERROR = 0x24,
    UDS_NRC_NO_RESPONSE_FROM_SUBNET_COMPONENT = 0x25,
    UDS_NRC_FAILURE_PREVENTS_EXECUTION = 0x26,
    UDS_NRC_REQUEST_OUT_OF_RANGE = 0x31,
    UDS_NRC_SECURITY_ACCESS_DENIED = 0x33,
    UDS_NRC_INVALID_KEY = 0x35,
    UDS_NRC_EXCEEDED_NUMBER_OF_ATTEMPTS = 0x36,
    UDS_NRC_REQUIRED_TIME_DELAY_NOT_EXPIRED = 0x37,
    UDS_NRC_UPLOAD_DOWNLOAD_NOT_ACCEPTED = 0x70,
    UDS_NRC_TRANSFER_DATA_SUSPENDED = 0x71,
    UDS_NRC_GENERAL_PROGRAMMING_FAILURE = 0x72,
    UDS_NRC_WRONG_BLOCK_SEQUENCE_COUNTER = 0x73,
    UDS_NRC_RESPONSE_PENDING = 0x78,
    UDS_NRC_SUBFUNCTION_NOT_SUPPORTED_IN_ACTIVE_SESSION = 0x7E,
    UDS_NRC_SERVICE_NOT_SUPPORTED_IN_ACTIVE_SESSION = 0x7F,
    UDS_NRC_RPM_TOO_HIGH = 0x81,
    UDS_NRC_RPM_TOO_LOW = 0x82,
    UDS_NRC_ENGINE_IS_RUNNING = 0x83,
    UDS_NRC_ENGINE_IS_NOT_RUNNING = 0x84,
    UDS_NRC_ENGINE_RUN_TIME_TOO_LOW = 0x85,
    UDS_NRC_TEMPERATURE_TOO_HIGH = 0x86,
    UDS_NRC_TEMPERATURE_TOO_LOW = 0x87,
    UDS_NRC_VEHICLE_SPEED_TOO_HIGH = 0x88,
    UDS_NRC_VEHICLE_SPEED_TOO_LOW = 0x89,
    UDS_NRC_VOLTAGE_TOO_HIGH = 0x92,
    UDS_NRC_VOLTAGE_TOO_LOW = 0x93,

} uds_negative_response_code_t;

typedef struct
{
    bool positive;
    uint8_t service_id;
    uint8_t request_service_id;
    uint8_t negative_response_code;
    const uint8_t *payload;
    size_t payload_length;

} uds_response_t;

typedef struct
{
    uint32_t code;
    uint8_t status;

} uds_dtc_record_t;

typedef struct
{
    bool test_failed;
    bool test_failed_this_operation_cycle;
    bool pending;
    bool confirmed;
    bool test_not_completed_since_last_clear;
    bool test_failed_since_last_clear;
    bool test_not_completed_this_operation_cycle;
    bool warning_indicator_requested;

} uds_dtc_status_t;

typedef struct
{
    uint8_t subfunction;
    uint8_t status_availability_mask;
    uint8_t format_identifier;
    uint16_t reported_count;
    const uint8_t *record_data;
    size_t record_count;

} uds_dtc_response_t;

esp_err_t uds_protocol_encode_request(
    uint8_t service_id,
    const uint8_t *parameters,
    size_t parameter_length,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);

esp_err_t uds_protocol_decode_response(
    const uint8_t *data,
    size_t size,
    uint8_t expected_request_service_id,
    uds_response_t *response
);

const char *uds_protocol_negative_response_name(
    uint8_t negative_response_code
);

esp_err_t uds_protocol_decode_dtc_response(
    const uds_response_t *response,
    uds_dtc_response_t *dtc_response
);

esp_err_t uds_protocol_get_dtc_record(
    const uds_dtc_response_t *response,
    size_t index,
    uds_dtc_record_t *record
);

void uds_protocol_decode_dtc_status(
    uint8_t status_byte,
    uds_dtc_status_t *status
);

#ifdef __cplusplus
}
#endif
