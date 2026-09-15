/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "uds_did_catalog.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

static bool uds_did_text_valid(
    const char *text,
    size_t capacity,
    bool required
)
{
    const size_t length = strnlen(text, capacity);

    return (length < capacity) &&
           (!required || (length != 0U));
}

static uint64_t uds_did_read_unsigned(
    const uint8_t *data,
    size_t length,
    uds_did_byte_order_t byte_order
)
{
    uint64_t result = 0U;

    if (byte_order == UDS_DID_BYTE_ORDER_BIG_ENDIAN) {
        for (size_t index = 0U; index < length; ++index) {
            result = (result << 8U) | data[index];
        }
    } else {
        for (size_t index = length; index > 0U; --index) {
            result = (result << 8U) | data[index - 1U];
        }
    }

    return result;
}

static bool uds_did_utf8_valid(
    const uint8_t *data,
    size_t length
)
{
    size_t index = 0U;

    while (index < length) {
        const uint8_t first = data[index++];
        size_t continuation_count = 0U;
        uint32_t code_point = 0U;
        uint32_t minimum = 0U;

        if (first <= 0x7FU) {
            if (first == 0U) {
                return false;
            }

            continue;
        } else if ((first & 0xE0U) == 0xC0U) {
            continuation_count = 1U;
            code_point = first & 0x1FU;
            minimum = 0x80U;
        } else if ((first & 0xF0U) == 0xE0U) {
            continuation_count = 2U;
            code_point = first & 0x0FU;
            minimum = 0x800U;
        } else if ((first & 0xF8U) == 0xF0U) {
            continuation_count = 3U;
            code_point = first & 0x07U;
            minimum = 0x10000U;
        } else {
            return false;
        }

        if (continuation_count > (length - index)) {
            return false;
        }

        for (size_t count = 0U;
             count < continuation_count;
             ++count) {

            const uint8_t continuation = data[index++];

            if ((continuation & 0xC0U) != 0x80U) {
                return false;
            }

            code_point =
                (code_point << 6U) |
                (continuation & 0x3FU);
        }

        if ((code_point < minimum) ||
            (code_point > 0x10FFFFU) ||
            ((code_point >= 0xD800U) &&
             (code_point <= 0xDFFFU))) {

            return false;
        }
    }

    return true;
}

