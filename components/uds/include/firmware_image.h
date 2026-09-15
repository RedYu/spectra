/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FIRMWARE_IMAGE_RECORD_DATA_MAX_SIZE  (255U)
#define FIRMWARE_IMAGE_LINE_MAX_SIZE         (524U)
#define FIRMWARE_IMAGE_READ_BUFFER_SIZE      (256U)

typedef enum
{
    FIRMWARE_IMAGE_FORMAT_BIN = 0,
    FIRMWARE_IMAGE_FORMAT_INTEL_HEX,
    FIRMWARE_IMAGE_FORMAT_S_RECORD,
    FIRMWARE_IMAGE_FORMAT_BHX,

} firmware_image_format_t;

typedef esp_err_t (*firmware_image_read_cb_t)(
    uint64_t offset,
    uint8_t *buffer,
    size_t capacity,
    size_t *read_size,
    void *context
);

typedef struct
{
    firmware_image_format_t format;
    firmware_image_read_cb_t read;
    void *read_context;
    uint64_t file_size;
    uint64_t binary_address;

} firmware_image_config_t;

typedef struct
{
    uint64_t address;
    const uint8_t *data;
    size_t size;

} firmware_image_block_t;

typedef struct
{
    uint64_t data_size;
    uint64_t lowest_address;
    uint64_t highest_address;
    uint64_t entry_address;
    uint32_t block_count;
    uint32_t segment_count;
    bool entry_address_valid;

} firmware_image_info_t;

typedef struct
{
    firmware_image_config_t config;
    uint64_t file_offset;
    uint64_t address_base;
    uint64_t previous_end;
    uint64_t entry_address;
    uint32_t data_record_count;
    uint32_t declared_record_count;
    uint32_t bhx_total_data_size;
    uint32_t bhx_consumed_data_size;
    uint32_t bhx_section_remaining;
    uint64_t bhx_section_address;
    uint8_t s_record_data_type;
    bool previous_block_valid;
    bool entry_address_valid;
    bool terminated;
    bool count_record_seen;
    bool bhx_global_header_read;
    size_t read_buffer_offset;
    size_t read_buffer_size;
    uint8_t read_buffer[FIRMWARE_IMAGE_READ_BUFFER_SIZE];
    char line[FIRMWARE_IMAGE_LINE_MAX_SIZE];
    uint8_t data[FIRMWARE_IMAGE_RECORD_DATA_MAX_SIZE];

} firmware_image_reader_t;

/**
 * @brief Initialize or reset a streaming firmware-image reader.
 */
esp_err_t firmware_image_open(
    firmware_image_reader_t *reader,
    const firmware_image_config_t *config
);

/**
 * @brief Read and validate the next data block.
 *
 * @return ESP_OK when a block is available, ESP_ERR_NOT_FOUND after the
 * validated end of the image, or an error for malformed input.
 */
esp_err_t firmware_image_next(
    firmware_image_reader_t *reader,
    firmware_image_block_t *block
);

/**
 * @brief Validate the complete image and calculate its address layout.
 *
 * This consumes the reader. Call firmware_image_open() again before reading
 * blocks for transfer.
 */
esp_err_t firmware_image_inspect(
    firmware_image_reader_t *reader,
    firmware_image_info_t *info
);

/**
 * @brief Select an image format from a file name extension.
 */
esp_err_t firmware_image_format_from_name(
    const char *file_name,
    firmware_image_format_t *format
);

#ifdef __cplusplus
}
#endif
