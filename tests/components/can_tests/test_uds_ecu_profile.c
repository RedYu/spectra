/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include <string.h>
#include <stdlib.h>

#include "unity.h"

#include "uds_ecu_profile.h"
#include "uds_profile_service.h"

static uds_ecu_profile_t test_uds_ecu_profile(void)
{
    uds_ecu_profile_t profile;

    uds_ecu_profile_initialize(&profile);
    (void)strncpy(
        profile.name,
        "Example ECU",
        sizeof(profile.name) - 1U
    );

    return profile;
}

TEST_CASE(
    "UDS ECU profile initializes with valid defaults",
    "[uds]"
)
{
    uds_ecu_profile_t profile = test_uds_ecu_profile();

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_ecu_profile_validate(&profile)
    );
    TEST_ASSERT_EQUAL(CAN_BUS_PRIMARY, profile.transport.bus);
    TEST_ASSERT_EQUAL_HEX32(
        0x7E0U,
        profile.transport.transmit_identifier
    );
    TEST_ASSERT_EQUAL_HEX32(
        0x7E8U,
        profile.transport.receive_identifier
    );
    TEST_ASSERT_TRUE(
        profile.programming.restore_default_session
    );
}

TEST_CASE(
    "UDS ECU profile rejects incompatible CAN settings",
    "[uds]"
)
{
    uds_ecu_profile_t profile = test_uds_ecu_profile();

    profile.transport.link_data_length = 64U;
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        uds_ecu_profile_validate(&profile)
    );

    profile = test_uds_ecu_profile();
    profile.transport.can_fd = true;
    profile.transport.link_data_length = 64U;
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        uds_ecu_profile_validate(&profile)
    );

    profile.transport.bus = CAN_BUS_SECONDARY;
    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_ecu_profile_validate(&profile)
    );
}

TEST_CASE(
    "UDS ECU profile validates security and routine policies",
    "[uds]"
)
{
    uds_ecu_profile_t profile = test_uds_ecu_profile();

    profile.programming.security_level = 2U;
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        uds_ecu_profile_validate(&profile)
    );

    profile.programming.security_level = 1U;
    profile.programming.erase.enabled = true;
    profile.programming.erase.identifier = 0xFF00U;
    profile.programming.erase.status_enabled = true;
    profile.programming.erase.pending_value = 1U;
    profile.programming.erase.success_value = 1U;
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        uds_ecu_profile_validate(&profile)
    );

    profile.programming.erase.success_value = 0U;
    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_ecu_profile_validate(&profile)
    );
}

TEST_CASE(
    "UDS ECU profile JSON preserves transport and routines",
    "[uds]"
)
{
    uds_ecu_profile_t source = test_uds_ecu_profile();
    source.transport.bus = CAN_BUS_SECONDARY;
    source.transport.can_fd = true;
    source.transport.bit_rate_switch = true;
    source.transport.link_data_length = 64U;
    source.programming.default_memory_address =
        0x123456789ABCDEF0ULL;
    source.programming.erase.enabled = true;
    source.programming.erase.identifier = 0xFF00U;
    source.programming.erase.option_record[0] = 0x12U;
    source.programming.erase.option_record[1] = 0x34U;
    source.programming.erase.option_record_length = 2U;

    char *json = NULL;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_profile_service_encode_json(
            &source,
            &json
        )
    );
    TEST_ASSERT_NOT_NULL(json);

    uds_ecu_profile_t decoded;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_profile_service_decode_json(
            json,
            &decoded
        )
    );
    TEST_ASSERT_EQUAL(CAN_BUS_SECONDARY, decoded.transport.bus);
    TEST_ASSERT_TRUE(decoded.transport.can_fd);
    TEST_ASSERT_TRUE(decoded.transport.bit_rate_switch);
    TEST_ASSERT_EQUAL(64U, decoded.transport.link_data_length);
    TEST_ASSERT_EQUAL_HEX64(
        source.programming.default_memory_address,
        decoded.programming.default_memory_address
    );
    TEST_ASSERT_EQUAL_HEX16(
        0xFF00U,
        decoded.programming.erase.identifier
    );
    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        source.programming.erase.option_record,
        decoded.programming.erase.option_record,
        source.programming.erase.option_record_length
    );

    free(json);
}
