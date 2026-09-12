/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "uds_download.h"

#include <string.h>

static void uds_download_client_callback(
    const uds_client_event_t *event,
    void *context
);

static void uds_download_fail(
    uds_download_t *download,
    esp_err_t result
)
{
    download->state = UDS_DOWNLOAD_ERROR;
    download->action_pending = false;
    download->last_result = result;
}

esp_err_t uds_download_open(
    uds_download_t *download,
    const uds_download_config_t *config
)
{
    if ((download == NULL) ||
        (config == NULL) ||
        (config->read == NULL) ||
        (config->transfer_buffer == NULL) ||
        (config->transfer_capacity == 0U) ||
        (config->memory_size == 0U) ||
        ((config->exit_parameter_record == NULL) &&
         (config->exit_parameter_record_length != 0U)) ||
        (config->exit_parameter_record_length >
         UDS_CLIENT_TRANSFER_EXIT_MAX_LENGTH)) {

        return ESP_ERR_INVALID_ARG;
    }

    memset(
        download,
        0,
        sizeof(*download)
    );

    download->config = *config;
    download->client_callback = config->client.callback;
    download->client_callback_context =
        config->client.callback_context;
    download->config.client.callback =
        uds_download_client_callback;
    download->config.client.callback_context = download;

    const esp_err_t result =
        uds_client_open(
            &download->client,
            &download->config.client
        );

    download->state =
        (result == ESP_OK)
            ? UDS_DOWNLOAD_IDLE
            : UDS_DOWNLOAD_CLOSED;
    download->last_result = result;

    return result;
}

