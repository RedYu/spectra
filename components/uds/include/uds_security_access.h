/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "uds_client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    UDS_SECURITY_ACCESS_CLOSED = 0,
    UDS_SECURITY_ACCESS_IDLE,
    UDS_SECURITY_ACCESS_REQUESTING_SEED,
    UDS_SECURITY_ACCESS_CALCULATING_KEY,
    UDS_SECURITY_ACCESS_SENDING_KEY,
    UDS_SECURITY_ACCESS_UNLOCKED,
    UDS_SECURITY_ACCESS_ERROR,

} uds_security_access_state_t;

typedef esp_err_t (*uds_security_access_algorithm_cb_t)(
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
    uds_client_config_t client;
    uds_security_access_algorithm_cb_t algorithm;
    void *algorithm_context;
    uint8_t *seed_buffer;
    size_t seed_capacity;
    uint8_t *key_buffer;
    size_t key_capacity;

} uds_security_access_config_t;

typedef struct
{
    uds_security_access_config_t config;
    uds_client_t client;
    uds_client_event_cb_t client_callback;
    void *client_callback_context;
    uds_security_access_state_t state;
    uint8_t security_level;
    size_t seed_length;
    esp_err_t last_result;

} uds_security_access_t;

esp_err_t uds_security_access_open(
    uds_security_access_t *adapter,
    const uds_security_access_config_t *config
);

esp_err_t uds_security_access_close(
    uds_security_access_t *adapter
);

esp_err_t uds_security_access_unlock(
    uds_security_access_t *adapter,
    uint8_t security_level,
    const uint8_t *data_record,
    size_t data_record_length,
    uint64_t now_us
);

esp_err_t uds_security_access_poll(
    uds_security_access_t *adapter,
    uint64_t now_us
);

uds_client_t *uds_security_access_client(
    uds_security_access_t *adapter
);

#ifdef __cplusplus
}
#endif
