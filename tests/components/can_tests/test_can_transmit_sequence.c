/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "unity.h"

#include "can_transmit_service.h"

static can_transmit_job_config_t can_transmit_test_config(
    void
)
{
    const can_transmit_job_config_t config = {
        .frame = {
            .bus = CAN_BUS_PRIMARY,
            .identifier = 0x123U,
            .dlc = 8U,
            .data_length = 8U,
        },
        .interval_ms = 100U,
        .count = 3U,
    };

    return config;
}

TEST_CASE(
    "CAN TX validates bus, flags, count and timing",
    "[can_tx]"
)
{
    can_transmit_job_config_t job =
        can_transmit_test_config();

    TEST_ASSERT_EQUAL(
        ESP_OK,
        can_transmit_job_validate(&job)
    );

    job.interval_ms = 0U;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        can_transmit_job_validate(&job)
    );

    job =
        can_transmit_test_config();

    job.frame.flags = CAN_FRAME_FLAG_FD;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        can_transmit_job_validate(&job)
    );

    job.frame.bus = CAN_BUS_SECONDARY;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        can_transmit_job_validate(&job)
    );

    job.frame.flags |= CAN_FRAME_FLAG_REMOTE;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        can_transmit_job_validate(&job)
    );

    job =
        can_transmit_test_config();

    job.count = 1000001U;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        can_transmit_job_validate(&job)
    );

    job.count = 0U;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        can_transmit_job_validate(&job)
    );
}

TEST_CASE(
    "CAN TX ID increment wraps to its initial value",
    "[can_tx]"
)
{
    can_transmit_job_config_t job =
        can_transmit_test_config();

    job.id_step = 2U;
    job.id_end = 0x126U;

    can_frame_t frame = job.frame;

    can_transmit_job_advance(
        &job,
        &frame
    );

    TEST_ASSERT_EQUAL_HEX32(
        0x125U,
        frame.identifier
    );

    can_transmit_job_advance(
        &job,
        &frame
    );

    TEST_ASSERT_EQUAL_HEX32(
        0x123U,
        frame.identifier
    );

    job.id_end = 0x800U;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        can_transmit_job_validate(&job)
    );
}

TEST_CASE(
    "CAN TX little and big endian counters wrap without touching "
    "neighbors",
    "[can_tx]"
)
{
    can_transmit_job_config_t job =
        can_transmit_test_config();

    job.data_offset = 2U;
    job.data_width = 2U;
    job.data_step = 1U;

    can_frame_t frame = job.frame;

    frame.data[1] = 0x55U;
    frame.data[2] = 0xFFU;
    frame.data[3] = 0x01U;

    can_transmit_job_advance(
        &job,
        &frame
    );

    TEST_ASSERT_EQUAL_HEX8(
        0U,
        frame.data[2]
    );

    TEST_ASSERT_EQUAL_HEX8(
        2U,
        frame.data[3]
    );

    TEST_ASSERT_EQUAL_HEX8(
        0x55U,
        frame.data[1]
    );

    job.data_big_endian = true;
    frame.data[2] = 0x01U;
    frame.data[3] = 0xFFU;

    can_transmit_job_advance(
        &job,
        &frame
    );

    TEST_ASSERT_EQUAL_HEX8(
        2U,
        frame.data[2]
    );

    TEST_ASSERT_EQUAL_HEX8(
        0U,
        frame.data[3]
    );

    frame.data[2] = 0xFFU;
    frame.data[3] = 0xFFU;

    can_transmit_job_advance(
        &job,
        &frame
    );

    TEST_ASSERT_EQUAL_HEX8(
        0U,
        frame.data[2]
    );

    TEST_ASSERT_EQUAL_HEX8(
        0U,
        frame.data[3]
    );
}

TEST_CASE(
    "CAN TX FD DLC grows and zero pads then wraps",
    "[can_tx]"
)
{
    can_transmit_job_config_t job =
        can_transmit_test_config();

    job.frame.bus = CAN_BUS_SECONDARY;
    job.frame.flags = CAN_FRAME_FLAG_FD;
    job.increment_dlc = true;
    job.dlc_end = 10U;

    can_frame_t frame = job.frame;

    frame.data[8] = 0xABU;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        can_transmit_job_validate(&job)
    );

    can_transmit_job_advance(
        &job,
        &frame
    );

    TEST_ASSERT_EQUAL(
        9U,
        frame.dlc
    );

    TEST_ASSERT_EQUAL(
        12U,
        frame.data_length
    );

    TEST_ASSERT_EQUAL(
        0U,
        frame.data[8]
    );

    can_transmit_job_advance(
        &job,
        &frame
    );

    TEST_ASSERT_EQUAL(
        16U,
        frame.data_length
    );

    can_transmit_job_advance(
        &job,
        &frame
    );

    TEST_ASSERT_EQUAL(
        8U,
        frame.dlc
    );

    TEST_ASSERT_EQUAL(
        8U,
        frame.data_length
    );
}

TEST_CASE(
    "CAN TX rejects a counter outside the shortest payload",
    "[can_tx]"
)
{
    can_transmit_job_config_t job =
        can_transmit_test_config();

    job.data_offset = 7U;
    job.data_width = 2U;
    job.data_step = 1U;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        can_transmit_job_validate(&job)
    );

    job.data_offset = 0U;
    job.data_width = 8U;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        can_transmit_job_validate(&job)
    );
}

TEST_CASE(
    "CAN TX increments masked payload bits independently",
    "[can_tx]"
)
{
    can_transmit_job_config_t job =
        can_transmit_test_config();

    job.increment_data_bytes = true;
    job.data_byte_masks[0] = 0xF0U;
    job.data_byte_masks[1] = 0x0FU;
    job.data_byte_masks[2] = 0xFFU;
    job.data_step = 1U;

    can_frame_t frame = job.frame;

    frame.data[0] = 0xA5U;
    frame.data[1] = 0xA5U;
    frame.data[2] = 0xFFU;
    frame.data[3] = 0x55U;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        can_transmit_job_validate(&job)
    );

    can_transmit_job_advance(
        &job,
        &frame
    );

    TEST_ASSERT_EQUAL_HEX8(0xB5U, frame.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xA6U, frame.data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00U, frame.data[2]);
    TEST_ASSERT_EQUAL_HEX8(0x55U, frame.data[3]);
}

TEST_CASE(
    "CAN TX validates masked byte increment settings",
    "[can_tx]"
)
{
    can_transmit_job_config_t job =
        can_transmit_test_config();

    job.increment_data_bytes = true;
    job.data_step = 1U;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        can_transmit_job_validate(&job)
    );

    job.data_byte_masks[8] = 0xFFU;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        can_transmit_job_validate(&job)
    );

    job.data_byte_masks[8] = 0U;
    job.data_byte_masks[0] = 0xFFU;
    job.data_width = 1U;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        can_transmit_job_validate(&job)
    );

    job.data_width = 0U;
    job.data_step = 256U;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        can_transmit_job_validate(&job)
    );
}
