/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */
#include "unity.h"
#include "xcp_commands.h"
#include "xcp_protocol.h"
TEST_CASE("XCP encodes standard CTO commands", "[xcp]")
{
    uint8_t data[8] = {0};
    size_t size = 0U;
    TEST_ASSERT_EQUAL(
        ESP_OK,
        xcp_command_encode_connect(0U, data, sizeof(data), &size)
    );
    TEST_ASSERT_EQUAL(2U, size);
    TEST_ASSERT_EQUAL_HEX8(XCP_COMMAND_CONNECT, data[0]);
    TEST_ASSERT_EQUAL_HEX8(0U, data[1]);
    TEST_ASSERT_EQUAL(
        ESP_OK,
        xcp_command_encode_short_upload(
            4U, 1U, 0x12345678U, false, data, sizeof(data), &size
        )
    );
    TEST_ASSERT_EQUAL(8U, size);
    TEST_ASSERT_EQUAL_HEX8(0x78U, data[4]);
    TEST_ASSERT_EQUAL_HEX8(0x12U, data[7]);
}
TEST_CASE("XCP decodes RES ERR and DAQ packets", "[xcp]")
{
    xcp_packet_t packet = {0};
    const uint8_t response[] = {XCP_PID_RES, 1U, 2U};
    TEST_ASSERT_EQUAL(
        ESP_OK,
        xcp_protocol_decode_packet(response, sizeof(response), &packet)
    );
    TEST_ASSERT_EQUAL(XCP_PACKET_RESPONSE, packet.type);
    TEST_ASSERT_EQUAL(2U, packet.payload_length);
    const uint8_t error[] = {XCP_PID_ERR, 0x22U};
    TEST_ASSERT_EQUAL(
        ESP_OK,
        xcp_protocol_decode_packet(error, sizeof(error), &packet)
    );
    TEST_ASSERT_EQUAL(XCP_PACKET_ERROR, packet.type);
    TEST_ASSERT_EQUAL_HEX8(0x22U, packet.code);
    const uint8_t dto[] = {0x01U, 0xAAU};
    TEST_ASSERT_EQUAL(
        ESP_OK,
        xcp_protocol_decode_packet(dto, sizeof(dto), &packet)
    );
    TEST_ASSERT_EQUAL(XCP_PACKET_DAQ, packet.type);
}
TEST_CASE("XCP decodes CONNECT capabilities", "[xcp]")
{
    const uint8_t data[] = {
        XCP_PID_RES, 0x1DU, 0x01U, 8U, 0x04U, 0x00U, 0x10U, 0x10U
    };
    xcp_packet_t packet = {0};
    xcp_connect_response_t response = {0};
    TEST_ASSERT_EQUAL(
        ESP_OK,
        xcp_protocol_decode_packet(data, sizeof(data), &packet)
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        xcp_protocol_decode_connect_response(&packet, &response)
    );
    TEST_ASSERT_EQUAL(8U, response.maximum_cto);
    TEST_ASSERT_EQUAL(1024U, response.maximum_dto);
    TEST_ASSERT_TRUE(response.byte_order_big_endian);
}
