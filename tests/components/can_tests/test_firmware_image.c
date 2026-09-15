/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "unity.h"

#include <string.h>

#include "firmware_image.h"

typedef struct
{
    const uint8_t *data;
    size_t size;

} firmware_image_test_source_t;

static esp_err_t firmware_image_test_read(
    uint64_t offset,
    uint8_t *buffer,
    size_t capacity,
    size_t *read_size,
    void *context
)
{
    firmware_image_test_source_t *source = context;

    if ((buffer == NULL) ||
        (read_size == NULL) ||
        (source == NULL) ||
        (offset > source->size)) {

        return ESP_ERR_INVALID_ARG;
    }

    const size_t available = source->size - (size_t)offset;
    const size_t size = (available < capacity)
        ? available
        : capacity;

    memcpy(buffer, source->data + offset, size);
    *read_size = size;
    return ESP_OK;
}

static firmware_image_config_t firmware_image_test_config(
    firmware_image_format_t format,
    const void *data,
    size_t size,
    firmware_image_test_source_t *source
)
{
    source->data = data;
    source->size = size;

    const firmware_image_config_t config = {
        .format = format,
        .read = firmware_image_test_read,
        .read_context = source,
        .file_size = size,
        .binary_address = 0x8000U,
    };

    return config;
}

TEST_CASE(
    "Firmware image streams a raw binary at its configured address",
    "[firmware_image]"
)
{
    const uint8_t image[] = {1U, 2U, 3U, 4U};
    firmware_image_test_source_t source;
    const firmware_image_config_t config =
        firmware_image_test_config(
            FIRMWARE_IMAGE_FORMAT_BIN,
            image,
            sizeof(image),
            &source
        );
    firmware_image_reader_t reader;
    firmware_image_block_t block;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        firmware_image_open(&reader, &config)
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        firmware_image_next(&reader, &block)
    );
    TEST_ASSERT_EQUAL_HEX64(0x8000U, block.address);
    TEST_ASSERT_EQUAL_UINT32(sizeof(image), block.size);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(image, block.data, sizeof(image));
    TEST_ASSERT_EQUAL(
        ESP_ERR_NOT_FOUND,
        firmware_image_next(&reader, &block)
    );
}

TEST_CASE(
    "Firmware image decodes Intel HEX addresses and checksum",
    "[firmware_image]"
)
{
    static const char image[] =
        ":020000040001F9\n"
        ":0400100001020304E2\n"
        ":00000001FF\n";
    firmware_image_test_source_t source;
    const firmware_image_config_t config =
        firmware_image_test_config(
            FIRMWARE_IMAGE_FORMAT_INTEL_HEX,
            image,
            sizeof(image) - 1U,
            &source
        );
    firmware_image_reader_t reader;
    firmware_image_info_t info;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        firmware_image_open(&reader, &config)
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        firmware_image_inspect(&reader, &info)
    );
    TEST_ASSERT_EQUAL_UINT64(4U, info.data_size);
    TEST_ASSERT_EQUAL_HEX64(0x10010U, info.lowest_address);
    TEST_ASSERT_EQUAL_HEX64(0x10013U, info.highest_address);
    TEST_ASSERT_EQUAL_UINT32(1U, info.segment_count);
}

TEST_CASE(
    "Firmware image decodes Motorola S-record metadata",
    "[firmware_image]"
)
{
    static const char image[] =
        "S107100001020304DE\r\n"
        "S5030001FB\r\n"
        "S9031000EC\r\n";
    firmware_image_test_source_t source;
    const firmware_image_config_t config =
        firmware_image_test_config(
            FIRMWARE_IMAGE_FORMAT_S_RECORD,
            image,
            sizeof(image) - 1U,
            &source
        );
    firmware_image_reader_t reader;
    firmware_image_info_t info;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        firmware_image_open(&reader, &config)
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        firmware_image_inspect(&reader, &info)
    );
    TEST_ASSERT_EQUAL_UINT64(4U, info.data_size);
    TEST_ASSERT_EQUAL_HEX64(0x1000U, info.lowest_address);
    TEST_ASSERT_EQUAL_HEX64(0x1003U, info.highest_address);
    TEST_ASSERT_TRUE(info.entry_address_valid);
    TEST_ASSERT_EQUAL_HEX64(0x1000U, info.entry_address);
}

