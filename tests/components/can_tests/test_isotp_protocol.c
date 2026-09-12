/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "unity.h"

#include "isotp_protocol.h"

TEST_CASE(
    "ISO-TP encodes and decodes Single Frames",
    "[isotp]"
)
{
    uint8_t frame[64] = {0};
    uint8_t offset = 0U;
    isotp_pci_t pci;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_protocol_encode_single_frame(
            7U,
            8U,
            frame,
            &offset
        )
    );
    TEST_ASSERT_EQUAL_UINT8(1U, offset);
    TEST_ASSERT_EQUAL_HEX8(0x07U, frame[0]);

    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_protocol_decode(
            frame,
            8U,
            &pci
        )
    );
    TEST_ASSERT_EQUAL(ISOTP_PCI_SINGLE_FRAME, pci.type);
    TEST_ASSERT_EQUAL_UINT32(7U, pci.payload_length);

    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_protocol_encode_single_frame(
            62U,
            sizeof(frame),
            frame,
            &offset
        )
    );
    TEST_ASSERT_EQUAL_UINT8(2U, offset);
    TEST_ASSERT_EQUAL_HEX8(0x00U, frame[0]);
    TEST_ASSERT_EQUAL_HEX8(0x3EU, frame[1]);

    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_protocol_decode(
            frame,
            sizeof(frame),
            &pci
        )
    );
    TEST_ASSERT_EQUAL_UINT32(62U, pci.payload_length);
}

TEST_CASE(
    "ISO-TP encodes First Frame length formats",
    "[isotp]"
)
{
    uint8_t frame[64] = {0};
    uint8_t offset = 0U;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_protocol_encode_first_frame(
            4095U,
            sizeof(frame),
            frame,
            &offset
        )
    );
    TEST_ASSERT_EQUAL_UINT8(2U, offset);
    TEST_ASSERT_EQUAL_HEX8(0x1FU, frame[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFFU, frame[1]);

    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_protocol_encode_first_frame(
            65536U,
            sizeof(frame),
            frame,
            &offset
        )
    );
    TEST_ASSERT_EQUAL_UINT8(6U, offset);
    TEST_ASSERT_EQUAL_HEX8(0x10U, frame[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00U, frame[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00U, frame[2]);
    TEST_ASSERT_EQUAL_HEX8(0x01U, frame[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00U, frame[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00U, frame[5]);
}

TEST_CASE(
    "ISO-TP decodes Consecutive and Flow Control frames",
    "[isotp]"
)
{
    const uint8_t consecutive[] = {
        0x2FU,
        0xAAU,
        0x55U,
    };
    const uint8_t flow_control[] = {
        0x30U,
        0x08U,
        0xF3U,
    };
    isotp_pci_t pci;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_protocol_decode(
            consecutive,
            sizeof(consecutive),
            &pci
        )
    );
    TEST_ASSERT_EQUAL(ISOTP_PCI_CONSECUTIVE_FRAME, pci.type);
    TEST_ASSERT_EQUAL_UINT8(15U, pci.sequence_number);
    TEST_ASSERT_EQUAL_UINT32(2U, pci.payload_length);

    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_protocol_decode(
            flow_control,
            sizeof(flow_control),
            &pci
        )
    );
    TEST_ASSERT_EQUAL(ISOTP_PCI_FLOW_CONTROL, pci.type);
    TEST_ASSERT_EQUAL_UINT8(8U, pci.block_size);
    TEST_ASSERT_EQUAL_HEX8(0xF3U, pci.st_min);
}

TEST_CASE(
    "ISO-TP converts STmin and rejects reserved values",
    "[isotp]"
)
{
    uint32_t microseconds = 0U;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_protocol_st_min_to_us(
            10U,
            &microseconds
        )
    );
    TEST_ASSERT_EQUAL_UINT32(10000U, microseconds);

    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_protocol_st_min_to_us(
            0xF9U,
            &microseconds
        )
    );
    TEST_ASSERT_EQUAL_UINT32(900U, microseconds);

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        isotp_protocol_st_min_to_us(
            0x80U,
            &microseconds
        )
    );
}

TEST_CASE(
    "ISO-TP rejects malformed protocol headers",
    "[isotp]"
)
{
    isotp_pci_t pci;
    const uint8_t invalid_single[] = {
        0x00U,
        0x07U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
    };
    const uint8_t invalid_flow_control[] = {
        0x33U,
        0x00U,
        0x00U,
    };
    uint8_t frame[8] = {0};
    uint8_t offset = 0U;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_SIZE,
        isotp_protocol_encode_single_frame(
            0U,
            sizeof(frame),
            frame,
            &offset
        )
    );

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_RESPONSE,
        isotp_protocol_decode(
            invalid_single,
            sizeof(invalid_single),
            &pci
        )
    );

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_RESPONSE,
        isotp_protocol_decode(
            invalid_flow_control,
            sizeof(invalid_flow_control),
            &pci
        )
    );
}
