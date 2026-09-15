/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "unity.h"

#include "uds_download.h"

typedef struct
{
    uint64_t addresses[2];
    uint64_t sizes[2];

} uds_download_test_segments_t;

static esp_err_t uds_download_test_segment(
    uint32_t index,
    uint64_t *address,
    uint64_t *size,
    void *context
)
{
    const uds_download_test_segments_t *segments = context;

    if ((segments == NULL) ||
        (address == NULL) ||
        (size == NULL) ||
        (index >= 2U)) {

        return ESP_ERR_INVALID_ARG;
    }

    *address = segments->addresses[index];
    *size = segments->sizes[index];
    return ESP_OK;
}

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

TEST_CASE(
    "UDS download validates segmented image layout",
    "[uds]"
)
{
    static const uint8_t source_data[] = {1U, 2U, 3U, 4U, 5U};
    uds_download_memory_source_t source = {
        .data = source_data,
        .size = sizeof(source_data),
    };
    uds_download_test_segments_t segments = {
        .addresses = {0x1000U, 0x2000U},
        .sizes = {2U, 3U},
    };
    uint8_t transfer_buffer[8];
    uds_download_config_t config = {
        .read = uds_download_memory_read,
        .read_context = &source,
        .transfer_buffer = transfer_buffer,
        .transfer_capacity = sizeof(transfer_buffer),
        .memory_size = sizeof(source_data) - 1U,
        .segment_count = 2U,
        .segment = uds_download_test_segment,
        .segment_context = &segments,
    };
    uds_download_t download;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_SIZE,
        uds_download_open(&download, &config)
    );

    config.memory_size = sizeof(source_data);
    segments.addresses[1] = 0x1001U;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        uds_download_open(&download, &config)
    );

    segments.addresses[1] = 0x2000U;
    config.segment = NULL;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        uds_download_open(&download, &config)
    );
}
