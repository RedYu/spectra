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
