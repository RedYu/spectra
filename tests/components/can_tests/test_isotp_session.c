/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include <string.h>

#include "unity.h"

#include "isotp_session.h"

static isotp_session_config_t isotp_session_test_config(void)
{
    const isotp_session_config_t config = {
        .link_data_length = 8U,
        .receive_block_size = 0U,
        .receive_st_min = 0U,
        .maximum_wait_frames = 3U,
        .flow_control_timeout_us = 1000U,
        .consecutive_frame_timeout_us = 1000U,
    };

    return config;
}

TEST_CASE(
    "ISO-TP session receives a Single Frame",
    "[isotp]"
)
{
    uint8_t receive_buffer[32] = {0};
    isotp_session_t session = {0};
    const isotp_session_config_t config =
        isotp_session_test_config();
    isotp_session_action_t action = {0};
    const uint8_t frame[8] = {
        0x03U, 0x22U, 0xF1U, 0x90U,
    };

    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_init(
            &session,
            &config,
            receive_buffer,
            sizeof(receive_buffer)
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_receive_frame(
            &session,
            frame,
            sizeof(frame),
            100U,
            &action
        )
    );
    TEST_ASSERT_EQUAL(
        ISOTP_SESSION_ACTION_RECEIVE_COMPLETE,
        action.type
    );
    TEST_ASSERT_EQUAL(3U, action.message_length);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        &frame[1],
        receive_buffer,
        3U
    );
}

TEST_CASE(
    "ISO-TP session reassembles a multi-frame message",
    "[isotp]"
)
{
    uint8_t receive_buffer[32] = {0};
    isotp_session_t session = {0};
    isotp_session_config_t config =
        isotp_session_test_config();
    isotp_session_action_t action = {0};
    const uint8_t first_frame[8] = {
        0x10U, 0x0AU, 1U, 2U, 3U, 4U, 5U, 6U,
    };
    const uint8_t consecutive_frame[8] = {
        0x21U, 7U, 8U, 9U, 10U, 0xAAU, 0xAAU, 0xAAU,
    };
    const uint8_t expected[10] = {
        1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U,
    };

    config.receive_block_size = 4U;
    config.receive_st_min = 5U;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_init(
            &session,
            &config,
            receive_buffer,
            sizeof(receive_buffer)
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_receive_frame(
            &session,
            first_frame,
            sizeof(first_frame),
            100U,
            &action
        )
    );
    TEST_ASSERT_EQUAL(
        ISOTP_SESSION_ACTION_SEND_FRAME,
        action.type
    );
    TEST_ASSERT_EQUAL_HEX8(0x30U, action.frame_data[0]);
    TEST_ASSERT_EQUAL_HEX8(4U, action.frame_data[1]);
    TEST_ASSERT_EQUAL_HEX8(5U, action.frame_data[2]);

    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_frame_transmitted(
            &session,
            110U,
            &action
        )
    );
    TEST_ASSERT_EQUAL(
        ISOTP_SESSION_RX_WAIT_CONSECUTIVE_FRAME,
        session.state
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_receive_frame(
            &session,
            consecutive_frame,
            sizeof(consecutive_frame),
            200U,
            &action
        )
    );
    TEST_ASSERT_EQUAL(
        ISOTP_SESSION_ACTION_RECEIVE_COMPLETE,
        action.type
    );
    TEST_ASSERT_EQUAL(10U, action.message_length);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        expected,
        receive_buffer,
        sizeof(expected)
    );
}