esp_err_t uds_download_close(
    uds_download_t *download
)
{
    if (download == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t result =
        uds_client_close(&download->client);

    if (result == ESP_OK) {
        download->state = UDS_DOWNLOAD_CLOSED;
        download->action_pending = false;
    }

    download->last_result = result;
    return result;
}

esp_err_t uds_download_start(
    uds_download_t *download,
    uint64_t now_us
)
{
    if (download == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((download->state != UDS_DOWNLOAD_IDLE) &&
        (download->state != UDS_DOWNLOAD_COMPLETE) &&
        (download->state != UDS_DOWNLOAD_CANCELLED) &&
        (download->state != UDS_DOWNLOAD_ERROR)) {

        return ESP_ERR_INVALID_STATE;
    }

    if (uds_client_busy(&download->client)) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result =
        uds_client_request_download(
            &download->client,
            download->config.data_format_identifier,
            download->config.memory_address,
            download->config.memory_address_length,
            download->config.memory_size,
            download->config.memory_size_length,
            now_us
        );

    if (result == ESP_OK) {
        download->state = UDS_DOWNLOAD_REQUESTING_DOWNLOAD;
        download->transferred_size = 0U;
        download->maximum_block_length = 0U;
        download->block_data_capacity = 0U;
        download->current_block_size = 0U;
        download->block_sequence_counter = 1U;
        download->action_pending = false;
    } else {
        uds_download_fail(download, result);
    }

    download->last_result = result;
    return result;
}

esp_err_t uds_download_poll(
    uds_download_t *download,
    uint64_t now_us
)
{
    if (download == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (download->state == UDS_DOWNLOAD_CLOSED) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result =
        uds_client_poll(
            &download->client,
            now_us
        );

    if (download->state == UDS_DOWNLOAD_CANCELLED) {
        return ESP_OK;
    }

    if (result != ESP_OK) {
        uds_download_fail(download, result);
        return result;
    }

    if (!download->action_pending) {
        return ESP_OK;
    }

    if (download->state == UDS_DOWNLOAD_TRANSFERRING) {
        const uint64_t remaining =
            download->config.memory_size -
            download->transferred_size;
        size_t requested = download->block_data_capacity;

        if (remaining < requested) {
            requested = (size_t)remaining;
        }

        size_t read_size = 0U;
        result =
            download->config.read(
                download->transferred_size,
                download->config.transfer_buffer,
                requested,
                &read_size,
                download->config.read_context
            );

        if ((result == ESP_OK) &&
            ((read_size == 0U) ||
             (read_size > requested))) {

            result = ESP_ERR_INVALID_SIZE;
        }

        if (result == ESP_OK) {
            download->current_block_size = read_size;
            download->action_pending = false;
            result =
                uds_client_transfer_data(
                    &download->client,
                    download->block_sequence_counter,
                    download->config.transfer_buffer,
                    read_size,
                    now_us
                );
        }

        if (result != ESP_OK) {
            uds_download_fail(download, result);
        }

        download->last_result = result;
        return result;
    }

    if (download->state ==
        UDS_DOWNLOAD_REQUESTING_TRANSFER_EXIT) {

        download->action_pending = false;
        result =
            uds_client_request_transfer_exit(
                &download->client,
                download->config.exit_parameter_record,
                download->config.exit_parameter_record_length,
                now_us
            );

        if (result != ESP_OK) {
            uds_download_fail(download, result);
        }

        download->last_result = result;
        return result;
    }

    uds_download_fail(download, ESP_ERR_INVALID_STATE);
    return ESP_ERR_INVALID_STATE;
}

esp_err_t uds_download_cancel(
    uds_download_t *download
)
{
    if (download == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((download->state !=
         UDS_DOWNLOAD_REQUESTING_DOWNLOAD) &&
        (download->state != UDS_DOWNLOAD_TRANSFERRING) &&
        (download->state !=
         UDS_DOWNLOAD_REQUESTING_TRANSFER_EXIT)) {

        return ESP_ERR_INVALID_STATE;
    }

    download->state = UDS_DOWNLOAD_CANCELLED;
    download->action_pending = false;
    download->last_result = ESP_OK;

    return ESP_OK;
}

esp_err_t uds_download_get_progress(
    const uds_download_t *download,
    uds_download_progress_t *progress
)
{
    if ((download == NULL) || (progress == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    *progress = (uds_download_progress_t) {
        .state = download->state,
        .transferred_size = download->transferred_size,
        .total_size = download->config.memory_size,
        .maximum_block_length = download->maximum_block_length,
        .block_data_capacity = download->block_data_capacity,
        .block_sequence_counter =
            download->block_sequence_counter,
        .last_result = download->last_result,
    };

    return ESP_OK;
}

uds_client_t *uds_download_client(
    uds_download_t *download
)
{
    return (download != NULL)
        ? &download->client
        : NULL;
}

esp_err_t uds_download_memory_read(
    uint64_t offset,
    uint8_t *buffer,
    size_t capacity,
    size_t *read_size,
    void *context
)
{
    const uds_download_memory_source_t *source = context;

    if ((buffer == NULL) ||
        (read_size == NULL) ||
        (source == NULL) ||
        (source->data == NULL) ||
        (offset > source->size)) {

        return ESP_ERR_INVALID_ARG;
    }

    size_t available = source->size - (size_t)offset;

    if (available > capacity) {
        available = capacity;
    }

    if (available != 0U) {
        memcpy(
            buffer,
            &source->data[(size_t)offset],
            available
        );
    }

    *read_size = available;
    return ESP_OK;
}

static void uds_download_client_callback(
    const uds_client_event_t *event,
    void *context
)
{
    uds_download_t *download = context;

    if ((event == NULL) || (download == NULL)) {
        return;
    }

    if ((event->type == UDS_CLIENT_EVENT_RESPONSE) &&
        (download->state ==
         UDS_DOWNLOAD_REQUESTING_DOWNLOAD)) {

        uds_request_download_response_t response = {0};
        const esp_err_t result =
            uds_protocol_decode_request_download_response(
                &event->response,
                &response
            );

        if (result != ESP_OK) {
            uds_download_fail(download, result);
        } else {
            uint64_t block_data_capacity =
                response.maximum_block_length - 2U;

            if (block_data_capacity >
                UDS_CLIENT_TRANSFER_DATA_MAX_LENGTH) {

                block_data_capacity =
                    UDS_CLIENT_TRANSFER_DATA_MAX_LENGTH;
            }

            if (block_data_capacity >
                download->config.transfer_capacity) {

                block_data_capacity =
                    download->config.transfer_capacity;
            }

            if (block_data_capacity == 0U) {
                uds_download_fail(
                    download,
                    ESP_ERR_INVALID_SIZE
                );
            } else {
                download->maximum_block_length =
                    response.maximum_block_length;
                download->block_data_capacity =
                    (size_t)block_data_capacity;
                download->state = UDS_DOWNLOAD_TRANSFERRING;
                download->action_pending = true;
                download->last_result = ESP_OK;
            }
        }
    } else if ((event->type == UDS_CLIENT_EVENT_RESPONSE) &&
               (download->state ==
                UDS_DOWNLOAD_TRANSFERRING)) {

        uds_transfer_data_response_t response = {0};
        esp_err_t result =
            uds_protocol_decode_transfer_data_response(
                &event->response,
                &response
            );

        if ((result == ESP_OK) &&
            (response.block_sequence_counter !=
             download->block_sequence_counter)) {

            result = ESP_ERR_INVALID_RESPONSE;
        }

        if (result != ESP_OK) {
            uds_download_fail(download, result);
        } else {
            download->transferred_size +=
                download->current_block_size;
            download->current_block_size = 0U;

            if (download->transferred_size ==
                download->config.memory_size) {

                download->state =
                    UDS_DOWNLOAD_REQUESTING_TRANSFER_EXIT;
            } else {
                download->block_sequence_counter++;
            }

            download->action_pending = true;
            download->last_result = ESP_OK;
        }
    } else if ((event->type == UDS_CLIENT_EVENT_RESPONSE) &&
               (download->state ==
                UDS_DOWNLOAD_REQUESTING_TRANSFER_EXIT)) {

        uds_transfer_exit_response_t response = {0};
        const esp_err_t result =
            uds_protocol_decode_transfer_exit_response(
                &event->response,
                &response
            );

        if (result == ESP_OK) {
            download->state = UDS_DOWNLOAD_COMPLETE;
            download->action_pending = false;
            download->last_result = ESP_OK;
        } else {
            uds_download_fail(download, result);
        }
    } else if ((download->state != UDS_DOWNLOAD_CANCELLED) &&
               ((event->type ==
                 UDS_CLIENT_EVENT_NEGATIVE_RESPONSE) ||
                (event->type == UDS_CLIENT_EVENT_TIMEOUT) ||
                (event->type ==
                 UDS_CLIENT_EVENT_TRANSPORT_ERROR) ||
                (event->type ==
                 UDS_CLIENT_EVENT_PROTOCOL_ERROR))) {

        uds_download_fail(
            download,
            (event->result != ESP_OK)
                ? event->result
                : ESP_FAIL
        );
    }

    if (download->client_callback != NULL) {
        download->client_callback(
            event,
            download->client_callback_context
        );
    }
}
