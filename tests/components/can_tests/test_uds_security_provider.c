/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "unity.h"

#include "uds_security_provider.h"

static esp_err_t test_security_algorithm(
    uint8_t security_level,
    const uint8_t *seed,
    size_t seed_length,
    uint8_t *key,
    size_t key_capacity,
    size_t *key_length,
    void *context
)
{
    TEST_ASSERT_EQUAL_UINT8(0x01U, security_level);
    TEST_ASSERT_EQUAL_UINT(2U, seed_length);
    TEST_ASSERT_EQUAL_UINT8(0x12U, seed[0]);
    TEST_ASSERT_EQUAL_PTR((void *)0x1234U, context);

    if (key_capacity < 2U) {
        return ESP_ERR_INVALID_SIZE;
    }

    key[0] = seed[0] ^ 0xFFU;
    key[1] = seed[1] ^ 0xFFU;
    *key_length = 2U;

    return ESP_OK;
}

TEST_CASE(
    "UDS security provider registers and selects local algorithms",
    "[uds]"
)
{
    uds_security_provider_t provider = {0};
    const uint8_t seed[] = {0x12U, 0x34U};
    uint8_t key[2] = {0};
    size_t key_length = 0U;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_security_provider_init(&provider)
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_security_provider_register(
            &provider,
            7U,
            test_security_algorithm,
            (void *)0x1234U
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_STATE,
        uds_security_provider_register(
            &provider,
            7U,
            test_security_algorithm,
            NULL
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_ERR_NOT_FOUND,
        uds_security_provider_select(
            &provider,
            8U
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_security_provider_select(
            &provider,
            7U
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_security_provider_calculate(
            0x01U,
            seed,
            sizeof(seed),
            key,
            sizeof(key),
            &key_length,
            &provider
        )
    );
    TEST_ASSERT_EQUAL_UINT(2U, key_length);
    TEST_ASSERT_EQUAL_HEX8(0xEDU, key[0]);
    TEST_ASSERT_EQUAL_HEX8(0xCBU, key[1]);

    uds_security_provider_deinit(&provider);
}

TEST_CASE(
    "UDS security provider consumes a manual key once",
    "[uds]"
)
{
    uds_security_provider_t provider = {0};
    const uint8_t seed[] = {0x01U};
    const uint8_t manual_key[] = {0xAAU, 0x55U};
    uint8_t key[2] = {0};
    size_t key_length = 0U;

    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_security_provider_init(&provider)
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_security_provider_set_manual_key(
            &provider,
            manual_key,
            sizeof(manual_key)
        )
    );
    TEST_ASSERT_EQUAL(
        ESP_OK,
        uds_security_provider_calculate(
            0x01U,
            seed,
            sizeof(seed),
            key,
            sizeof(key),
            &key_length,
            &provider
        )
    );
    TEST_ASSERT_EQUAL_UINT(2U, key_length);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        manual_key,
        key,
        sizeof(manual_key)
    );
    TEST_ASSERT_FALSE(provider.manual_key_pending);
    TEST_ASSERT_EQUAL(
        ESP_ERR_NOT_FOUND,
        uds_security_provider_calculate(
            0x01U,
            seed,
            sizeof(seed),
            key,
            sizeof(key),
            &key_length,
            &provider
        )
    );

    uds_security_provider_deinit(&provider);
}
