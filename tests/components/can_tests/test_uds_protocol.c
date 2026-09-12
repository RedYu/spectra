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
