/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "unity.h"

#include "uds_download.h"

TEST_CASE(
    "UDS download memory reader returns bounded blocks",
    "[uds]"
)
{
    const uint8_t data[] = {
        0x10U,
        0x20U,
        0x30U,
        0x40U,
        0x50U,
    };
    const uds_download_memory_source_t source = {
        .data = data,
        .size = sizeof(data),
    };
    uint8_t buffer[3] = {0};
    size_t read_size = 0U;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_download_memory_read(
            1U,
            buffer,
            sizeof(buffer),
            &read_size,
            (void *)&source
        )
    );
    TEST_ASSERT_EQUAL(3U, read_size);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        &data[1],
        buffer,
        read_size
    );

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_download_memory_read(
            sizeof(data),
            buffer,
            sizeof(buffer),
            &read_size,
            (void *)&source
        )
    );
    TEST_ASSERT_EQUAL(0U, read_size);
}

TEST_CASE(
    "UDS download validates configuration and closed state",
    "[uds]"
)
{
    uds_download_t download = {0};
    uds_download_config_t config = {0};
    uds_download_progress_t progress = {0};

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        uds_download_open(
            &download,
            &config
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        uds_download_start(
            &download,
            0U
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        uds_download_poll(
            &download,
            0U
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_download_get_progress(
            &download,
            &progress
        )
    );
    TEST_ASSERT_EQUAL(UDS_DOWNLOAD_CLOSED, progress.state);
    TEST_ASSERT_NULL(uds_download_client(NULL));
}