TEST_CASE(
    "Firmware image rejects invalid checksum and trailing records",
    "[firmware_image]"
)
{
    static const char invalid_checksum[] =
        ":0400100001020304E3\n"
        ":00000001FF\n";
    static const char trailing_record[] =
        ":00000001FF\n"
        ":0400100001020304E2\n";
    firmware_image_test_source_t source;
    firmware_image_reader_t reader;
    firmware_image_info_t info;
    firmware_image_config_t config =
        firmware_image_test_config(
            FIRMWARE_IMAGE_FORMAT_INTEL_HEX,
            invalid_checksum,
            sizeof(invalid_checksum) - 1U,
            &source
        );

    TEST_ASSERT_EQUAL(
        ESP_OK,
        firmware_image_open(&reader, &config)
    );
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_CRC,
        firmware_image_inspect(&reader, &info)
    );

    config = firmware_image_test_config(
        FIRMWARE_IMAGE_FORMAT_INTEL_HEX,
        trailing_record,
        sizeof(trailing_record) - 1U,
        &source
    );

    TEST_ASSERT_EQUAL(
        ESP_OK,
        firmware_image_open(&reader, &config)
    );
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_RESPONSE,
        firmware_image_inspect(&reader, &info)
    );
}

TEST_CASE(
    "Firmware image decodes multiple BHX sections",
    "[firmware_image]"
)
{
    static const uint8_t image[] = {
        'G', 'H', 'D', 'R',
        0x00U, 0x00U, 0x00U, 0x01U,
        0x00U, 0x00U, 0x00U, 0x07U,
        'S', 'H', 'D', 'R',
        0x00U, 0x00U, 0x00U, 0x01U,
        0x00U, 0x01U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x04U,
        0xC0U, 0xDEU, 0xCAU, 0xFEU,
        0x11U, 0x22U, 0x33U, 0x44U,
        'S', 'H', 'D', 'R',
        0x00U, 0x00U, 0x00U, 0x01U,
        0x00U, 0x2FU, 0xFFU, 0x00U,
        0x00U, 0x00U, 0x00U, 0x03U,
        0xC0U, 0xDEU, 0xCAU, 0xFEU,
        0xAAU, 0xBBU, 0xCCU,
    };
    firmware_image_test_source_t source;
    const firmware_image_config_t config =
        firmware_image_test_config(
            FIRMWARE_IMAGE_FORMAT_BHX,
            image,
            sizeof(image),
            &source
        );
    firmware_image_reader_t reader;
    firmware_image_block_t block;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        firmware_image_open(&reader, &config)
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        firmware_image_next(&reader, &block)
    );
    TEST_ASSERT_EQUAL_HEX64(0x00010000U, block.address);
    TEST_ASSERT_EQUAL_UINT32(4U, block.size);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&image[32], block.data, 4U);
    TEST_ASSERT_EQUAL(
        ESP_OK,
        firmware_image_next(&reader, &block)
    );
    TEST_ASSERT_EQUAL_HEX64(0x002FFF00U, block.address);
    TEST_ASSERT_EQUAL_UINT32(3U, block.size);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&image[56], block.data, 3U);
    TEST_ASSERT_EQUAL(
        ESP_ERR_NOT_FOUND,
        firmware_image_next(&reader, &block)
    );
}

TEST_CASE(
    "Firmware image rejects an inconsistent BHX total size",
    "[firmware_image]"
)
{
    static const uint8_t image[] = {
        'G', 'H', 'D', 'R',
        0x00U, 0x00U, 0x00U, 0x01U,
        0x00U, 0x00U, 0x00U, 0x02U,
        'S', 'H', 'D', 'R',
        0x00U, 0x00U, 0x00U, 0x01U,
        0x00U, 0x00U, 0x10U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x01U,
        0xC0U, 0xDEU, 0xCAU, 0xFEU,
        0xAAU,
    };
    firmware_image_test_source_t source;
    const firmware_image_config_t config =
        firmware_image_test_config(
            FIRMWARE_IMAGE_FORMAT_BHX,
            image,
            sizeof(image),
            &source
        );
    firmware_image_reader_t reader;
    firmware_image_info_t info;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        firmware_image_open(&reader, &config)
    );
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_SIZE,
        firmware_image_inspect(&reader, &info)
    );
}

TEST_CASE(
    "Firmware image recognizes supported extensions",
    "[firmware_image]"
)
{
    firmware_image_format_t format;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        firmware_image_format_from_name("image.BIN", &format)
    );
    TEST_ASSERT_EQUAL(FIRMWARE_IMAGE_FORMAT_BIN, format);
    TEST_ASSERT_EQUAL(
        ESP_OK,
        firmware_image_format_from_name("image.hex", &format)
    );
    TEST_ASSERT_EQUAL(FIRMWARE_IMAGE_FORMAT_INTEL_HEX, format);
    TEST_ASSERT_EQUAL(
        ESP_OK,
        firmware_image_format_from_name("image.s19", &format)
    );
    TEST_ASSERT_EQUAL(FIRMWARE_IMAGE_FORMAT_S_RECORD, format);
    TEST_ASSERT_EQUAL(
        ESP_ERR_NOT_SUPPORTED,
        firmware_image_format_from_name("image.bhex", &format)
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        firmware_image_format_from_name("image.BHX", &format)
    );
    TEST_ASSERT_EQUAL(FIRMWARE_IMAGE_FORMAT_BHX, format);
}
