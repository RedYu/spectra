/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "uds_client.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UDS_DOWNLOAD_DEFAULT_MAXIMUM_BLOCK_RETRIES (3U)
#define UDS_DOWNLOAD_DEFAULT_OPERATION_TIMEOUT_US  (600000000ULL)

typedef enum
{
    UDS_DOWNLOAD_CLOSED = 0,
    UDS_DOWNLOAD_IDLE,
    UDS_DOWNLOAD_REQUESTING_DOWNLOAD,
    UDS_DOWNLOAD_TRANSFERRING,
    UDS_DOWNLOAD_REQUESTING_TRANSFER_EXIT,
    UDS_DOWNLOAD_COMPLETE,
    UDS_DOWNLOAD_CANCELLED,
    UDS_DOWNLOAD_ERROR,

} uds_download_state_t;

typedef esp_err_t (*uds_download_read_cb_t)(
    uint64_t offset,
    uint8_t *buffer,
    size_t capacity,
    size_t *read_size,
    void *context
);

typedef struct
{
    uds_client_config_t client;
    uds_download_read_cb_t read;
    void *read_context;
    uint8_t *transfer_buffer;
    size_t transfer_capacity;
    uint8_t data_format_identifier;
    uint64_t memory_address;
    uint8_t memory_address_length;
    uint64_t memory_size;
    uint8_t memory_size_length;
    const uint8_t *exit_parameter_record;
    size_t exit_parameter_record_length;
    uint8_t maximum_block_retries;
    uint64_t operation_timeout_us;

} uds_download_config_t;

typedef struct
{
    uds_download_state_t state;
    uint64_t transferred_size;
    uint64_t total_size;
    uint64_t maximum_block_length;
    size_t block_data_capacity;
    uint8_t block_sequence_counter;
    uint32_t acknowledged_blocks;
    uint32_t retry_count;
    uint8_t current_block_retry;
    uint8_t last_negative_response_code;
    esp_err_t last_result;

} uds_download_progress_t;

typedef struct
{
    uds_download_config_t config;
    uds_client_t client;
    uds_client_event_cb_t client_callback;
    void *client_callback_context;
    uds_download_state_t state;
    uint64_t transferred_size;
    uint64_t maximum_block_length;
    size_t block_data_capacity;
    size_t current_block_size;
    uint8_t block_sequence_counter;
    uint8_t maximum_block_retries;
    uint8_t current_block_retry;
    uint8_t last_negative_response_code;
    uint32_t acknowledged_blocks;
    uint32_t retry_count;
    uint64_t started_at_us;
    uint64_t operation_timeout_us;
    bool action_pending;
    bool retry_pending;
    esp_err_t last_result;

} uds_download_t;

typedef struct
{
    const uint8_t *data;
    size_t size;

} uds_download_memory_source_t;

esp_err_t uds_download_open(
    uds_download_t *download,
    const uds_download_config_t *config
);

esp_err_t uds_download_close(
    uds_download_t *download
);

esp_err_t uds_download_start(
    uds_download_t *download,
    uint64_t now_us
);

esp_err_t uds_download_poll(
    uds_download_t *download,
    uint64_t now_us
);

esp_err_t uds_download_cancel(
    uds_download_t *download
);

esp_err_t uds_download_get_progress(
    const uds_download_t *download,
    uds_download_progress_t *progress
);

uds_client_t *uds_download_client(
    uds_download_t *download
);

esp_err_t uds_download_memory_read(
    uint64_t offset,
    uint8_t *buffer,
    size_t capacity,
    size_t *read_size,
    void *context
);

#ifdef __cplusplus
}
#endif
