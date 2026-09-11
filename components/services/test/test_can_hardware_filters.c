/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "unity.h"

#include "can_service.h"
#include "can_fd_mcp2518fd_driver.h"

TEST_CASE(
    "TWAI rejects invalid masks before stopping the interface",
    "[can_filters]"
)
{
    can_twai_acceptance_filter_t filter = {
        .identifier = 0x800U,
        .mask = 0x7FFU,
    };

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        can_service_set_acceptance_filter(NULL)
    );

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        can_service_set_acceptance_filter(&filter)
    );

    filter.identifier = 0x123U;
    filter.mask = 0x800U;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        can_service_set_acceptance_filter(&filter)
    );

    filter.extended = true;
    filter.mask = 0x20000000U;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        can_service_set_acceptance_filter(&filter)
    );
}

TEST_CASE(
    "MCP2518FD rejects invalid banks before SPI access",
    "[can_filters]"
)
{
    can_fd_mcp2518fd_filter_bank_t bank = {0};

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        can_fd_mcp2518fd_driver_set_filter_bank(NULL)
    );

    bank.count = CAN_FD_MCP2518FD_FILTER_COUNT + 1U;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        can_fd_mcp2518fd_driver_set_filter_bank(&bank)
    );

    bank.count = 1U;
    bank.accept_all = true;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        can_fd_mcp2518fd_driver_set_filter_bank(&bank)
    );

    bank.accept_all = false;
    bank.filters[0].index = CAN_FD_MCP2518FD_FILTER_COUNT;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        can_fd_mcp2518fd_driver_set_filter_bank(&bank)
    );

    bank.filters[0].index = 0U;
    bank.filters[0].identifier = 0x800U;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        can_fd_mcp2518fd_driver_set_filter_bank(&bank)
    );

    bank.filters[0].identifier = 0x123U;
    bank.count = 2U;
    bank.filters[1].index = 0U;

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        can_fd_mcp2518fd_driver_set_filter_bank(&bank)
    );
}
