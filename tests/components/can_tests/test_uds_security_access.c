/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "unity.h"

#include "uds_security_access.h"

TEST_CASE(
    "UDS Security Access adapter validates its configuration",
    "[uds]"
)
{
    uds_security_access_t adapter = {0};
    uds_security_access_config_t config = {0};

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        uds_security_access_open(
            NULL,
            &config
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        uds_security_access_open(
            &adapter,
            NULL
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        uds_security_access_open(
            &adapter,
            &config
        )
    );
    TEST_ASSERT_NULL(
        uds_security_access_client(NULL)
    );
}

TEST_CASE(
    "UDS Security Access adapter rejects operations before open",
    "[uds]"
)
{
    uds_security_access_t adapter = {0};

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        uds_security_access_unlock(
            &adapter,
            0x01U,
            NULL,
            0U,
            0U
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        uds_security_access_poll(
            &adapter,
            0U
        )
    );
}
