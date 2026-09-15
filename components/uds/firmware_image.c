/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "firmware_image.h"

#include <ctype.h>
#include <string.h>

static bool firmware_image_hex_digit(
    char character,
    uint8_t *value
)
{
    if ((character >= '0') && (character <= '9')) {
        *value = (uint8_t)(character - '0');
        return true;
    }

    if ((character >= 'A') && (character <= 'F')) {
        *value = (uint8_t)(character - 'A' + 10);
        return true;
    }

    if ((character >= 'a') && (character <= 'f')) {
        *value = (uint8_t)(character - 'a' + 10);
        return true;
    }

    return false;
}

static bool firmware_image_hex_byte(
    const char *text,
    uint8_t *value
)
{
    uint8_t high = 0U;
    uint8_t low = 0U;

    if (!firmware_image_hex_digit(text[0], &high) ||
        !firmware_image_hex_digit(text[1], &low)) {

        return false;
    }

    *value = (uint8_t)((high << 4U) | low);
    return true;
}

static esp_err_t firmware_image_read_byte(
    firmware_image_reader_t *reader,
    uint8_t *value
)
{
    if (reader->file_offset >= reader->config.file_size) {
        return ESP_ERR_NOT_FOUND;
    }

    if (reader->read_buffer_offset >=
        reader->read_buffer_size) {

        const uint64_t remaining =
            reader->config.file_size - reader->file_offset;
        const size_t capacity =
            (remaining < sizeof(reader->read_buffer))
                ? (size_t)remaining
                : sizeof(reader->read_buffer);
        size_t read_size = 0U;
        const esp_err_t result = reader->config.read(
            reader->file_offset,
            reader->read_buffer,
            capacity,
            &read_size,
            reader->config.read_context
        );

        if (result != ESP_OK) {
            return result;
        }

        if ((read_size == 0U) ||
            (read_size > capacity)) {

            return ESP_ERR_INVALID_SIZE;
        }

        reader->read_buffer_offset = 0U;
        reader->read_buffer_size = read_size;
    }

    *value = reader->read_buffer[
        reader->read_buffer_offset++
    ];
    reader->file_offset++;
    return ESP_OK;
}

static esp_err_t firmware_image_read_line(
    firmware_image_reader_t *reader,
    size_t *line_length
)
{
    size_t length = 0U;

    while (reader->file_offset < reader->config.file_size) {
        uint8_t character = 0U;
        const esp_err_t result = firmware_image_read_byte(
            reader,
            &character
        );

        if (result != ESP_OK) {
            return result;
        }

        if (character == '\n') {
            break;
        }

        if (character == '\r') {
            continue;
        }

        if ((length + 1U) >= sizeof(reader->line)) {
            return ESP_ERR_INVALID_SIZE;
        }

        reader->line[length++] = (char)character;
    }

    reader->line[length] = '\0';
    *line_length = length;
    return (length > 0U)
        ? ESP_OK
        : ESP_ERR_NOT_FOUND;
}

static esp_err_t firmware_image_validate_trailing_data(
    firmware_image_reader_t *reader
)
{
    while (reader->file_offset < reader->config.file_size) {
        uint8_t character = 0U;
        const esp_err_t result = firmware_image_read_byte(
            reader,
            &character
        );

        if (result != ESP_OK) {
            return result;
        }

        if (!isspace(character)) {
            return ESP_ERR_INVALID_RESPONSE;
        }
    }

    return ESP_OK;
}

static esp_err_t firmware_image_publish_block(
    firmware_image_reader_t *reader,
    uint64_t address,
    size_t size,
    firmware_image_block_t *block
)
{
    if ((size == 0U) ||
        (address > (UINT64_MAX - size)) ||
        (reader->previous_block_valid &&
         (address < reader->previous_end))) {

        return ESP_ERR_INVALID_ARG;
    }

    block->address = address;
    block->data = reader->data;
    block->size = size;
    reader->previous_end = address + size;
    reader->previous_block_valid = true;
    reader->data_record_count++;
    return ESP_OK;
}

