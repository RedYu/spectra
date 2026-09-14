/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "uds_security_provider.h"

#include <string.h>

#define UDS_SECURITY_PROVIDER_NO_ALGORITHM  (SIZE_MAX)

static void uds_security_provider_erase(
    void *data,
    size_t size
)
{
    volatile uint8_t *bytes = data;

    while (size > 0U) {
        *bytes = 0U;
        bytes++;
        size--;
    }
}

esp_err_t uds_security_provider_init(
    uds_security_provider_t *provider
)
{
    if (provider == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(
        provider,
        0,
        sizeof(*provider)
    );

    provider->selected_algorithm =
        UDS_SECURITY_PROVIDER_NO_ALGORITHM;
    provider->initialized = true;

    return ESP_OK;
}

void uds_security_provider_deinit(
    uds_security_provider_t *provider
)
{
    if (provider == NULL) {
        return;
    }

    uds_security_provider_erase(
        provider,
        sizeof(*provider)
    );
}

esp_err_t uds_security_provider_register(
    uds_security_provider_t *provider,
    uint32_t identifier,
    uds_security_provider_algorithm_cb_t callback,
    void *context
)
{
    if ((provider == NULL) ||
        !provider->initialized ||
        (callback == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    for (size_t index = 0U;
         index < provider->algorithm_count;
         index++) {

        if (provider->algorithms[index].identifier == identifier) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    if (provider->algorithm_count >=
        UDS_SECURITY_PROVIDER_ALGORITHM_COUNT) {

        return ESP_ERR_NO_MEM;
    }

    uds_security_provider_algorithm_t *algorithm =
        &provider->algorithms[provider->algorithm_count];

    algorithm->identifier = identifier;
    algorithm->callback = callback;
    algorithm->context = context;
    provider->algorithm_count++;

    return ESP_OK;
}

esp_err_t uds_security_provider_select(
    uds_security_provider_t *provider,
    uint32_t identifier
)
{
    if ((provider == NULL) || !provider->initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t index = 0U;
         index < provider->algorithm_count;
         index++) {

        if (provider->algorithms[index].identifier == identifier) {
            provider->selected_algorithm = index;
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t uds_security_provider_set_manual_key(
    uds_security_provider_t *provider,
    const uint8_t *key,
    size_t key_length
)
{
    if ((provider == NULL) ||
        !provider->initialized ||
        (key == NULL) ||
        (key_length == 0U) ||
        (key_length > sizeof(provider->manual_key))) {

        return ESP_ERR_INVALID_ARG;
    }

    uds_security_provider_clear_manual_key(provider);

    memcpy(
        provider->manual_key,
        key,
        key_length
    );

    provider->manual_key_length = key_length;
    provider->manual_key_pending = true;

    return ESP_OK;
}

void uds_security_provider_clear_manual_key(
    uds_security_provider_t *provider
)
{
    if (provider == NULL) {
        return;
    }

    uds_security_provider_erase(
        provider->manual_key,
        sizeof(provider->manual_key)
    );

    provider->manual_key_length = 0U;
    provider->manual_key_pending = false;
}

esp_err_t uds_security_provider_calculate(
    uint8_t security_level,
    const uint8_t *seed,
    size_t seed_length,
    uint8_t *key,
    size_t key_capacity,
    size_t *key_length,
    void *context
)
{
    uds_security_provider_t *provider = context;

    if ((provider == NULL) ||
        !provider->initialized ||
        (seed == NULL) ||
        (seed_length == 0U) ||
        (key == NULL) ||
        (key_length == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *key_length = 0U;

    if (provider->manual_key_pending) {
        const size_t manual_key_length =
            provider->manual_key_length;

        esp_err_t result = ESP_OK;

        if (manual_key_length > key_capacity) {
            result = ESP_ERR_INVALID_SIZE;
        } else {
            memcpy(
                key,
                provider->manual_key,
                manual_key_length
            );

            *key_length = manual_key_length;
        }

        uds_security_provider_clear_manual_key(provider);
        return result;
    }

    if (provider->selected_algorithm >=
        provider->algorithm_count) {

        return ESP_ERR_NOT_FOUND;
    }

    const uds_security_provider_algorithm_t *algorithm =
        &provider->algorithms[provider->selected_algorithm];

    return algorithm->callback(
        security_level,
        seed,
        seed_length,
        key,
        key_capacity,
        key_length,
        algorithm->context
    );
}
