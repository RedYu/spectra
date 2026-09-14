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

#define UDS_SECURITY_PROVIDER_ALGORITHM_COUNT  (16U)
#define UDS_SECURITY_PROVIDER_MANUAL_KEY_SIZE  (64U)

typedef esp_err_t (*uds_security_provider_algorithm_cb_t)(
    uint8_t security_level,
    const uint8_t *seed,
    size_t seed_length,
    uint8_t *key,
    size_t key_capacity,
    size_t *key_length,
    void *context
);

typedef struct
{
    uint32_t identifier;
    uds_security_provider_algorithm_cb_t callback;
    void *context;

} uds_security_provider_algorithm_t;

typedef struct
{
    uds_security_provider_algorithm_t
        algorithms[UDS_SECURITY_PROVIDER_ALGORITHM_COUNT];
    size_t algorithm_count;
    size_t selected_algorithm;
    uint8_t manual_key[UDS_SECURITY_PROVIDER_MANUAL_KEY_SIZE];
    size_t manual_key_length;
    bool manual_key_pending;
    bool initialized;

} uds_security_provider_t;

/**
 * @brief Initialize an empty SecurityAccess provider.
 */
esp_err_t uds_security_provider_init(
    uds_security_provider_t *provider
);

/**
 * @brief Remove registered algorithms and erase a pending manual key.
 */
void uds_security_provider_deinit(
    uds_security_provider_t *provider
);

/**
 * @brief Register one locally implemented seed-key algorithm.
 */
esp_err_t uds_security_provider_register(
    uds_security_provider_t *provider,
    uint32_t identifier,
    uds_security_provider_algorithm_cb_t callback,
    void *context
);

/**
 * @brief Select a registered local algorithm by identifier.
 */
esp_err_t uds_security_provider_select(
    uds_security_provider_t *provider,
    uint32_t identifier
);

/**
 * @brief Supply a key for the next calculation request.
 *
 * The key is consumed and erased after one calculation attempt.
 */
esp_err_t uds_security_provider_set_manual_key(
    uds_security_provider_t *provider,
    const uint8_t *key,
    size_t key_length
);

/**
 * @brief Erase the pending manual key.
 */
void uds_security_provider_clear_manual_key(
    uds_security_provider_t *provider
);

/**
 * @brief Calculate or return a manually supplied SecurityAccess key.
 *
 * This function matches uds_security_access_algorithm_cb_t and can be
 * assigned directly to uds_security_access_config_t::algorithm.
 */
esp_err_t uds_security_provider_calculate(
    uint8_t security_level,
    const uint8_t *seed,
    size_t seed_length,
    uint8_t *key,
    size_t key_capacity,
    size_t *key_length,
    void *context
);

#ifdef __cplusplus
}
#endif
