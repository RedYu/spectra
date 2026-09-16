/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "unity.h"

#include "uds_protocol.h"
#include "uds_requests.h"

TEST_CASE(
    "UDS protocol encodes a request",
    "[uds]"
)
{
    const uint8_t parameters[] = {
        0xF1U,
        0x90U,
    };
    uint8_t buffer[8] = {0};
    size_t encoded_size = 0U;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_protocol_encode_request(
            UDS_SERVICE_READ_DATA_BY_IDENTIFIER,
            parameters,
            sizeof(parameters),
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
    TEST_ASSERT_EQUAL(3U, encoded_size);
    TEST_ASSERT_EQUAL_HEX8(0x22U, buffer[0]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        parameters,
        &buffer[1],
        sizeof(parameters)
    );
}

TEST_CASE(
    "UDS encodes Read DTC Information requests",
    "[uds]"
)
{
    uint8_t buffer[8] = {0};
    size_t encoded_size = 0U;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_request_encode_read_dtc_information(
            UDS_READ_DTC_REPORT_BY_STATUS_MASK,
            0xAFU,
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
    const uint8_t expected[] = {
        0x19U,
        0x02U,
        0xAFU,
    };
    TEST_ASSERT_EQUAL(sizeof(expected), encoded_size);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        expected,
        buffer,
        sizeof(expected)
    );

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_request_encode_read_dtc_information(
            UDS_READ_DTC_REPORT_SUPPORTED,
            0xFFU,
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
    TEST_ASSERT_EQUAL(2U, encoded_size);
    TEST_ASSERT_EQUAL_HEX8(0x19U, buffer[0]);
    TEST_ASSERT_EQUAL_HEX8(0x0AU, buffer[1]);
}

TEST_CASE(
    "UDS decodes DTC count and records",
    "[uds]"
)
{
    const uint8_t count_data[] = {
        0x59U, 0x01U, 0xFFU, 0x01U, 0x00U, 0x02U,
    };
    uds_response_t response = {0};
    uds_dtc_response_t dtc_response = {0};

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_protocol_decode_response(
            count_data,
            sizeof(count_data),
            UDS_SERVICE_READ_DTC_INFORMATION,
            &response
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_protocol_decode_dtc_response(
            &response,
            &dtc_response
        )
    );
    TEST_ASSERT_EQUAL_UINT16(2U, dtc_response.reported_count);
    TEST_ASSERT_EQUAL_HEX8(0xFFU, dtc_response.status_availability_mask);
    TEST_ASSERT_EQUAL_HEX8(0x01U, dtc_response.format_identifier);

    const uint8_t record_data[] = {
        0x59U, 0x02U, 0xAFU,
        0x12U, 0x34U, 0x56U, 0x09U,
        0xABU, 0xCDU, 0xEFU, 0x08U,
    };

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_protocol_decode_response(
            record_data,
            sizeof(record_data),
            UDS_SERVICE_READ_DTC_INFORMATION,
            &response
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_protocol_decode_dtc_response(
            &response,
            &dtc_response
        )
    );
    TEST_ASSERT_EQUAL(2U, dtc_response.record_count);

    uds_dtc_record_t record = {0};

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_protocol_get_dtc_record(
            &dtc_response,
            1U,
            &record
        )
    );
    TEST_ASSERT_EQUAL_HEX32(0xABCDEFU, record.code);
    TEST_ASSERT_EQUAL_HEX8(0x08U, record.status);
}

TEST_CASE(
    "UDS protocol decodes a positive response",
    "[uds]"
)
{
    const uint8_t data[] = {
        0x62U,
        0xF1U,
        0x90U,
        'S',
        'P',
        'E',
        'C',
        'T',
        'R',
        'A',
    };
    uds_response_t response = {0};

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_protocol_decode_response(
            data,
            sizeof(data),
            UDS_SERVICE_READ_DATA_BY_IDENTIFIER,
            &response
        )
    );
    TEST_ASSERT_TRUE(response.positive);
    TEST_ASSERT_EQUAL_HEX8(0x62U, response.service_id);
    TEST_ASSERT_EQUAL(9U, response.payload_length);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        &data[1],
        response.payload,
        response.payload_length
    );
}

