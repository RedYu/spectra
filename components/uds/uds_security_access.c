/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "uds_security_access.h"

#include <string.h>

static void uds_security_access_client_callback(
    const uds_client_event_t *event,
    void *context
);

static bool uds_security_access_can_start(
    const uds_security_access_t *adapter
)
{
    return (adapter->state == UDS_SECURITY_ACCESS_IDLE) ||
           (adapter->state == UDS_SECURITY_ACCESS_UNLOCKED) ||
           (adapter->state == UDS_SECURITY_ACCESS_ERROR);
}

esp_err_t uds_security_access_open(
    uds_security_access_t *adapter,
    const uds_security_access_config_t *config
)
{
    if ((adapter == NULL) ||
        (config == NULL) ||
        (config->algorithm == NULL) ||
        (config->seed_buffer == NULL) ||
        (config->seed_capacity == 0U) ||
        (config->key_buffer == NULL) ||
        (config->key_capacity == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    memset(
        adapter,
        0,
        sizeof(*adapter)
    );

    adapter->config = *config;
    adapter->client_callback = config->client.callback;
    adapter->client_callback_context =
        config->client.callback_context;
    adapter->config.client.callback =
        uds_security_access_client_callback;
    adapter->config.client.callback_context = adapter;

    const esp_err_t result =
        uds_client_open(
            &adapter->client,
            &adapter->config.client
        );

    adapter->last_result = result;
    adapter->state =
        (result == ESP_OK)
            ? UDS_SECURITY_ACCESS_IDLE
            : UDS_SECURITY_ACCESS_CLOSED;

    return result;
}

esp_err_t uds_security_access_close(
    uds_security_access_t *adapter
)
{
    if (adapter == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t result =
        uds_client_close(&adapter->client);

    if (result == ESP_OK) {
        adapter->state = UDS_SECURITY_ACCESS_CLOSED;
        adapter->seed_length = 0U;
    }

    adapter->last_result = result;
    return result;
}

esp_err_t uds_security_access_unlock(
    uds_security_access_t *adapter,
    uint8_t security_level,
    const uint8_t *data_record,
    size_t data_record_length,
    uint64_t now_us
)
{
    if (adapter == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!uds_security_access_can_start(adapter)) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result =
        uds_client_security_access_request_seed(
            &adapter->client,
            security_level,
            data_record,
            data_record_length,
            now_us
        );

    if (result == ESP_OK) {
        adapter->security_level = security_level;
        adapter->seed_length = 0U;
        adapter->state = UDS_SECURITY_ACCESS_REQUESTING_SEED;
    } else {
        adapter->state = UDS_SECURITY_ACCESS_ERROR;
    }

    adapter->last_result = result;
    return result;
}

esp_err_t uds_security_access_poll(
    uds_security_access_t *adapter,
    uint64_t now_us
)
{
    if (adapter == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (adapter->state == UDS_SECURITY_ACCESS_CLOSED) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result =
        uds_client_poll(
            &adapter->client,
            now_us
        );

    if (result != ESP_OK) {
        adapter->state = UDS_SECURITY_ACCESS_ERROR;
        adapter->last_result = result;
        return result;
    }

    if (adapter->state !=
        UDS_SECURITY_ACCESS_CALCULATING_KEY) {

        return ESP_OK;
    }

    size_t key_length = 0U;

    result =
        adapter->config.algorithm(
            adapter->security_level,
            adapter->config.seed_buffer,
            adapter->seed_length,
            adapter->config.key_buffer,
            adapter->config.key_capacity,
            &key_length,
            adapter->config.algorithm_context
        );

    if ((result == ESP_OK) &&
        ((key_length == 0U) ||
         (key_length > adapter->config.key_capacity) ||
         (key_length > UDS_CLIENT_SECURITY_DATA_MAX_LENGTH))) {

        result = ESP_ERR_INVALID_SIZE;
    }

    if (result == ESP_OK) {
        result =
            uds_client_security_access_send_key(
                &adapter->client,
                adapter->security_level,
                adapter->config.key_buffer,
                key_length,
                now_us
            );
    }

    adapter->last_result = result;
    adapter->state =
        (result == ESP_OK)
            ? UDS_SECURITY_ACCESS_SENDING_KEY
            : UDS_SECURITY_ACCESS_ERROR;

    return result;
}

uds_client_t *uds_security_access_client(
    uds_security_access_t *adapter
)
{
    return (adapter != NULL)
        ? &adapter->client
        : NULL;
}

static void uds_security_access_client_callback(
    const uds_client_event_t *event,
    void *context
)
{
    uds_security_access_t *adapter = context;

    if ((event == NULL) || (adapter == NULL)) {
        return;
    }

    const bool exchange_response =
        (event->type == UDS_CLIENT_EVENT_RESPONSE) &&
        ((adapter->state ==
          UDS_SECURITY_ACCESS_REQUESTING_SEED) ||
         (adapter->state ==
          UDS_SECURITY_ACCESS_SENDING_KEY));

    if (exchange_response &&
        ((event->response.service_id !=
          (UDS_SERVICE_SECURITY_ACCESS +
           UDS_POSITIVE_RESPONSE_OFFSET)) ||
         (event->response.payload_length < 1U))) {

        adapter->state = UDS_SECURITY_ACCESS_ERROR;
        adapter->last_result = ESP_ERR_INVALID_RESPONSE;
    } else if (exchange_response) {

        const uint8_t subfunction =
            event->response.payload[0];

        if ((adapter->state ==
             UDS_SECURITY_ACCESS_REQUESTING_SEED) &&
            (subfunction == adapter->security_level)) {

            const size_t seed_length =
                event->response.payload_length - 1U;

            if (seed_length == 0U) {
                adapter->state = UDS_SECURITY_ACCESS_UNLOCKED;
                adapter->last_result = ESP_OK;
            } else if (seed_length >
                       adapter->config.seed_capacity) {

                adapter->state = UDS_SECURITY_ACCESS_ERROR;
                adapter->last_result = ESP_ERR_INVALID_SIZE;
            } else {
                memcpy(
                    adapter->config.seed_buffer,
                    &event->response.payload[1],
                    seed_length
                );
                adapter->seed_length = seed_length;
                adapter->state =
                    UDS_SECURITY_ACCESS_CALCULATING_KEY;
            }
        } else if ((adapter->state ==
                    UDS_SECURITY_ACCESS_SENDING_KEY) &&
                   (subfunction ==
                    (adapter->security_level + 1U))) {

            adapter->state = UDS_SECURITY_ACCESS_UNLOCKED;
            adapter->last_result = ESP_OK;
        } else {
            adapter->state = UDS_SECURITY_ACCESS_ERROR;
            adapter->last_result = ESP_ERR_INVALID_RESPONSE;
        }
    } else if ((event->type ==
                UDS_CLIENT_EVENT_NEGATIVE_RESPONSE) ||
               (event->type == UDS_CLIENT_EVENT_TIMEOUT) ||
               (event->type ==
                UDS_CLIENT_EVENT_TRANSPORT_ERROR) ||
               (event->type ==
                UDS_CLIENT_EVENT_PROTOCOL_ERROR)) {

        adapter->state = UDS_SECURITY_ACCESS_ERROR;
        adapter->last_result =
            (event->result != ESP_OK)
                ? event->result
                : ESP_FAIL;
    }

    if (adapter->client_callback != NULL) {
        adapter->client_callback(
            event,
            adapter->client_callback_context
        );
    }
}
