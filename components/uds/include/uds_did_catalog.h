/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UDS_DID_NAME_MAX_LENGTH         (64U)
#define UDS_DID_UNIT_MAX_LENGTH         (24U)
#define UDS_DID_DESCRIPTION_MAX_LENGTH  (128U)
#define UDS_DID_DATA_MAX_LENGTH         (256U)

typedef enum
{
    UDS_DID_DATA_UNSIGNED = 0,
    UDS_DID_DATA_SIGNED,
    UDS_DID_DATA_FLOAT,
    UDS_DID_DATA_ASCII,
    UDS_DID_DATA_UTF8,
    UDS_DID_DATA_BYTES,

    UDS_DID_DATA_TYPE_COUNT,

} uds_did_data_type_t;

typedef enum
{
    UDS_DID_BYTE_ORDER_BIG_ENDIAN = 0,
    UDS_DID_BYTE_ORDER_LITTLE_ENDIAN,

    UDS_DID_BYTE_ORDER_COUNT,

} uds_did_byte_order_t;

typedef struct
{
    uint16_t identifier;
    char name[UDS_DID_NAME_MAX_LENGTH];
    char unit[UDS_DID_UNIT_MAX_LENGTH];
    char description[UDS_DID_DESCRIPTION_MAX_LENGTH];
    uds_did_data_type_t data_type;
    uds_did_byte_order_t byte_order;
    size_t data_length;
    double scale;
    double offset;

} uds_did_definition_t;

typedef struct
{
    const uds_did_definition_t *definitions;
    size_t count;

} uds_did_catalog_t;

typedef enum
{
    UDS_DID_VALUE_NUMBER = 0,
    UDS_DID_VALUE_TEXT,
    UDS_DID_VALUE_BYTES,

} uds_did_value_type_t;

typedef struct
{
    uds_did_value_type_t type;
    double physical_value;
    uint64_t raw_unsigned;
    int64_t raw_signed;
    const uint8_t *bytes;
    size_t length;

} uds_did_value_t;

/**
 * @brief Validate one DID definition.
 */
esp_err_t uds_did_definition_validate(
    const uds_did_definition_t *definition
);

/**
 * @brief Validate all definitions and reject duplicate identifiers.
 */
esp_err_t uds_did_catalog_validate(
    const uds_did_catalog_t *catalog
);

/**
 * @brief Find one DID definition using its 16-bit identifier.
 *
 * The function performs a linear search and does not allocate memory.
 */
const uds_did_definition_t *uds_did_catalog_find(
    const uds_did_catalog_t *catalog,
    uint16_t identifier
);

/**
 * @brief Decode the data bytes returned after a ReadDataByIdentifier DID.
 *
 * Numeric results preserve the raw integer and provide a scaled physical
 * value. Text is copied into text_buffer and terminated with a null byte.
 * Byte-array results refer directly to the supplied data buffer.
 */
esp_err_t uds_did_decode(
    const uds_did_definition_t *definition,
    const uint8_t *data,
    size_t data_length,
    char *text_buffer,
    size_t text_capacity,
    uds_did_value_t *value
);

#ifdef __cplusplus
}
#endif
