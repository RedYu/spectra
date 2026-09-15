/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include <stdlib.h>
#include <string.h>

#include "unity.h"

#include "uds_did_catalog.h"
#include "uds_did_catalog_service.h"

static uds_did_definition_t test_uds_did_definition(void)
{
    uds_did_definition_t definition = {
        .identifier = 0xF190U,
        .data_type = UDS_DID_DATA_UNSIGNED,
        .byte_order = UDS_DID_BYTE_ORDER_BIG_ENDIAN,
        .data_length = 2U,
        .scale = 0.1,
        .offset = -40.0,
    };

    (void)strncpy(
        definition.name,
        "Coolant temperature",
        sizeof(definition.name) - 1U
    );
    (void)strncpy(
        definition.unit,
        "deg C",
        sizeof(definition.unit) - 1U
    );

    return definition;
}

TEST_CASE(
    "UDS DID catalog rejects duplicate identifiers",
    "[uds]"
)
{
    uds_did_definition_t definitions[2] = {
        test_uds_did_definition(),
        test_uds_did_definition(),
    };
    const uds_did_catalog_t catalog = {
        .definitions = definitions,
        .count = 2U,
    };

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        uds_did_catalog_validate(&catalog)
    );

    definitions[1].identifier = 0xF191U;
    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_did_catalog_validate(&catalog)
    );
    TEST_ASSERT_EQUAL_PTR(
        &definitions[1],
        uds_did_catalog_find(&catalog, 0xF191U)
    );
}

TEST_CASE(
    "UDS DID decoder applies byte order scale and offset",
    "[uds]"
)
{
    uds_did_definition_t definition =
        test_uds_did_definition();
    const uint8_t big_endian[] = {0x01U, 0x90U};
    uds_did_value_t value;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_did_decode(
            &definition,
            big_endian,
            sizeof(big_endian),
            NULL,
            0U,
            &value
        )
    );
    TEST_ASSERT_EQUAL_HEX64(0x190U, value.raw_unsigned);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 0.0, value.physical_value);

    definition.byte_order = UDS_DID_BYTE_ORDER_LITTLE_ENDIAN;
    const uint8_t little_endian[] = {0x90U, 0x01U};

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_did_decode(
            &definition,
            little_endian,
            sizeof(little_endian),
            NULL,
            0U,
            &value
        )
    );
    TEST_ASSERT_EQUAL_HEX64(0x190U, value.raw_unsigned);
}

TEST_CASE(
    "UDS DID decoder sign extends integer values",
    "[uds]"
)
{
    uds_did_definition_t definition =
        test_uds_did_definition();
    definition.data_type = UDS_DID_DATA_SIGNED;
    definition.data_length = 1U;
    definition.scale = 1.0;
    definition.offset = 0.0;
    const uint8_t data[] = {0xFBU};
    uds_did_value_t value;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_did_decode(
            &definition,
            data,
            sizeof(data),
            NULL,
            0U,
            &value
        )
    );
    TEST_ASSERT_EQUAL_INT64(-5, value.raw_signed);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, -5.0, value.physical_value);
}

TEST_CASE(
    "UDS DID decoder validates ASCII and UTF-8 text",
    "[uds]"
)
{
    uds_did_definition_t definition =
        test_uds_did_definition();
    definition.data_type = UDS_DID_DATA_ASCII;
    definition.data_length = 3U;
    definition.scale = 1.0;
    definition.offset = 0.0;
    const uint8_t ascii[] = {'A', 'B', 'C'};
    char text[4];
    uds_did_value_t value;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_did_decode(
            &definition,
            ascii,
            sizeof(ascii),
            text,
            sizeof(text),
            &value
        )
    );
    TEST_ASSERT_EQUAL_STRING("ABC", text);

    definition.data_type = UDS_DID_DATA_UTF8;
    definition.data_length = 2U;
    const uint8_t invalid_utf8[] = {0xC0U, 0x80U};

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_RESPONSE,
        uds_did_decode(
            &definition,
            invalid_utf8,
            sizeof(invalid_utf8),
            text,
            sizeof(text),
            &value
        )
    );
}

