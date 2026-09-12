/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "uds_requests.h"

#include <string.h>

#include "uds_protocol.h"

static esp_err_t uds_request_encode_subfunction(
    uint8_t service_id,
    uint8_t subfunction,
    bool suppress_positive_response,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if ((subfunction == 0U) ||
        ((subfunction & UDS_SUPPRESS_POSITIVE_RESPONSE) != 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t parameter =
        subfunction |
        (suppress_positive_response
            ? UDS_SUPPRESS_POSITIVE_RESPONSE
            : 0U);

    return uds_protocol_encode_request(
        service_id,
        &parameter,
        sizeof(parameter),
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t uds_request_encode_diagnostic_session_control(
    uint8_t session_type,
    bool suppress_positive_response,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    return uds_request_encode_subfunction(
        UDS_SERVICE_DIAGNOSTIC_SESSION_CONTROL,
        session_type,
        suppress_positive_response,
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t uds_request_encode_ecu_reset(
    uint8_t reset_type,
    bool suppress_positive_response,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    return uds_request_encode_subfunction(
        UDS_SERVICE_ECU_RESET,
        reset_type,
        suppress_positive_response,
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t uds_request_encode_tester_present(
    bool suppress_positive_response,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    const uint8_t subfunction =
        suppress_positive_response
            ? UDS_SUPPRESS_POSITIVE_RESPONSE
            : 0U;

    return uds_protocol_encode_request(
        UDS_SERVICE_TESTER_PRESENT,
        &subfunction,
        sizeof(subfunction),
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t uds_request_encode_read_data_by_identifier(
    const uint16_t *identifiers,
    size_t identifier_count,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if ((identifiers == NULL) ||
        (identifier_count == 0U) ||
        (buffer == NULL) ||
        (encoded_size == NULL) ||
        (identifier_count > ((SIZE_MAX - 1U) / 2U))) {

        return ESP_ERR_INVALID_ARG;
    }

    const size_t required_size =
        1U + (identifier_count * 2U);

    if (capacity < required_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    buffer[0] = UDS_SERVICE_READ_DATA_BY_IDENTIFIER;

    for (size_t index = 0U;
         index < identifier_count;
         ++index) {

        const size_t offset = 1U + (index * 2U);

        buffer[offset] =
            (uint8_t)(identifiers[index] >> 8U);
        buffer[offset + 1U] =
            (uint8_t)identifiers[index];
    }

    *encoded_size = required_size;
    return ESP_OK;
}

esp_err_t uds_request_encode_write_data_by_identifier(
    uint16_t identifier,
    const uint8_t *data,
    size_t data_length,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if ((data == NULL) ||
        (data_length == 0U) ||
        (buffer == NULL) ||
        (encoded_size == NULL) ||
        (data_length > (SIZE_MAX - 3U))) {

        return ESP_ERR_INVALID_ARG;
    }

    const size_t required_size = 3U + data_length;

    if (capacity < required_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    buffer[0] = UDS_SERVICE_WRITE_DATA_BY_IDENTIFIER;
    buffer[1] = (uint8_t)(identifier >> 8U);
    buffer[2] = (uint8_t)identifier;

    memcpy(
        &buffer[3],
        data,
        data_length
    );

    *encoded_size = required_size;
    return ESP_OK;
}

esp_err_t uds_request_encode_read_dtc_information(
    uint8_t subfunction,
    uint8_t status_mask,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    uint8_t parameters[2] = {
        subfunction,
        status_mask,
    };
    size_t parameter_length = sizeof(parameters);

    if (subfunction == UDS_READ_DTC_REPORT_SUPPORTED) {
        parameter_length = 1U;
    } else if ((subfunction !=
                UDS_READ_DTC_REPORT_NUMBER_BY_STATUS_MASK) &&
               (subfunction !=
                UDS_READ_DTC_REPORT_BY_STATUS_MASK)) {

        return ESP_ERR_INVALID_ARG;
    }

    return uds_protocol_encode_request(
        UDS_SERVICE_READ_DTC_INFORMATION,
        parameters,
        parameter_length,
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t uds_request_encode_clear_diagnostic_information(
    uint32_t group_of_dtc,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if (group_of_dtc > 0xFFFFFFU) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t parameters[3] = {
        (uint8_t)(group_of_dtc >> 16U),
        (uint8_t)(group_of_dtc >> 8U),
        (uint8_t)group_of_dtc,
    };

    return uds_protocol_encode_request(
        UDS_SERVICE_CLEAR_DIAGNOSTIC_INFORMATION,
        parameters,
        sizeof(parameters),
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t uds_request_encode_routine_control(
    uint8_t control_type,
    uint16_t routine_identifier,
    const uint8_t *option_record,
    size_t option_record_length,
    bool suppress_positive_response,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if ((control_type < UDS_ROUTINE_CONTROL_START) ||
        (control_type > UDS_ROUTINE_CONTROL_REQUEST_RESULTS) ||
        ((option_record == NULL) && (option_record_length != 0U)) ||
        (buffer == NULL) ||
        (encoded_size == NULL) ||
        (option_record_length > (SIZE_MAX - 4U))) {

        return ESP_ERR_INVALID_ARG;
    }

    const size_t required_size = 4U + option_record_length;

    if (capacity < required_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    buffer[0] = UDS_SERVICE_ROUTINE_CONTROL;
    buffer[1] =
        control_type |
        (suppress_positive_response
            ? UDS_SUPPRESS_POSITIVE_RESPONSE
            : 0U);
    buffer[2] = (uint8_t)(routine_identifier >> 8U);
    buffer[3] = (uint8_t)routine_identifier;

    if (option_record_length != 0U) {
        memcpy(
            &buffer[4],
            option_record,
            option_record_length
        );
    }

    *encoded_size = required_size;
    return ESP_OK;
}

static bool uds_request_security_level_valid(
    uint8_t security_level
)
{
    return (security_level >= UDS_SECURITY_ACCESS_LEVEL_MIN) &&
           (security_level <= UDS_SECURITY_ACCESS_LEVEL_MAX) &&
           ((security_level & 1U) != 0U);
}

esp_err_t uds_request_encode_security_access_request_seed(
    uint8_t security_level,
    const uint8_t *data_record,
    size_t data_record_length,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if (!uds_request_security_level_valid(security_level) ||
        ((data_record == NULL) && (data_record_length != 0U)) ||
        (buffer == NULL) ||
        (encoded_size == NULL) ||
        (data_record_length > (SIZE_MAX - 2U))) {

        return ESP_ERR_INVALID_ARG;
    }

    const size_t required_size = 2U + data_record_length;

    if (capacity < required_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    buffer[0] = UDS_SERVICE_SECURITY_ACCESS;
    buffer[1] = security_level;

    if (data_record_length != 0U) {
        memcpy(
            &buffer[2],
            data_record,
            data_record_length
        );
    }

    *encoded_size = required_size;
    return ESP_OK;
}

esp_err_t uds_request_encode_security_access_send_key(
    uint8_t security_level,
    const uint8_t *key,
    size_t key_length,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if (!uds_request_security_level_valid(security_level) ||
        (key == NULL) ||
        (key_length == 0U) ||
        (buffer == NULL) ||
        (encoded_size == NULL) ||
        (key_length > (SIZE_MAX - 2U))) {

        return ESP_ERR_INVALID_ARG;
    }

    const size_t required_size = 2U + key_length;

    if (capacity < required_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    buffer[0] = UDS_SERVICE_SECURITY_ACCESS;
    buffer[1] = security_level + 1U;

    memcpy(
        &buffer[2],
        key,
        key_length
    );

    *encoded_size = required_size;
    return ESP_OK;
}

static bool uds_request_value_fits_length(
    uint64_t value,
    uint8_t length
)
{
    if ((length == 0U) || (length > sizeof(uint64_t))) {
        return false;
    }

    return (length == sizeof(uint64_t)) ||
           (value < (1ULL << (length * 8U)));
}

static void uds_request_write_big_endian(
    uint8_t *destination,
    uint64_t value,
    uint8_t length
)
{
    for (uint8_t index = 0U; index < length; ++index) {
        destination[index] =
            (uint8_t)(value >>
                      ((length - index - 1U) * 8U));
    }
}

esp_err_t uds_request_encode_request_download(
    uint8_t data_format_identifier,
    uint64_t memory_address,
    uint8_t memory_address_length,
    uint64_t memory_size,
    uint8_t memory_size_length,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if ((memory_size == 0U) ||
        !uds_request_value_fits_length(
            memory_address,
            memory_address_length
        ) ||
        !uds_request_value_fits_length(
            memory_size,
            memory_size_length
        ) ||
        (buffer == NULL) ||
        (encoded_size == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    const size_t required_size =
        3U +
        memory_address_length +
        memory_size_length;

    if (capacity < required_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    buffer[0] = UDS_SERVICE_REQUEST_DOWNLOAD;
    buffer[1] = data_format_identifier;
    buffer[2] =
        (uint8_t)((memory_size_length << 4U) |
                  memory_address_length);

    uds_request_write_big_endian(
        &buffer[3],
        memory_address,
        memory_address_length
    );
    uds_request_write_big_endian(
        &buffer[3U + memory_address_length],
        memory_size,
        memory_size_length
    );

    *encoded_size = required_size;
    return ESP_OK;
}

esp_err_t uds_request_encode_transfer_data(
    uint8_t block_sequence_counter,
    const uint8_t *data,
    size_t data_length,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if ((data == NULL) ||
        (data_length == 0U) ||
        (buffer == NULL) ||
        (encoded_size == NULL) ||
        (data_length > (SIZE_MAX - 2U))) {

        return ESP_ERR_INVALID_ARG;
    }

    const size_t required_size = 2U + data_length;

    if (capacity < required_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    buffer[0] = UDS_SERVICE_TRANSFER_DATA;
    buffer[1] = block_sequence_counter;

    memcpy(
        &buffer[2],
        data,
        data_length
    );

    *encoded_size = required_size;
    return ESP_OK;
}

esp_err_t uds_request_encode_request_transfer_exit(
    const uint8_t *parameter_record,
    size_t parameter_record_length,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if (((parameter_record == NULL) &&
         (parameter_record_length != 0U)) ||
        (buffer == NULL) ||
        (encoded_size == NULL) ||
        (parameter_record_length > (SIZE_MAX - 1U))) {

        return ESP_ERR_INVALID_ARG;
    }

    const size_t required_size = 1U + parameter_record_length;

    if (capacity < required_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    buffer[0] = UDS_SERVICE_REQUEST_TRANSFER_EXIT;

    if (parameter_record_length != 0U) {
        memcpy(
            &buffer[1],
            parameter_record,
            parameter_record_length
        );
    }

    *encoded_size = required_size;
    return ESP_OK;
}