TEST_CASE(
    "UDS protocol decodes Response Pending",
    "[uds]"
)
{
    const uint8_t data[] = {
        0x7FU,
        0x22U,
        0x78U,
    };
    uds_response_t response = {0};

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_protocol_decode_response(
            data,
            sizeof(data),
            UDS_SERVICE_READ_DATA_BY_IDENTIFIER,
            &response
        )
    );
    TEST_ASSERT_FALSE(response.positive);
    TEST_ASSERT_EQUAL_HEX8(
        UDS_NRC_RESPONSE_PENDING,
        response.negative_response_code
    );
    TEST_ASSERT_EQUAL_STRING(
        "Response pending",
        uds_protocol_negative_response_name(
            response.negative_response_code
        )
    );
}

TEST_CASE(
    "UDS protocol rejects a response for another service",
    "[uds]"
)
{
    const uint8_t positive[] = {
        0x50U,
        0x03U,
    };
    const uint8_t negative[] = {
        0x7FU,
        0x10U,
        0x22U,
    };
    uds_response_t response = {0};

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_RESPONSE,
        uds_protocol_decode_response(
            positive,
            sizeof(positive),
            UDS_SERVICE_READ_DATA_BY_IDENTIFIER,
            &response
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_RESPONSE,
        uds_protocol_decode_response(
            negative,
            sizeof(negative),
            UDS_SERVICE_READ_DATA_BY_IDENTIFIER,
            &response
        )
    );
}