static esp_err_t firmware_image_next_bin(
    firmware_image_reader_t *reader,
    firmware_image_block_t *block
)
{
    if (reader->file_offset >= reader->config.file_size) {
        reader->terminated = true;
        return ESP_ERR_NOT_FOUND;
    }

    const uint64_t remaining =
        reader->config.file_size - reader->file_offset;
    const size_t capacity = (remaining < sizeof(reader->data))
        ? (size_t)remaining
        : sizeof(reader->data);
    size_t read_size = 0U;
    const uint64_t offset = reader->file_offset;
    const esp_err_t result = reader->config.read(
        offset,
        reader->data,
        capacity,
        &read_size,
        reader->config.read_context
    );

    if (result != ESP_OK) {
        return result;
    }

    if ((read_size == 0U) || (read_size > capacity)) {
        return ESP_ERR_INVALID_SIZE;
    }

    reader->file_offset += read_size;
    return firmware_image_publish_block(
        reader,
        reader->config.binary_address + offset,
        read_size,
        block
    );
}

static esp_err_t firmware_image_decode_hex_record(
    const char *line,
    size_t length,
    uint8_t *record,
    size_t *record_size
)
{
    if ((length < 3U) || (line[0] != ':') ||
        (((length - 1U) & 1U) != 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    const size_t size = (length - 1U) / 2U;

    if ((size < 5U) ||
        (size > (FIRMWARE_IMAGE_RECORD_DATA_MAX_SIZE + 5U))) {

        return ESP_ERR_INVALID_SIZE;
    }

    for (size_t index = 0U; index < size; ++index) {
        if (!firmware_image_hex_byte(
                &line[1U + (index * 2U)],
                &record[index]
            )) {

            return ESP_ERR_INVALID_ARG;
        }
    }

    const size_t data_size = record[0];

    if (size != (data_size + 5U)) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t checksum = 0U;

    for (size_t index = 0U; index < size; ++index) {
        checksum = (uint8_t)(checksum + record[index]);
    }

    if (checksum != 0U) {
        return ESP_ERR_INVALID_CRC;
    }

    *record_size = size;
    return ESP_OK;
}

static esp_err_t firmware_image_next_intel_hex(
    firmware_image_reader_t *reader,
    firmware_image_block_t *block
)
{
    uint8_t record[FIRMWARE_IMAGE_RECORD_DATA_MAX_SIZE + 5U];

    while (!reader->terminated) {
        size_t line_length = 0U;
        esp_err_t result = firmware_image_read_line(
            reader,
            &line_length
        );

        if (result == ESP_ERR_NOT_FOUND) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        if (result != ESP_OK) {
            return result;
        }

        size_t record_size = 0U;
        result = firmware_image_decode_hex_record(
            reader->line,
            line_length,
            record,
            &record_size
        );

        if (result != ESP_OK) {
            return result;
        }

        const size_t data_size = record[0];
        const uint16_t offset =
            ((uint16_t)record[1] << 8U) | record[2];
        const uint8_t type = record[3];
        const uint8_t *data = &record[4];

        if (type == 0x00U) {
            if ((data_size == 0U) ||
                (((uint32_t)offset + data_size) > 0x10000UL) ||
                (reader->address_base >
                 (UINT64_MAX - offset))) {

                return ESP_ERR_INVALID_ARG;
            }

            memcpy(reader->data, data, data_size);
            return firmware_image_publish_block(
                reader,
                reader->address_base + offset,
                data_size,
                block
            );
        }

        if (type == 0x01U) {
            if ((data_size != 0U) || (offset != 0U)) {
                return ESP_ERR_INVALID_ARG;
            }

            reader->terminated = true;
            result = firmware_image_validate_trailing_data(reader);
            return (result == ESP_OK)
                ? ESP_ERR_NOT_FOUND
                : result;
        }

        if (type == 0x02U) {
            if ((data_size != 2U) || (offset != 0U)) {
                return ESP_ERR_INVALID_ARG;
            }

            reader->address_base =
                (uint64_t)(((uint16_t)data[0] << 8U) | data[1]) << 4U;
            continue;
        }

        if (type == 0x04U) {
            if ((data_size != 2U) || (offset != 0U)) {
                return ESP_ERR_INVALID_ARG;
            }

            reader->address_base =
                (uint64_t)(((uint16_t)data[0] << 8U) | data[1]) << 16U;
            continue;
        }

        if ((type == 0x03U) || (type == 0x05U)) {
            if ((data_size != 4U) || (offset != 0U)) {
                return ESP_ERR_INVALID_ARG;
            }

            if (type == 0x03U) {
                const uint32_t segment =
                    ((uint32_t)data[0] << 8U) | data[1];
                const uint32_t entry_offset =
                    ((uint32_t)data[2] << 8U) | data[3];

                reader->entry_address =
                    ((uint64_t)segment << 4U) + entry_offset;
            } else {
                reader->entry_address =
                    ((uint32_t)data[0] << 24U) |
                    ((uint32_t)data[1] << 16U) |
                    ((uint32_t)data[2] << 8U) |
                    data[3];
            }

            reader->entry_address_valid = true;
            continue;
        }

        return ESP_ERR_NOT_SUPPORTED;
    }

    return ESP_ERR_NOT_FOUND;
}

static esp_err_t firmware_image_decode_s_record(
    const char *line,
    size_t length,
    uint8_t *type,
    uint8_t *record,
    size_t *record_size
)
{
    if ((length < 4U) || (line[0] != 'S') ||
        !isdigit((unsigned char)line[1]) ||
        ((length & 1U) != 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    *type = (uint8_t)(line[1] - '0');
    const size_t size = (length - 2U) / 2U;

    if ((size < 2U) ||
        (size > (FIRMWARE_IMAGE_RECORD_DATA_MAX_SIZE + 1U))) {

        return ESP_ERR_INVALID_SIZE;
    }

    for (size_t index = 0U; index < size; ++index) {
        if (!firmware_image_hex_byte(
                &line[2U + (index * 2U)],
                &record[index]
            )) {

            return ESP_ERR_INVALID_ARG;
        }
    }

    if ((record[0] != (size - 1U)) ||
        (record[0] == 0U)) {

        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t checksum = 0U;

    for (size_t index = 0U; index < size; ++index) {
        checksum = (uint8_t)(checksum + record[index]);
    }

    if (checksum != 0xFFU) {
        return ESP_ERR_INVALID_CRC;
    }

    *record_size = size;
    return ESP_OK;
}

static size_t firmware_image_s_record_address_size(
    uint8_t type
)
{
    switch (type) {
        case 0U:
        case 1U:
        case 5U:
        case 9U:
            return 2U;

        case 2U:
        case 6U:
        case 8U:
            return 3U;

        case 3U:
        case 7U:
            return 4U;

        default:
            return 0U;
    }
}

static uint64_t firmware_image_s_record_address(
    const uint8_t *record,
    size_t address_size
)
{
    uint64_t address = 0U;

    for (size_t index = 0U; index < address_size; ++index) {
        address = (address << 8U) | record[1U + index];
    }

    return address;
}

static esp_err_t firmware_image_next_s_record(
    firmware_image_reader_t *reader,
    firmware_image_block_t *block
)
{
    uint8_t record[FIRMWARE_IMAGE_RECORD_DATA_MAX_SIZE + 1U];

    while (!reader->terminated) {
        size_t line_length = 0U;
        esp_err_t result = firmware_image_read_line(
            reader,
            &line_length
        );

        if (result == ESP_ERR_NOT_FOUND) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        if (result != ESP_OK) {
            return result;
        }

        uint8_t type = 0U;
        size_t record_size = 0U;
        result = firmware_image_decode_s_record(
            reader->line,
            line_length,
            &type,
            record,
            &record_size
        );

        if (result != ESP_OK) {
            return result;
        }

        const size_t address_size =
            firmware_image_s_record_address_size(type);

        if ((address_size == 0U) ||
            (record_size < (address_size + 2U))) {

            return ESP_ERR_INVALID_ARG;
        }

        const uint64_t address =
            firmware_image_s_record_address(record, address_size);
        const size_t data_size =
            record_size - address_size - 2U;
        const uint8_t *data = &record[1U + address_size];

        if ((type >= 1U) && (type <= 3U)) {
            if ((data_size == 0U) || reader->count_record_seen) {
                return ESP_ERR_INVALID_ARG;
            }

            if (reader->s_record_data_type == 0U) {
                reader->s_record_data_type = type;
            } else if (reader->s_record_data_type != type) {
                return ESP_ERR_INVALID_RESPONSE;
            }

            memcpy(reader->data, data, data_size);
            return firmware_image_publish_block(
                reader,
                address,
                data_size,
                block
            );
        }

        if ((type == 5U) || (type == 6U)) {
            if ((data_size != 0U) ||
                reader->count_record_seen ||
                (reader->data_record_count == 0U)) {
                return ESP_ERR_INVALID_ARG;
            }

            reader->declared_record_count = (uint32_t)address;
            reader->count_record_seen = true;
            continue;
        }

        if ((type == 7U) || (type == 8U) || (type == 9U)) {
            if ((data_size != 0U) ||
                (reader->s_record_data_type == 0U) ||
                ((reader->s_record_data_type + type) != 10U) ||
                (reader->count_record_seen &&
                 (reader->declared_record_count !=
                  reader->data_record_count))) {

                return ESP_ERR_INVALID_RESPONSE;
            }

            reader->entry_address = address;
            reader->entry_address_valid = true;
            reader->terminated = true;
            result = firmware_image_validate_trailing_data(reader);
            return (result == ESP_OK)
                ? ESP_ERR_NOT_FOUND
                : result;
        }

        if (type != 0U) {
            return ESP_ERR_NOT_SUPPORTED;
        }

        if (reader->data_record_count != 0U) {
            return ESP_ERR_INVALID_RESPONSE;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t firmware_image_open(
    firmware_image_reader_t *reader,
    const firmware_image_config_t *config
)
{
    if ((reader == NULL) ||
        (config == NULL) ||
        (config->read == NULL) ||
        (config->file_size == 0U) ||
        (config->format > FIRMWARE_IMAGE_FORMAT_S_RECORD) ||
        ((config->format == FIRMWARE_IMAGE_FORMAT_BIN) &&
         (config->binary_address >
          (UINT64_MAX - config->file_size)))) {

        return ESP_ERR_INVALID_ARG;
    }

    memset(reader, 0, sizeof(*reader));
    reader->config = *config;
    return ESP_OK;
}

esp_err_t firmware_image_next(
    firmware_image_reader_t *reader,
    firmware_image_block_t *block
)
{
    if ((reader == NULL) || (block == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(block, 0, sizeof(*block));

    switch (reader->config.format) {
        case FIRMWARE_IMAGE_FORMAT_BIN:
            return firmware_image_next_bin(reader, block);

        case FIRMWARE_IMAGE_FORMAT_INTEL_HEX:
            return firmware_image_next_intel_hex(reader, block);

        case FIRMWARE_IMAGE_FORMAT_S_RECORD:
            return firmware_image_next_s_record(reader, block);

        default:
            return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t firmware_image_inspect(
    firmware_image_reader_t *reader,
    firmware_image_info_t *info
)
{
    if ((reader == NULL) || (info == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(info, 0, sizeof(*info));
    firmware_image_block_t block;
    esp_err_t result = ESP_OK;
    uint64_t previous_end = 0U;
    bool previous_valid = false;

    while ((result = firmware_image_next(reader, &block)) == ESP_OK) {
        if (info->block_count == 0U) {
            info->lowest_address = block.address;
        }

        if (!previous_valid || (block.address != previous_end)) {
            info->segment_count++;
        }

        if (info->data_size > (UINT64_MAX - block.size)) {
            return ESP_ERR_INVALID_SIZE;
        }

        info->data_size += block.size;
        info->highest_address = block.address + block.size - 1U;
        info->block_count++;
        previous_end = block.address + block.size;
        previous_valid = true;
    }

    if ((result != ESP_ERR_NOT_FOUND) ||
        (info->block_count == 0U) ||
        !reader->terminated) {

        return (result != ESP_ERR_NOT_FOUND)
            ? result
            : ESP_ERR_INVALID_RESPONSE;
    }

    info->entry_address = reader->entry_address;
    info->entry_address_valid = reader->entry_address_valid;
    return ESP_OK;
}

esp_err_t firmware_image_format_from_name(
    const char *file_name,
    firmware_image_format_t *format
)
{
    if ((file_name == NULL) || (format == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *extension = strrchr(file_name, '.');

    if (extension == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    char normalized[8];
    size_t length = strlen(extension);

    if (length >= sizeof(normalized)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    for (size_t index = 0U; index <= length; ++index) {
        normalized[index] =
            (char)tolower((unsigned char)extension[index]);
    }

    if (strcmp(normalized, ".bin") == 0) {
        *format = FIRMWARE_IMAGE_FORMAT_BIN;
        return ESP_OK;
    }

    if ((strcmp(normalized, ".hex") == 0) ||
        (strcmp(normalized, ".ihex") == 0)) {

        *format = FIRMWARE_IMAGE_FORMAT_INTEL_HEX;
        return ESP_OK;
    }

    if ((strcmp(normalized, ".srec") == 0) ||
        (strcmp(normalized, ".s19") == 0) ||
        (strcmp(normalized, ".s28") == 0) ||
        (strcmp(normalized, ".s37") == 0) ||
        (strcmp(normalized, ".mot") == 0)) {

        *format = FIRMWARE_IMAGE_FORMAT_S_RECORD;
        return ESP_OK;
    }

    return ESP_ERR_NOT_SUPPORTED;
}