esp_err_t uds_did_definition_validate(
    const uds_did_definition_t *definition
)
{
    if ((definition == NULL) ||
        !uds_did_text_valid(
            definition->name,
            sizeof(definition->name),
            true
        ) ||
        !uds_did_text_valid(
            definition->unit,
            sizeof(definition->unit),
            false
        ) ||
        !uds_did_text_valid(
            definition->description,
            sizeof(definition->description),
            false
        ) ||
        (definition->data_type >= UDS_DID_DATA_TYPE_COUNT) ||
        (definition->byte_order >= UDS_DID_BYTE_ORDER_COUNT) ||
        (definition->data_length == 0U) ||
        (definition->data_length > UDS_DID_DATA_MAX_LENGTH) ||
        !isfinite(definition->scale) ||
        !isfinite(definition->offset)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (((definition->data_type == UDS_DID_DATA_UNSIGNED) ||
         (definition->data_type == UDS_DID_DATA_SIGNED)) &&
        (definition->data_length > sizeof(uint64_t))) {

        return ESP_ERR_INVALID_ARG;
    }

    if ((definition->data_type == UDS_DID_DATA_FLOAT) &&
        (definition->data_length != sizeof(float)) &&
        (definition->data_length != sizeof(double))) {

        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

esp_err_t uds_did_catalog_validate(
    const uds_did_catalog_t *catalog
)
{
    if ((catalog == NULL) ||
        ((catalog->definitions == NULL) &&
         (catalog->count != 0U))) {

        return ESP_ERR_INVALID_ARG;
    }

    for (size_t index = 0U;
         index < catalog->count;
         ++index) {

        if (uds_did_definition_validate(
                &catalog->definitions[index]
            ) != ESP_OK) {

            return ESP_ERR_INVALID_ARG;
        }

        for (size_t previous = 0U;
             previous < index;
             ++previous) {

            if (catalog->definitions[previous].identifier ==
                catalog->definitions[index].identifier) {

                return ESP_ERR_INVALID_ARG;
            }
        }
    }

    return ESP_OK;
}

const uds_did_definition_t *uds_did_catalog_find(
    const uds_did_catalog_t *catalog,
    uint16_t identifier
)
{
    if ((catalog == NULL) ||
        ((catalog->definitions == NULL) &&
         (catalog->count != 0U))) {

        return NULL;
    }

    for (size_t index = 0U;
         index < catalog->count;
         ++index) {

        if (catalog->definitions[index].identifier == identifier) {
            return &catalog->definitions[index];
        }
    }

    return NULL;
}

esp_err_t uds_did_decode(
    const uds_did_definition_t *definition,
    const uint8_t *data,
    size_t data_length,
    char *text_buffer,
    size_t text_capacity,
    uds_did_value_t *value
)
{
    if ((uds_did_definition_validate(definition) != ESP_OK) ||
        (data == NULL) ||
        (data_length != definition->data_length) ||
        (value == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    memset(value, 0, sizeof(*value));
    value->bytes = data;
    value->length = data_length;

    if (definition->data_type == UDS_DID_DATA_BYTES) {
        value->type = UDS_DID_VALUE_BYTES;
        return ESP_OK;
    }

    if ((definition->data_type == UDS_DID_DATA_ASCII) ||
        (definition->data_type == UDS_DID_DATA_UTF8)) {

        if ((text_buffer == NULL) ||
            (text_capacity <= data_length)) {

            return ESP_ERR_INVALID_SIZE;
        }

        if (definition->data_type == UDS_DID_DATA_ASCII) {
            for (size_t index = 0U;
                 index < data_length;
                 ++index) {

                if ((data[index] < 0x20U) ||
                    (data[index] > 0x7EU)) {

                    return ESP_ERR_INVALID_RESPONSE;
                }
            }
        } else if (!uds_did_utf8_valid(data, data_length)) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        memcpy(text_buffer, data, data_length);
        text_buffer[data_length] = '\0';
        value->type = UDS_DID_VALUE_TEXT;
        return ESP_OK;
    }

    const uint64_t raw = uds_did_read_unsigned(
        data,
        data_length,
        definition->byte_order
    );

    value->type = UDS_DID_VALUE_NUMBER;
    value->raw_unsigned = raw;

    double numeric_value = 0.0;

    if (definition->data_type == UDS_DID_DATA_UNSIGNED) {
        numeric_value = (double)raw;
    } else if (definition->data_type == UDS_DID_DATA_SIGNED) {
        const size_t bit_count = data_length * 8U;
        int64_t signed_value = 0;

        if (bit_count == 64U) {
            memcpy(&signed_value, &raw, sizeof(signed_value));
        } else if ((raw & (1ULL << (bit_count - 1U))) != 0U) {
            const uint64_t mask = (1ULL << bit_count) - 1ULL;
            signed_value =
                -(int64_t)(((~raw) & mask) + 1ULL);
        } else {
            signed_value = (int64_t)raw;
        }

        value->raw_signed = signed_value;
        numeric_value = (double)signed_value;
    } else if (data_length == sizeof(float)) {
        const uint32_t bits = (uint32_t)raw;
        float float_value = 0.0F;

        memcpy(&float_value, &bits, sizeof(float_value));

        if (!isfinite(float_value)) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        numeric_value = float_value;
    } else {
        double double_value = 0.0;

        memcpy(&double_value, &raw, sizeof(double_value));

        if (!isfinite(double_value)) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        numeric_value = double_value;
    }

    value->physical_value =
        (numeric_value * definition->scale) +
        definition->offset;

    if (!isfinite(value->physical_value)) {
        memset(value, 0, sizeof(*value));
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}