TEST_CASE(
    "UDS DID catalog JSON preserves definitions",
    "[uds]"
)
{
    uds_did_definition_t source_definition =
        test_uds_did_definition();
    const uds_did_catalog_document_t source = {
        .name = "Powertrain",
        .description = "Powertrain data identifiers",
        .definitions = &source_definition,
        .count = 1U,
        .capacity = 1U,
    };
    char *json = NULL;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_did_catalog_service_encode_json(
            &source,
            &json
        )
    );
    TEST_ASSERT_NOT_NULL(json);

    uds_did_definition_t decoded_definition;
    uds_did_catalog_document_t decoded = {
        .definitions = &decoded_definition,
        .capacity = 1U,
    };

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_did_catalog_service_decode_json(
            json,
            &decoded
        )
    );
    TEST_ASSERT_EQUAL_STRING("Powertrain", decoded.name);
    TEST_ASSERT_EQUAL_STRING(
        "Powertrain data identifiers",
        decoded.description
    );
    TEST_ASSERT_EQUAL_UINT32(1U, decoded.count);
    TEST_ASSERT_EQUAL_HEX16(
        source_definition.identifier,
        decoded_definition.identifier
    );
    TEST_ASSERT_EQUAL_STRING(
        source_definition.name,
        decoded_definition.name
    );
    TEST_ASSERT_EQUAL_STRING(
        source_definition.unit,
        decoded_definition.unit
    );
    TEST_ASSERT_EQUAL(
        source_definition.data_type,
        decoded_definition.data_type
    );
    TEST_ASSERT_EQUAL(
        source_definition.byte_order,
        decoded_definition.byte_order
    );
    TEST_ASSERT_EQUAL_UINT32(
        source_definition.data_length,
        decoded_definition.data_length
    );
    TEST_ASSERT_DOUBLE_WITHIN(
        0.001,
        source_definition.scale,
        decoded_definition.scale
    );
    TEST_ASSERT_DOUBLE_WITHIN(
        0.001,
        source_definition.offset,
        decoded_definition.offset
    );

    free(json);
}

TEST_CASE(
    "UDS DID catalog JSON rejects unsupported schema",
    "[uds]"
)
{
    const char json[] =
        "{\"version\":2,\"name\":\"Future\","
        "\"description\":\"\",\"definitions\":[]}";
    uds_did_catalog_document_t document = {0};

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        uds_did_catalog_service_decode_json(
            json,
            &document
        )
    );
}

TEST_CASE(
    "UDS DID catalog streaming parser decodes escapes",
    "[uds]"
)
{
    const char json[] =
        "{\"description\":\"Engine \\\"live\\\" data\","
        "\"definitions\":[{\"offset\":-40,"
        "\"scale\":0.1,\"data_length\":2,"
        "\"byte_order\":\"big_endian\","
        "\"data_type\":\"unsigned\","
        "\"description\":\"Coolant temperature\","
        "\"unit\":\"\\u00B0C\",\"name\":\"ECT\","
        "\"identifier\":5}],\"name\":\"Powertrain\","
        "\"version\":1}";
    uds_did_definition_t definition;
    uds_did_catalog_document_t document = {
        .definitions = &definition,
        .capacity = 1U,
    };

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_did_catalog_service_decode_json(
            json,
            &document
        )
    );
    TEST_ASSERT_EQUAL_STRING(
        "Engine \"live\" data",
        document.description
    );
    TEST_ASSERT_EQUAL_STRING("\xC2\xB0" "C", definition.unit);
    TEST_ASSERT_EQUAL_HEX16(5U, definition.identifier);
}

TEST_CASE(
    "UDS DID catalog streaming parser rejects duplicate fields",
    "[uds]"
)
{
    const char json[] =
        "{\"version\":1,\"version\":1,\"name\":\"Duplicate\","
        "\"description\":\"\",\"definitions\":[]}";
    uds_did_catalog_document_t document = {0};

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        uds_did_catalog_service_decode_json(
            json,
            &document
        )
    );
}
