/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "uds_protocol.h"

#include <string.h>

esp_err_t uds_protocol_encode_request(
    uint8_t service_id,
    const uint8_t *parameters,
    size_t parameter_length,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if ((service_id == 0U) ||
        (service_id > UDS_REQUEST_SERVICE_ID_MAX) ||
        (service_id > 0xBFU) ||
        (service_id == UDS_NEGATIVE_RESPONSE_SID) ||
        ((parameters == NULL) && (parameter_length != 0U)) ||
        (buffer == NULL) ||
        (encoded_size == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    if ((parameter_length == SIZE_MAX) ||
        (capacity < (parameter_length + 1U))) {

        return ESP_ERR_INVALID_SIZE;
    }

    buffer[0] = service_id;

    if (parameter_length != 0U) {
        memcpy(
            &buffer[1],
            parameters,
            parameter_length
        );
    }

    *encoded_size = parameter_length + 1U;
    return ESP_OK;
}

esp_err_t uds_protocol_decode_response(
    const uint8_t *data,
    size_t size,
    uint8_t expected_request_service_id,
    uds_response_t *response
)
{
    if ((data == NULL) ||
        (size == 0U) ||
        (expected_request_service_id == 0U) ||
        (expected_request_service_id >
         UDS_REQUEST_SERVICE_ID_MAX) ||
        (expected_request_service_id > 0xBFU) ||
        (response == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    memset(
        response,
        0,
        sizeof(*response)
    );

    if (data[0] == UDS_NEGATIVE_RESPONSE_SID) {
        if ((size < 3U) ||
            (data[1] != expected_request_service_id)) {

            return ESP_ERR_INVALID_RESPONSE;
        }

        response->positive = false;
        response->service_id = data[0];
        response->request_service_id = data[1];
        response->negative_response_code = data[2];
        response->payload = &data[3];
        response->payload_length = size - 3U;

        return ESP_OK;
    }

    const uint8_t expected_positive_service_id =
        expected_request_service_id +
        UDS_POSITIVE_RESPONSE_OFFSET;

    if (data[0] != expected_positive_service_id) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    response->positive = true;
    response->service_id = data[0];
    response->request_service_id = expected_request_service_id;
    response->payload = &data[1];
    response->payload_length = size - 1U;

    return ESP_OK;
}

const char *uds_protocol_negative_response_name(
    uint8_t negative_response_code
)
{
    switch (negative_response_code) {
        case UDS_NRC_GENERAL_REJECT:
            return "General reject";
        case UDS_NRC_SERVICE_NOT_SUPPORTED:
            return "Service not supported";
        case UDS_NRC_SUBFUNCTION_NOT_SUPPORTED:
            return "Sub-function not supported";
        case UDS_NRC_INCORRECT_MESSAGE_LENGTH_OR_FORMAT:
            return "Incorrect message length or format";
        case UDS_NRC_RESPONSE_TOO_LONG:
            return "Response too long";
        case UDS_NRC_BUSY_REPEAT_REQUEST:
            return "Busy, repeat request";
        case UDS_NRC_CONDITIONS_NOT_CORRECT:
            return "Conditions not correct";
        case UDS_NRC_REQUEST_SEQUENCE_ERROR:
            return "Request sequence error";
        case UDS_NRC_REQUEST_OUT_OF_RANGE:
            return "Request out of range";
        case UDS_NRC_SECURITY_ACCESS_DENIED:
            return "Security access denied";
        case UDS_NRC_INVALID_KEY:
            return "Invalid key";
        case UDS_NRC_EXCEEDED_NUMBER_OF_ATTEMPTS:
            return "Exceeded number of attempts";
        case UDS_NRC_REQUIRED_TIME_DELAY_NOT_EXPIRED:
            return "Required time delay not expired";
        case UDS_NRC_RESPONSE_PENDING:
            return "Response pending";
        case UDS_NRC_SUBFUNCTION_NOT_SUPPORTED_IN_ACTIVE_SESSION:
            return "Sub-function not supported in active session";
        case UDS_NRC_SERVICE_NOT_SUPPORTED_IN_ACTIVE_SESSION:
            return "Service not supported in active session";
        case UDS_NRC_RPM_TOO_HIGH:
            return "RPM too high";
        case UDS_NRC_RPM_TOO_LOW:
            return "RPM too low";
        case UDS_NRC_ENGINE_IS_RUNNING:
            return "Engine is running";
        case UDS_NRC_ENGINE_IS_NOT_RUNNING:
            return "Engine is not running";
        case UDS_NRC_TEMPERATURE_TOO_HIGH:
            return "Temperature too high";
        case UDS_NRC_TEMPERATURE_TOO_LOW:
            return "Temperature too low";
        case UDS_NRC_VEHICLE_SPEED_TOO_HIGH:
            return "Vehicle speed too high";
        case UDS_NRC_VEHICLE_SPEED_TOO_LOW:
            return "Vehicle speed too low";
        case UDS_NRC_VOLTAGE_TOO_HIGH:
            return "Voltage too high";
        case UDS_NRC_VOLTAGE_TOO_LOW:
            return "Voltage too low";
        default:
            return "Unknown negative response";
    }
}