TEST_CASE(
    "ISO-TP session rejects an incorrect sequence number",
    "[isotp]"
)
{
    uint8_t receive_buffer[32] = {0};
    isotp_session_t session = {0};
    const isotp_session_config_t config =
        isotp_session_test_config();
    isotp_session_action_t action = {0};
    const uint8_t first_frame[8] = {
        0x10U, 0x0AU, 1U, 2U, 3U, 4U, 5U, 6U,
    };
    const uint8_t incorrect_frame[8] = {
        0x22U, 7U, 8U, 9U, 10U,
    };

    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_init(
            &session,
            &config,
            receive_buffer,
            sizeof(receive_buffer)
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_receive_frame(
            &session,
            first_frame,
            sizeof(first_frame),
            0U,
            &action
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_frame_transmitted(
            &session,
            10U,
            &action
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_RESPONSE,
        isotp_session_receive_frame(
            &session,
            incorrect_frame,
            sizeof(incorrect_frame),
            20U,
            &action
        )
    );
    TEST_ASSERT_EQUAL(
        ISOTP_SESSION_ACTION_ERROR,
        action.type
    );
    TEST_ASSERT_EQUAL(
        ISOTP_SESSION_ERROR_SEQUENCE,
        action.error
    );
}

TEST_CASE(
    "ISO-TP session segments a multi-frame transmission",
    "[isotp]"
)
{
    uint8_t receive_buffer[32] = {0};
    isotp_session_t session = {0};
    const isotp_session_config_t config =
        isotp_session_test_config();
    isotp_session_action_t action = {0};
    const uint8_t payload[10] = {
        1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U,
    };
    const uint8_t flow_control[8] = {
        0x30U, 0U, 0U,
    };

    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_init(
            &session,
            &config,
            receive_buffer,
            sizeof(receive_buffer)
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_start_transmit(
            &session,
            payload,
            sizeof(payload),
            100U,
            &action
        )
    );
    TEST_ASSERT_EQUAL_HEX8(0x10U, action.frame_data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x0AU, action.frame_data[1]);
    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_frame_transmitted(
            &session,
            110U,
            &action
        )
    );
    TEST_ASSERT_EQUAL(
        ISOTP_SESSION_TX_WAIT_FLOW_CONTROL,
        session.state
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_receive_frame(
            &session,
            flow_control,
            sizeof(flow_control),
            120U,
            &action
        )
    );
    TEST_ASSERT_EQUAL(
        ISOTP_SESSION_ACTION_SEND_FRAME,
        action.type
    );
    TEST_ASSERT_EQUAL_HEX8(0x21U, action.frame_data[0]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        &payload[6],
        &action.frame_data[1],
        4U
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_frame_transmitted(
            &session,
            130U,
            &action
        )
    );
    TEST_ASSERT_EQUAL(
        ISOTP_SESSION_ACTION_TRANSMIT_COMPLETE,
        action.type
    );
    TEST_ASSERT_EQUAL(sizeof(payload), action.message_length);
}

TEST_CASE(
    "ISO-TP session enforces STmin between Consecutive Frames",
    "[isotp]"
)
{
    uint8_t receive_buffer[64] = {0};
    isotp_session_t session = {0};
    const isotp_session_config_t config =
        isotp_session_test_config();
    isotp_session_action_t action = {0};
    uint8_t payload[21] = {0};
    const uint8_t flow_control[8] = {
        0x30U, 0U, 5U,
    };

    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_init(
            &session,
            &config,
            receive_buffer,
            sizeof(receive_buffer)
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_start_transmit(
            &session,
            payload,
            sizeof(payload),
            0U,
            &action
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_frame_transmitted(
            &session,
            10U,
            &action
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_receive_frame(
            &session,
            flow_control,
            sizeof(flow_control),
            20U,
            &action
        )
    );
    TEST_ASSERT_EQUAL(
        ISOTP_SESSION_ACTION_SEND_FRAME,
        action.type
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_frame_transmitted(
            &session,
            30U,
            &action
        )
    );
    TEST_ASSERT_EQUAL(
        ISOTP_SESSION_TX_WAIT_ST_MIN,
        session.state
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_poll(
            &session,
            5029U,
            &action
        )
    );
    TEST_ASSERT_EQUAL(
        ISOTP_SESSION_ACTION_NONE,
        action.type
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_poll(
            &session,
            5030U,
            &action
        )
    );
    TEST_ASSERT_EQUAL(
        ISOTP_SESSION_ACTION_SEND_FRAME,
        action.type
    );
    TEST_ASSERT_EQUAL_HEX8(0x22U, action.frame_data[0]);
}

TEST_CASE(
    "ISO-TP session supports extended addressing and padding",
    "[isotp]"
)
{
    uint8_t receive_buffer[32] = {0};
    isotp_session_t session = {0};
    isotp_session_config_t config =
        isotp_session_test_config();
    isotp_session_action_t action = {0};
    const uint8_t payload[3] = {
        0x22U,
        0xF1U,
        0x90U,
    };

    config.addressing_mode = ISOTP_ADDRESSING_EXTENDED;
    config.transmit_address = 0xDAU;
    config.receive_address = 0xF1U;
    config.padding_byte = 0xAAU;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_init(
            &session,
            &config,
            receive_buffer,
            sizeof(receive_buffer)
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_start_transmit(
            &session,
            payload,
            sizeof(payload),
            0U,
            &action
        )
    );
    TEST_ASSERT_EQUAL_HEX8(0xDAU, action.frame_data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x03U, action.frame_data[1]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        payload,
        &action.frame_data[2],
        sizeof(payload)
    );
    TEST_ASSERT_EQUAL_HEX8(0xAAU, action.frame_data[7]);

    isotp_session_reset(&session);

    const uint8_t received_frame[8] = {
        0xF1U,
        0x03U,
        0x62U,
        0xF1U,
        0x90U,
        0xAAU,
        0xAAU,
        0xAAU,
    };

    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_receive_frame(
            &session,
            received_frame,
            sizeof(received_frame),
            0U,
            &action
        )
    );
    TEST_ASSERT_EQUAL(
        ISOTP_SESSION_ACTION_RECEIVE_COMPLETE,
        action.type
    );
    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        &received_frame[2],
        receive_buffer,
        3U
    );
}

TEST_CASE(
    "ISO-TP receiver sends overflow Flow Control",
    "[isotp]"
)
{
    uint8_t receive_buffer[8] = {0};
    isotp_session_t session = {0};
    const isotp_session_config_t config =
        isotp_session_test_config();
    isotp_session_action_t action = {0};
    const uint8_t first_frame[8] = {
        0x10U,
        0x10U,
        1U,
        2U,
        3U,
        4U,
        5U,
        6U,
    };

    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_init(
            &session,
            &config,
            receive_buffer,
            sizeof(receive_buffer)
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_receive_frame(
            &session,
            first_frame,
            sizeof(first_frame),
            0U,
            &action
        )
    );
    TEST_ASSERT_EQUAL(
        ISOTP_SESSION_ACTION_SEND_FRAME,
        action.type
    );
    TEST_ASSERT_EQUAL_HEX8(0x32U, action.frame_data[0]);
    TEST_ASSERT_EQUAL(
        ISOTP_SESSION_RX_SENDING_OVERFLOW,
        session.state
    );
    TEST_ASSERT_EQUAL(
        ESP_ERR_NO_MEM,
        isotp_session_frame_transmitted(
            &session,
            10U,
            &action
        )
    );
    TEST_ASSERT_EQUAL(ISOTP_SESSION_ACTION_ERROR, action.type);
    TEST_ASSERT_EQUAL(
        ISOTP_SESSION_ERROR_BUFFER_OVERFLOW,
        action.error
    );
}

TEST_CASE(
    "ISO-TP functional transmission is limited to a Single Frame",
    "[isotp]"
)
{
    uint8_t receive_buffer[32] = {0};
    isotp_session_t session = {0};
    isotp_session_config_t config =
        isotp_session_test_config();
    isotp_session_action_t action = {0};
    const uint8_t payload[8] = {0};

    config.functional_transmit = true;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_init(
            &session,
            &config,
            receive_buffer,
            sizeof(receive_buffer)
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_SIZE,
        isotp_session_start_transmit(
            &session,
            payload,
            sizeof(payload),
            0U,
            &action
        )
    );
    TEST_ASSERT_EQUAL(ISOTP_SESSION_IDLE, session.state);
}

TEST_CASE(
    "ISO-TP session reports a Flow Control timeout",
    "[isotp]"
)
{
    uint8_t receive_buffer[32] = {0};
    isotp_session_t session = {0};
    const isotp_session_config_t config =
        isotp_session_test_config();
    isotp_session_action_t action = {0};
    uint8_t payload[10] = {0};

    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_init(
            &session,
            &config,
            receive_buffer,
            sizeof(receive_buffer)
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_start_transmit(
            &session,
            payload,
            sizeof(payload),
            0U,
            &action
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        isotp_session_frame_transmitted(
            &session,
            10U,
            &action
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_ERR_TIMEOUT,
        isotp_session_poll(
            &session,
            1010U,
            &action
        )
    );
    TEST_ASSERT_EQUAL(
        ISOTP_SESSION_ACTION_ERROR,
        action.type
    );
    TEST_ASSERT_EQUAL(
        ISOTP_SESSION_ERROR_TIMEOUT,
        action.error
    );
}