TEST_CASE(
    "UDS request helpers encode common diagnostics",
    "[uds]"
)
{
    uint8_t buffer[16] = {0};
    size_t encoded_size = 0U;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_request_encode_diagnostic_session_control(
            UDS_DIAGNOSTIC_SESSION_EXTENDED,
            false,
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
    TEST_ASSERT_EQUAL(2U, encoded_size);
    TEST_ASSERT_EQUAL_HEX8(0x10U, buffer[0]);
    TEST_ASSERT_EQUAL_HEX8(0x03U, buffer[1]);

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_request_encode_tester_present(
            true,
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
    TEST_ASSERT_EQUAL_HEX8(0x3EU, buffer[0]);
    TEST_ASSERT_EQUAL_HEX8(0x80U, buffer[1]);

    const uint16_t identifiers[] = {
        0xF190U,
        0xF187U,
    };

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_request_encode_read_data_by_identifier(
            identifiers,
            2U,
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
    const uint8_t expected[] = {
        0x22U,
        0xF1U,
        0x90U,
        0xF1U,
        0x87U,
    };
    TEST_ASSERT_EQUAL(sizeof(expected), encoded_size);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        expected,
        buffer,
        sizeof(expected)
    );
}

TEST_CASE(
    "UDS request helper encodes Clear Diagnostic Information",
    "[uds]"
)
{
    uint8_t buffer[4] = {0};
    size_t encoded_size = 0U;
    const uint8_t expected[] = {
        0x14U,
        0xFFU,
        0xFFU,
        0xFFU,
    };

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_request_encode_clear_diagnostic_information(
            0xFFFFFFU,
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
    TEST_ASSERT_EQUAL(sizeof(expected), encoded_size);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        expected,
        buffer,
        sizeof(expected)
    );
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        uds_request_encode_clear_diagnostic_information(
            0x1000000U,
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
}

TEST_CASE(
    "UDS request helper encodes Write Data By Identifier",
    "[uds]"
)
{
    const uint8_t data[] = {
        0x12U,
        0x34U,
        0x56U,
    };
    uint8_t buffer[6] = {0};
    size_t encoded_size = 0U;
    const uint8_t expected[] = {
        0x2EU,
        0xF1U,
        0x90U,
        0x12U,
        0x34U,
        0x56U,
    };

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_request_encode_write_data_by_identifier(
            0xF190U,
            data,
            sizeof(data),
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
    TEST_ASSERT_EQUAL(sizeof(expected), encoded_size);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        expected,
        buffer,
        sizeof(expected)
    );
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        uds_request_encode_write_data_by_identifier(
            0xF190U,
            data,
            0U,
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_SIZE,
        uds_request_encode_write_data_by_identifier(
            0xF190U,
            data,
            sizeof(data),
            buffer,
            sizeof(buffer) - 1U,
            &encoded_size
        )
    );
}

TEST_CASE(
    "UDS request helpers encode control and memory services",
    "[uds]"
)
{
    uint8_t buffer[16] = {0};
    size_t encoded_size = 0U;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_request_encode_read_memory_by_address(
            0x12345678U,
            4U,
            0x20U,
            2U,
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
    const uint8_t read_memory[] = {
        0x23U, 0x24U,
        0x12U, 0x34U, 0x56U, 0x78U,
        0x00U, 0x20U,
    };
    TEST_ASSERT_EQUAL(sizeof(read_memory), encoded_size);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        read_memory,
        buffer,
        sizeof(read_memory)
    );

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_request_encode_communication_control(
            UDS_COMMUNICATION_DISABLE_RX_AND_TX,
            0x03U,
            true,
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
    const uint8_t communication[] = {0x28U, 0x83U, 0x03U};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        communication,
        buffer,
        sizeof(communication)
    );

    const uint8_t state[] = {0xAAU, 0x55U};
    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_request_encode_input_output_control_by_identifier(
            0xF200U,
            0x03U,
            state,
            sizeof(state),
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
    const uint8_t io_control[] = {
        0x2FU, 0xF2U, 0x00U, 0x03U, 0xAAU, 0x55U,
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        io_control,
        buffer,
        sizeof(io_control)
    );

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_request_encode_control_dtc_setting(
            UDS_DTC_SETTING_OFF,
            NULL,
            0U,
            false,
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
    TEST_ASSERT_EQUAL(2U, encoded_size);
    TEST_ASSERT_EQUAL_HEX8(0x85U, buffer[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02U, buffer[1]);
}

TEST_CASE(
    "UDS protocol decodes DTC status bits",
    "[uds]"
)
{
    uds_dtc_status_t status = {0};

    uds_protocol_decode_dtc_status(
        0x8DU,
        &status
    );

    TEST_ASSERT_TRUE(status.test_failed);
    TEST_ASSERT_FALSE(status.test_failed_this_operation_cycle);
    TEST_ASSERT_TRUE(status.pending);
    TEST_ASSERT_TRUE(status.confirmed);
    TEST_ASSERT_FALSE(status.test_not_completed_since_last_clear);
    TEST_ASSERT_FALSE(status.test_failed_since_last_clear);
    TEST_ASSERT_FALSE(status.test_not_completed_this_operation_cycle);
    TEST_ASSERT_TRUE(status.warning_indicator_requested);
}

TEST_CASE(
    "UDS request helper encodes Routine Control",
    "[uds]"
)
{
    const uint8_t option_record[] = {
        0x12U,
        0x34U,
    };
    uint8_t buffer[6] = {0};
    size_t encoded_size = 0U;
    const uint8_t expected[] = {
        0x31U,
        0x81U,
        0xFFU,
        0x00U,
        0x12U,
        0x34U,
    };

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_request_encode_routine_control(
            UDS_ROUTINE_CONTROL_START,
            0xFF00U,
            option_record,
            sizeof(option_record),
            true,
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
    TEST_ASSERT_EQUAL(sizeof(expected), encoded_size);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        expected,
        buffer,
        sizeof(expected)
    );
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        uds_request_encode_routine_control(
            0x04U,
            0xFF00U,
            NULL,
            0U,
            false,
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_request_encode_routine_control(
            UDS_ROUTINE_CONTROL_REQUEST_RESULTS,
            0xFF00U,
            NULL,
            0U,
            false,
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
    TEST_ASSERT_EQUAL(4U, encoded_size);
    TEST_ASSERT_EQUAL_HEX8(0x31U, buffer[0]);
    TEST_ASSERT_EQUAL_HEX8(0x03U, buffer[1]);
}

TEST_CASE(
    "UDS request helpers encode Security Access",
    "[uds]"
)
{
    const uint8_t key[] = {
        0x12U,
        0x34U,
        0x56U,
        0x78U,
    };
    uint8_t buffer[6] = {0};
    size_t encoded_size = 0U;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_request_encode_security_access_request_seed(
            0x01U,
            NULL,
            0U,
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
    TEST_ASSERT_EQUAL(2U, encoded_size);
    TEST_ASSERT_EQUAL_HEX8(0x27U, buffer[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01U, buffer[1]);

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_request_encode_security_access_send_key(
            0x01U,
            key,
            sizeof(key),
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
    TEST_ASSERT_EQUAL(6U, encoded_size);
    TEST_ASSERT_EQUAL_HEX8(0x27U, buffer[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02U, buffer[1]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        key,
        &buffer[2],
        sizeof(key)
    );
}

TEST_CASE(
    "UDS Security Access rejects invalid levels and empty keys",
    "[uds]"
)
{
    const uint8_t key = 0x12U;
    uint8_t buffer[3] = {0};
    size_t encoded_size = 0U;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        uds_request_encode_security_access_request_seed(
            0x02U,
            NULL,
            0U,
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        uds_request_encode_security_access_request_seed(
            0x7FU,
            NULL,
            0U,
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        uds_request_encode_security_access_send_key(
            0x01U,
            &key,
            0U,
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
}

TEST_CASE(
    "UDS request helper encodes Request Download",
    "[uds]"
)
{
    uint8_t buffer[11] = {0};
    size_t encoded_size = 0U;
    const uint8_t expected[] = {
        0x34U,
        0x00U,
        0x44U,
        0x12U,
        0x34U,
        0x56U,
        0x78U,
        0x00U,
        0x01U,
        0x00U,
        0x00U,
    };

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_request_encode_request_download(
            0x00U,
            0x12345678U,
            4U,
            0x00010000U,
            4U,
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
    TEST_ASSERT_EQUAL(sizeof(expected), encoded_size);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        expected,
        buffer,
        sizeof(expected)
    );
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        uds_request_encode_request_download(
            0x00U,
            0x100U,
            1U,
            1U,
            1U,
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
}

TEST_CASE(
    "UDS protocol decodes Request Download response",
    "[uds]"
)
{
    const uint8_t payload[] = {
        0x20U,
        0x04U,
        0x02U,
    };
    const uds_response_t response = {
        .positive = true,
        .service_id = 0x74U,
        .request_service_id = UDS_SERVICE_REQUEST_DOWNLOAD,
        .payload = payload,
        .payload_length = sizeof(payload),
    };
    uds_request_download_response_t download = {0};

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_protocol_decode_request_download_response(
            &response,
            &download
        )
    );
    TEST_ASSERT_EQUAL(2U, download.maximum_block_length_size);
    TEST_ASSERT_EQUAL_UINT64(0x0402U, download.maximum_block_length);
}

TEST_CASE(
    "UDS request helpers encode transfer data and exit",
    "[uds]"
)
{
    const uint8_t data[] = {
        0xAAU,
        0x55U,
    };
    uint8_t buffer[4] = {0};
    size_t encoded_size = 0U;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_request_encode_transfer_data(
            0x01U,
            data,
            sizeof(data),
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
    TEST_ASSERT_EQUAL(4U, encoded_size);
    TEST_ASSERT_EQUAL_HEX8(0x36U, buffer[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01U, buffer[1]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, &buffer[2], sizeof(data));

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_request_encode_request_transfer_exit(
            NULL,
            0U,
            buffer,
            sizeof(buffer),
            &encoded_size
        )
    );
    TEST_ASSERT_EQUAL(1U, encoded_size);
    TEST_ASSERT_EQUAL_HEX8(0x37U, buffer[0]);
}

TEST_CASE(
    "UDS protocol decodes transfer responses",
    "[uds]"
)
{
    const uint8_t block_payload[] = {
        0x7FU,
        0x12U,
        0x34U,
    };
    const uds_response_t block_response = {
        .positive = true,
        .service_id = 0x76U,
        .request_service_id = UDS_SERVICE_TRANSFER_DATA,
        .payload = block_payload,
        .payload_length = sizeof(block_payload),
    };
    uds_transfer_data_response_t block = {0};

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_protocol_decode_transfer_data_response(
            &block_response,
            &block
        )
    );
    TEST_ASSERT_EQUAL_HEX8(0x7FU, block.block_sequence_counter);
    TEST_ASSERT_EQUAL(2U, block.parameter_record_length);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        &block_payload[1],
        block.parameter_record,
        block.parameter_record_length
    );

    const uds_response_t exit_response = {
        .positive = true,
        .service_id = 0x77U,
        .request_service_id = UDS_SERVICE_REQUEST_TRANSFER_EXIT,
        .payload = NULL,
        .payload_length = 0U,
    };
    uds_transfer_exit_response_t transfer_exit = {0};

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_protocol_decode_transfer_exit_response(
            &exit_response,
            &transfer_exit
        )
    );
    TEST_ASSERT_NULL(transfer_exit.parameter_record);
    TEST_ASSERT_EQUAL(0U, transfer_exit.parameter_record_length);
}
