/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "uds_download.h"

#include <string.h>

#include "esp_timer.h"

static void uds_download_client_callback(
    const uds_client_event_t *event,
    void *context
);

static void uds_download_schedule_after_session(
    uds_download_t *download
);

static void uds_download_schedule_after_security(
    uds_download_t *download
);

static void uds_download_schedule_after_transfer(
    uds_download_t *download
);

static void uds_download_schedule_completion(
    uds_download_t *download
);

static esp_err_t uds_download_select_segment(
    uds_download_t *download,
    uint32_t index
);

static esp_err_t uds_download_validate_segments(
    uds_download_t *download
);

static esp_err_t uds_download_select_segment(
    uds_download_t *download,
    uint32_t index
)
{
    uint64_t address = download->config.memory_address;
    uint64_t size = download->config.memory_size;
    esp_err_t result = ESP_OK;

    if (download->config.segment_count != 0U) {
        result = download->config.segment(
            index,
            &address,
            &size,
            download->config.segment_context
        );
    }

    if ((result != ESP_OK) || (size == 0U)) {
        return (result != ESP_OK)
            ? result
            : ESP_ERR_INVALID_SIZE;
    }

    download->segment_index = index;
    download->segment_memory_address = address;
    download->segment_memory_size = size;
    download->segment_transferred_size = 0U;
    download->block_sequence_counter = 1U;
    download->maximum_block_length = 0U;
    download->block_data_capacity = 0U;
    download->current_block_size = 0U;
    download->current_block_retry = 0U;
    return ESP_OK;
}

static esp_err_t uds_download_validate_segments(
    uds_download_t *download
)
{
    download->segment_count =
        (download->config.segment_count != 0U)
            ? download->config.segment_count
            : 1U;

    uint64_t total_size = 0U;
    uint64_t previous_end = 0U;

    for (uint32_t index = 0U;
         index < download->segment_count;
         ++index) {

        uint64_t address = download->config.memory_address;
        uint64_t size = download->config.memory_size;
        esp_err_t result = ESP_OK;

        if (download->config.segment_count != 0U) {
            result = download->config.segment(
                index,
                &address,
                &size,
                download->config.segment_context
            );
        }

        if ((result != ESP_OK) ||
            (size == 0U) ||
            (address > (UINT64_MAX - size)) ||
            ((index != 0U) && (address < previous_end)) ||
            (total_size > (UINT64_MAX - size))) {

            return (result != ESP_OK)
                ? result
                : ESP_ERR_INVALID_ARG;
        }

        total_size += size;
        previous_end = address + size;
    }

    if (total_size != download->config.memory_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    return uds_download_select_segment(download, 0U);
}

static esp_err_t uds_download_submit_action(
    uds_download_t *download,
    uint64_t now_us
);

static bool uds_download_routine_response_valid(
    const uds_client_event_t *event,
    uint8_t control_type,
    uint16_t routine_identifier
)
{
    return (event->response.payload_length >= 3U) &&
           ((event->response.payload[0] & 0x7FU) ==
            control_type) &&
           (event->response.payload[1] ==
            (uint8_t)(routine_identifier >> 8U)) &&
           (event->response.payload[2] ==
            (uint8_t)routine_identifier);
}

static void uds_download_fail(
    uds_download_t *download,
    esp_err_t result
)
{
    if ((download->state ==
         UDS_DOWNLOAD_RESTORING_DEFAULT_SESSION) &&
        download->restoring_after_error) {

        download->state = UDS_DOWNLOAD_ERROR;
        download->action_pending = false;
        download->retry_pending = false;
        download->last_result = download->operation_result;
        download->last_negative_response_code =
            download->operation_negative_response_code;
        return;
    }

    download->operation_result = result;
    download->operation_negative_response_code =
        download->last_negative_response_code;
    download->action_pending = false;
    download->retry_pending = false;
    download->last_result = result;

    if (download->config.restore_default_session &&
        !download->default_session_restored &&
        (download->state !=
         UDS_DOWNLOAD_RESTORING_DEFAULT_SESSION) &&
        (download->client.state != UDS_CLIENT_CLOSED)) {

        download->state =
            UDS_DOWNLOAD_RESTORING_DEFAULT_SESSION;
        download->restoring_after_error = true;
        download->action_pending = true;
    } else {
        download->state = UDS_DOWNLOAD_ERROR;
    }
}

static void uds_download_schedule_after_session(
    uds_download_t *download
)
{
    uds_download_schedule_after_security(download);
}

static void uds_download_schedule_after_security(
    uds_download_t *download
)
{
    if (download->security_resume_state != UDS_DOWNLOAD_CLOSED) {
        download->state = download->security_resume_state;
        download->security_resume_state = UDS_DOWNLOAD_CLOSED;
    } else {
        download->state =
            (download->config.erase_routine_identifier != 0U)
                ? UDS_DOWNLOAD_ERASING_MEMORY
                : UDS_DOWNLOAD_REQUESTING_DOWNLOAD;
    }
    download->action_pending = true;
}

static void uds_download_schedule_after_transfer(
    uds_download_t *download
)
{
    if (download->config.verify_routine_identifier != 0U) {
        download->state = UDS_DOWNLOAD_VERIFYING_MEMORY;
        download->action_pending = true;
    } else {
        uds_download_schedule_completion(download);
    }
}

static void uds_download_schedule_completion(
    uds_download_t *download
)
{
    if (download->config.reset_type != 0U) {
        download->state = UDS_DOWNLOAD_RESETTING_ECU;
        download->action_pending = true;
    } else if (download->config.restore_default_session) {
        download->state =
            UDS_DOWNLOAD_RESTORING_DEFAULT_SESSION;
        download->action_pending = true;
    } else {
        download->state = UDS_DOWNLOAD_COMPLETE;
        download->action_pending = false;
        download->last_result = ESP_OK;
    }
}

static bool uds_download_retry_block(
    uds_download_t *download,
    uint8_t negative_response_code
)
{
    if ((download->state != UDS_DOWNLOAD_TRANSFERRING) ||
        (download->current_block_size == 0U) ||
        (download->current_block_retry >=
         download->maximum_block_retries)) {

        return false;
    }

    download->current_block_retry++;
    download->retry_count++;
    download->last_negative_response_code =
        negative_response_code;
    download->last_nrc_action =
        (negative_response_code == UDS_NRC_BUSY_REPEAT_REQUEST)
            ? UDS_DOWNLOAD_NRC_WAIT_AND_RETRY
            : UDS_DOWNLOAD_NRC_RETRY;
    download->retry_pending = true;
    download->action_pending = true;
    download->last_result = ESP_OK;

    return true;
}

uds_download_nrc_action_t uds_download_classify_nrc(
    uds_download_state_t state,
    uint8_t negative_response_code
)
{
    if (negative_response_code == UDS_NRC_BUSY_REPEAT_REQUEST) {
        return UDS_DOWNLOAD_NRC_WAIT_AND_RETRY;
    }

    if ((state == UDS_DOWNLOAD_TRANSFERRING) &&
        (negative_response_code ==
         UDS_NRC_WRONG_BLOCK_SEQUENCE_COUNTER)) {

        return UDS_DOWNLOAD_NRC_RETRY;
    }

    if (((state == UDS_DOWNLOAD_REQUESTING_SECURITY_SEED) ||
         (state == UDS_DOWNLOAD_SENDING_SECURITY_KEY)) &&
        (negative_response_code ==
         UDS_NRC_REQUIRED_TIME_DELAY_NOT_EXPIRED)) {

        return UDS_DOWNLOAD_NRC_WAIT_AND_RETRY;
    }

    if (((state == UDS_DOWNLOAD_ERASING_MEMORY) ||
         (state == UDS_DOWNLOAD_REQUESTING_DOWNLOAD) ||
         (state == UDS_DOWNLOAD_VERIFYING_MEMORY)) &&
        (negative_response_code == UDS_NRC_SECURITY_ACCESS_DENIED)) {

        return UDS_DOWNLOAD_NRC_RESTART_SECURITY;
    }

    return UDS_DOWNLOAD_NRC_FAIL;
}

static bool uds_download_retry_action(
    uds_download_t *download,
    uint8_t negative_response_code,
    uint64_t delay_us
)
{
    if (download->state == UDS_DOWNLOAD_TRANSFERRING) {
        return uds_download_retry_block(
            download,
            negative_response_code
        );
    }

    if (download->current_action_retry >=
        download->maximum_block_retries) {

        return false;
    }

    download->current_action_retry++;
    download->retry_count++;
    download->last_negative_response_code =
        negative_response_code;
    download->retry_pending = true;
    download->action_pending = true;
    download->action_due_us =
        (uint64_t)esp_timer_get_time() + delay_us;
    download->last_result = ESP_OK;
    return true;
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
        (((config->segment_count == 0U) &&
          (config->segment != NULL)) ||
         ((config->segment_count != 0U) &&
          (config->segment == NULL))) ||
        ((config->exit_parameter_record == NULL) &&
         (config->exit_parameter_record_length != 0U)) ||
        (config->exit_parameter_record_length >
         UDS_CLIENT_TRANSFER_EXIT_MAX_LENGTH) ||
        ((config->security_level != 0U) &&
         ((config->security_level & 1U) == 0U)) ||
        ((config->security_level != 0U) &&
         ((config->security_algorithm == NULL) ||
          (config->security_seed_buffer == NULL) ||
          (config->security_seed_capacity == 0U) ||
          (config->security_key_buffer == NULL) ||
          (config->security_key_capacity == 0U))) ||
        ((config->erase_option_record == NULL) &&
         (config->erase_option_record_length != 0U)) ||
        (config->erase_option_record_length >
         UDS_DOWNLOAD_ROUTINE_RECORD_MAX_LENGTH) ||
        ((config->verify_option_record == NULL) &&
         (config->verify_option_record_length != 0U)) ||
        (config->verify_option_record_length >
         UDS_DOWNLOAD_ROUTINE_RECORD_MAX_LENGTH)) {

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
    download->maximum_block_retries =
        (config->maximum_block_retries != 0U)
            ? config->maximum_block_retries
            : UDS_DOWNLOAD_DEFAULT_MAXIMUM_BLOCK_RETRIES;
    download->operation_timeout_us =
        (config->operation_timeout_us != 0U)
            ? config->operation_timeout_us
            : UDS_DOWNLOAD_DEFAULT_OPERATION_TIMEOUT_US;
    download->routine_poll_interval_us =
        (config->routine_poll_interval_us != 0U)
            ? config->routine_poll_interval_us
            : UDS_DOWNLOAD_DEFAULT_ROUTINE_POLL_INTERVAL_US;
    download->maximum_routine_polls =
        (config->maximum_routine_polls != 0U)
            ? config->maximum_routine_polls
            : UDS_DOWNLOAD_DEFAULT_MAXIMUM_ROUTINE_POLLS;

    esp_err_t result = uds_download_validate_segments(download);

    if (result != ESP_OK) {
        download->state = UDS_DOWNLOAD_CLOSED;
        download->last_result = result;
        return result;
    }

    result =
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

    download->state =
        (download->config.programming_session_type != 0U)
            ? UDS_DOWNLOAD_ENTERING_SESSION
            : (download->config.erase_routine_identifier != 0U)
                ? UDS_DOWNLOAD_ERASING_MEMORY
                : UDS_DOWNLOAD_REQUESTING_DOWNLOAD;
    download->action_pending = true;
    download->security_unlocked = false;
    download->default_session_restored = false;
    download->restoring_after_error = false;
    download->security_seed_length = 0U;
    download->security_resume_state = UDS_DOWNLOAD_CLOSED;
    download->routine_result_polling = false;
    download->routine_poll_count = 0U;
    download->action_due_us = 0U;
    download->operation_result = ESP_OK;
    download->operation_negative_response_code = 0U;

    esp_err_t result = uds_download_select_segment(
        download,
        0U
    );

    if (result != ESP_OK) {
        uds_download_fail(download, result);
        download->last_result = result;
        return result;
    }

    result =
        uds_download_submit_action(download, now_us);

    if (result == ESP_OK) {
        download->transferred_size = 0U;
        download->maximum_block_length = 0U;
        download->block_data_capacity = 0U;
        download->current_block_size = 0U;
        download->block_sequence_counter = 1U;
        download->current_block_retry = 0U;
        download->current_action_retry = 0U;
        download->last_negative_response_code = 0U;
        download->last_nrc_action = UDS_DOWNLOAD_NRC_FAIL;
        download->acknowledged_blocks = 0U;
        download->retry_count = 0U;
        download->started_at_us = now_us;
        download->retry_pending = false;
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

    if ((download->state != UDS_DOWNLOAD_IDLE) &&
        (download->state != UDS_DOWNLOAD_COMPLETE) &&
        (download->state != UDS_DOWNLOAD_CANCELLED) &&
        (download->state != UDS_DOWNLOAD_ERROR) &&
        ((now_us - download->started_at_us) >=
         download->operation_timeout_us)) {

        uds_download_fail(download, ESP_ERR_TIMEOUT);
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result =
        uds_client_poll(
            &download->client,
            now_us
        );

    if (download->state == UDS_DOWNLOAD_CANCELLED) {
        return ESP_OK;
    }

    if ((result != ESP_OK) && !download->retry_pending) {
        uds_download_fail(download, result);
        return result;
    }

    if (download->retry_pending) {
        result = ESP_OK;
    }

    if (download->state ==
        UDS_DOWNLOAD_CALCULATING_SECURITY_KEY) {

        size_t key_length = 0U;
        result =
            download->config.security_algorithm(
                download->config.security_level,
                download->config.security_seed_buffer,
                download->security_seed_length,
                download->config.security_key_buffer,
                download->config.security_key_capacity,
                &key_length,
                download->config.security_algorithm_context
            );

        if ((result == ESP_OK) &&
            ((key_length == 0U) ||
             (key_length > download->config.security_key_capacity) ||
             (key_length > UDS_CLIENT_SECURITY_DATA_MAX_LENGTH))) {

            result = ESP_ERR_INVALID_SIZE;
        }

        if (result == ESP_OK) {
            download->security_key_length = key_length;
            result =
                uds_client_security_access_send_key(
                    &download->client,
                    download->config.security_level,
                    download->config.security_key_buffer,
                    key_length,
                    now_us
                );
        }

        if (result == ESP_OK) {
            download->state = UDS_DOWNLOAD_SENDING_SECURITY_KEY;
        } else {
            uds_download_fail(download, result);
        }

        download->last_result = result;
        return result;
    }

    if (!download->action_pending) {
        return ESP_OK;
    }

    if (now_us < download->action_due_us) {
        return ESP_OK;
    }

    if (download->state == UDS_DOWNLOAD_TRANSFERRING) {
        if (!download->retry_pending) {
            const uint64_t remaining =
                download->segment_memory_size -
                download->segment_transferred_size;
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
            }
        }

        download->action_pending = false;
        download->retry_pending = false;

        if (result == ESP_OK) {
            result =
                uds_client_transfer_data(
                    &download->client,
                    download->block_sequence_counter,
                    download->config.transfer_buffer,
                    download->current_block_size,
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

    download->retry_pending = false;
    return uds_download_submit_action(download, now_us);
}

static esp_err_t uds_download_submit_action(
    uds_download_t *download,
    uint64_t now_us
)
{
    download->action_pending = false;
    esp_err_t result = ESP_ERR_INVALID_STATE;

    switch (download->state) {
        case UDS_DOWNLOAD_ENTERING_SESSION:
            result =
                uds_client_diagnostic_session_control(
                    &download->client,
                    download->config.programming_session_type,
                    false,
                    now_us
                );
            break;

        case UDS_DOWNLOAD_REQUESTING_SECURITY_SEED:
            result =
                uds_client_security_access_request_seed(
                    &download->client,
                    download->config.security_level,
                    NULL,
                    0U,
                    now_us
                );
            break;

        case UDS_DOWNLOAD_SENDING_SECURITY_KEY:
            result =
                uds_client_security_access_send_key(
                    &download->client,
                    download->config.security_level,
                    download->config.security_key_buffer,
                    download->security_key_length,
                    now_us
                );
            break;

        case UDS_DOWNLOAD_ERASING_MEMORY:
            result =
                uds_client_routine_control(
                    &download->client,
                    download->routine_result_polling
                        ? UDS_ROUTINE_CONTROL_REQUEST_RESULTS
                        : UDS_ROUTINE_CONTROL_START,
                    download->config.erase_routine_identifier,
                    download->routine_result_polling
                        ? NULL
                        : download->config.erase_option_record,
                    download->routine_result_polling
                        ? 0U
                        : download->config.erase_option_record_length,
                    false,
                    now_us
                );
            break;

        case UDS_DOWNLOAD_REQUESTING_DOWNLOAD:
            result =
                uds_client_request_download(
                    &download->client,
                    download->config.data_format_identifier,
                    download->segment_memory_address,
                    download->config.memory_address_length,
                    download->segment_memory_size,
                    download->config.memory_size_length,
                    now_us
                );
            break;

        case UDS_DOWNLOAD_VERIFYING_MEMORY:
            result =
                uds_client_routine_control(
                    &download->client,
                    download->routine_result_polling
                        ? UDS_ROUTINE_CONTROL_REQUEST_RESULTS
                        : UDS_ROUTINE_CONTROL_START,
                    download->config.verify_routine_identifier,
                    download->routine_result_polling
                        ? NULL
                        : download->config.verify_option_record,
                    download->routine_result_polling
                        ? 0U
                        : download->config.verify_option_record_length,
                    false,
                    now_us
                );
            break;

        case UDS_DOWNLOAD_RESETTING_ECU:
            result =
                uds_client_ecu_reset(
                    &download->client,
                    download->config.reset_type,
                    false,
                    now_us
                );
            break;

        case UDS_DOWNLOAD_RESTORING_DEFAULT_SESSION:
            result =
                uds_client_diagnostic_session_control(
                    &download->client,
                    UDS_DIAGNOSTIC_SESSION_DEFAULT,
                    false,
                    now_us
                );
            break;

        default:
            break;
    }

    if (result != ESP_OK) {
        uds_download_fail(download, result);
    }

    download->last_result = result;
    return result;
}

esp_err_t uds_download_cancel(
    uds_download_t *download
)
{
    if (download == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((download->state == UDS_DOWNLOAD_CLOSED) ||
        (download->state == UDS_DOWNLOAD_IDLE) ||
        (download->state == UDS_DOWNLOAD_COMPLETE) ||
        (download->state == UDS_DOWNLOAD_CANCELLED) ||
        (download->state == UDS_DOWNLOAD_ERROR)) {

        return ESP_ERR_INVALID_STATE;
    }

    download->state = UDS_DOWNLOAD_CANCELLED;
    download->action_pending = false;
    download->retry_pending = false;
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
        .acknowledged_blocks = download->acknowledged_blocks,
        .retry_count = download->retry_count,
        .current_block_retry = download->current_block_retry,
        .current_action_retry = download->current_action_retry,
        .last_negative_response_code =
            download->last_negative_response_code,
        .last_nrc_action = download->last_nrc_action,
        .last_result = download->last_result,
        .security_unlocked = download->security_unlocked,
        .default_session_restored =
            download->default_session_restored,
        .routine_poll_count = download->routine_poll_count,
        .segment_index = download->segment_index,
        .segment_count = download->segment_count,
        .segment_transferred_size =
            download->segment_transferred_size,
        .segment_total_size = download->segment_memory_size,
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

    if (event->type == UDS_CLIENT_EVENT_RESPONSE) {
        download->last_negative_response_code = 0U;
        download->current_action_retry = 0U;
        download->last_nrc_action = UDS_DOWNLOAD_NRC_FAIL;
    }

    if ((event->type == UDS_CLIENT_EVENT_RESPONSE) &&
        (download->state == UDS_DOWNLOAD_ENTERING_SESSION)) {

        if ((event->response.service_id ==
             (UDS_SERVICE_DIAGNOSTIC_SESSION_CONTROL +
              UDS_POSITIVE_RESPONSE_OFFSET)) &&
            (event->response.payload_length >= 1U) &&
            ((event->response.payload[0] & 0x7FU) ==
             download->config.programming_session_type)) {

            uds_download_schedule_after_session(download);
        } else {
            uds_download_fail(
                download,
                ESP_ERR_INVALID_RESPONSE
            );
        }
    } else if ((event->type == UDS_CLIENT_EVENT_RESPONSE) &&
               (download->state ==
                UDS_DOWNLOAD_REQUESTING_SECURITY_SEED)) {

        const bool valid =
            (event->response.service_id ==
             (UDS_SERVICE_SECURITY_ACCESS +
              UDS_POSITIVE_RESPONSE_OFFSET)) &&
            (event->response.payload_length >= 1U) &&
            (event->response.payload[0] ==
             download->config.security_level);

        if (!valid) {
            uds_download_fail(
                download,
                ESP_ERR_INVALID_RESPONSE
            );
        } else {
            const size_t seed_length =
                event->response.payload_length - 1U;

            if (seed_length == 0U) {
                download->security_unlocked = true;
                uds_download_schedule_after_security(download);
            } else if (seed_length >
                       download->config.security_seed_capacity) {

                uds_download_fail(
                    download,
                    ESP_ERR_INVALID_SIZE
                );
            } else {
                memcpy(
                    download->config.security_seed_buffer,
                    &event->response.payload[1],
                    seed_length
                );
                download->security_seed_length = seed_length;
                download->state =
                    UDS_DOWNLOAD_CALCULATING_SECURITY_KEY;
            }
        }
    } else if ((event->type == UDS_CLIENT_EVENT_RESPONSE) &&
               (download->state ==
                UDS_DOWNLOAD_SENDING_SECURITY_KEY)) {

        const bool valid =
            (event->response.service_id ==
             (UDS_SERVICE_SECURITY_ACCESS +
              UDS_POSITIVE_RESPONSE_OFFSET)) &&
            (event->response.payload_length >= 1U) &&
            (event->response.payload[0] ==
             (uint8_t)(download->config.security_level + 1U));

        if (valid) {
            download->security_unlocked = true;
            uds_download_schedule_after_security(download);
        } else {
            uds_download_fail(
                download,
                ESP_ERR_INVALID_RESPONSE
            );
        }
    } else if ((event->type == UDS_CLIENT_EVENT_RESPONSE) &&
               (download->state ==
                UDS_DOWNLOAD_ERASING_MEMORY)) {

        const uint8_t control_type =
            download->routine_result_polling
                ? UDS_ROUTINE_CONTROL_REQUEST_RESULTS
                : UDS_ROUTINE_CONTROL_START;

        if (!uds_download_routine_response_valid(
                event,
                control_type,
                download->config.erase_routine_identifier
            )) {

            uds_download_fail(
                download,
                ESP_ERR_INVALID_RESPONSE
            );
        } else if (!download->routine_result_polling) {
            download->routine_result_polling = true;
            download->routine_poll_count = 0U;
            download->action_due_us =
                (uint64_t)esp_timer_get_time() +
                download->routine_poll_interval_us;
            download->action_pending = true;
        } else {
            download->routine_poll_count++;
            const uds_download_routine_result_t result =
                (download->config.erase_result != NULL)
                    ? download->config.erase_result(
                        download->config.erase_routine_identifier,
                        &event->response.payload[3],
                        event->response.payload_length - 3U,
                        download->config.erase_result_context
                    )
                    : UDS_DOWNLOAD_ROUTINE_RESULT_COMPLETE;

            if (result == UDS_DOWNLOAD_ROUTINE_RESULT_COMPLETE) {
                download->routine_result_polling = false;
                download->action_due_us = 0U;
                download->state =
                    UDS_DOWNLOAD_REQUESTING_DOWNLOAD;
                download->action_pending = true;
            } else if ((result ==
                        UDS_DOWNLOAD_ROUTINE_RESULT_PENDING) &&
                       (download->routine_poll_count <
                        download->maximum_routine_polls)) {

                download->action_due_us =
                    (uint64_t)esp_timer_get_time() +
                    download->routine_poll_interval_us;
                download->action_pending = true;
            } else {
                uds_download_fail(
                    download,
                    (result == UDS_DOWNLOAD_ROUTINE_RESULT_PENDING)
                        ? ESP_ERR_TIMEOUT
                        : ESP_FAIL
                );
            }
        }
    } else if ((event->type == UDS_CLIENT_EVENT_RESPONSE) &&
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
            download->segment_transferred_size +=
                download->current_block_size;
            download->current_block_size = 0U;
            download->current_block_retry = 0U;
            download->last_negative_response_code = 0U;
            download->acknowledged_blocks++;

            if (download->segment_transferred_size ==
                download->segment_memory_size) {

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
            if ((download->segment_index + 1U) <
                download->segment_count) {

                const esp_err_t segment_result =
                    uds_download_select_segment(
                        download,
                        download->segment_index + 1U
                    );

                if (segment_result == ESP_OK) {
                    download->state =
                        UDS_DOWNLOAD_REQUESTING_DOWNLOAD;
                    download->action_pending = true;
                } else {
                    uds_download_fail(
                        download,
                        segment_result
                    );
                }
            } else {
                uds_download_schedule_after_transfer(download);
            }
        } else {
            uds_download_fail(download, result);
        }
    } else if ((event->type == UDS_CLIENT_EVENT_RESPONSE) &&
               (download->state ==
                UDS_DOWNLOAD_VERIFYING_MEMORY)) {

        const uint8_t control_type =
            download->routine_result_polling
                ? UDS_ROUTINE_CONTROL_REQUEST_RESULTS
                : UDS_ROUTINE_CONTROL_START;

        if (!uds_download_routine_response_valid(
                event,
                control_type,
                download->config.verify_routine_identifier
            )) {

            uds_download_fail(
                download,
                ESP_ERR_INVALID_RESPONSE
            );
        } else if (!download->routine_result_polling) {
            download->routine_result_polling = true;
            download->routine_poll_count = 0U;
            download->action_due_us =
                (uint64_t)esp_timer_get_time() +
                download->routine_poll_interval_us;
            download->action_pending = true;
        } else {
            download->routine_poll_count++;
            const uds_download_routine_result_t result =
                (download->config.verify_result != NULL)
                    ? download->config.verify_result(
                        download->config.verify_routine_identifier,
                        &event->response.payload[3],
                        event->response.payload_length - 3U,
                        download->config.verify_result_context
                    )
                    : UDS_DOWNLOAD_ROUTINE_RESULT_COMPLETE;

            if (result == UDS_DOWNLOAD_ROUTINE_RESULT_COMPLETE) {
                download->routine_result_polling = false;
                download->action_due_us = 0U;
                uds_download_schedule_completion(download);
            } else if ((result ==
                        UDS_DOWNLOAD_ROUTINE_RESULT_PENDING) &&
                       (download->routine_poll_count <
                        download->maximum_routine_polls)) {

                download->action_due_us =
                    (uint64_t)esp_timer_get_time() +
                    download->routine_poll_interval_us;
                download->action_pending = true;
            } else {
                uds_download_fail(
                    download,
                    (result == UDS_DOWNLOAD_ROUTINE_RESULT_PENDING)
                        ? ESP_ERR_TIMEOUT
                        : ESP_FAIL
                );
            }
        }
    } else if ((event->type == UDS_CLIENT_EVENT_RESPONSE) &&
               (download->state ==
                UDS_DOWNLOAD_RESETTING_ECU)) {

        if ((event->response.payload_length >= 1U) &&
            ((event->response.payload[0] & 0x7FU) ==
             download->config.reset_type)) {

            download->default_session_restored = true;
            download->state = UDS_DOWNLOAD_COMPLETE;
            download->action_pending = false;
            download->last_result = ESP_OK;
        } else {
            uds_download_fail(
                download,
                ESP_ERR_INVALID_RESPONSE
            );
        }
    } else if ((event->type == UDS_CLIENT_EVENT_RESPONSE) &&
               (download->state ==
                UDS_DOWNLOAD_RESTORING_DEFAULT_SESSION)) {

        const bool valid =
            (event->response.payload_length >= 1U) &&
            ((event->response.payload[0] & 0x7FU) ==
             UDS_DIAGNOSTIC_SESSION_DEFAULT);

        if (!valid) {
            uds_download_fail(
                download,
                ESP_ERR_INVALID_RESPONSE
            );
        } else {
            download->default_session_restored = true;
            download->action_pending = false;

            if (download->restoring_after_error) {
                download->state = UDS_DOWNLOAD_ERROR;
                download->last_result = download->operation_result;
                download->last_negative_response_code =
                    download->operation_negative_response_code;
            } else {
                download->state = UDS_DOWNLOAD_COMPLETE;
                download->last_result = ESP_OK;
            }
        }
    } else if ((event->type == UDS_CLIENT_EVENT_TIMEOUT) &&
               uds_download_retry_block(download, 0U)) {

        /* Retry the same data and block counter. */
    } else if ((event->type == UDS_CLIENT_EVENT_NEGATIVE_RESPONSE) &&
               ((event->response.negative_response_code ==
                 UDS_NRC_WRONG_BLOCK_SEQUENCE_COUNTER) ||
                (event->response.negative_response_code ==
                 UDS_NRC_BUSY_REPEAT_REQUEST)) &&
               uds_download_retry_block(
                   download,
                   event->response.negative_response_code
               )) {

        /* Retry only explicitly recoverable TransferData failures. */
    } else if ((event->type == UDS_CLIENT_EVENT_NEGATIVE_RESPONSE) &&
               (event->response.negative_response_code ==
                UDS_NRC_BUSY_REPEAT_REQUEST) &&
               download->routine_result_polling &&
               ((download->state == UDS_DOWNLOAD_ERASING_MEMORY) ||
                (download->state ==
                 UDS_DOWNLOAD_VERIFYING_MEMORY))) {

        download->routine_poll_count++;
        download->last_negative_response_code =
            event->response.negative_response_code;

        if (download->routine_poll_count <
            download->maximum_routine_polls) {

            download->action_due_us =
                (uint64_t)esp_timer_get_time() +
                download->routine_poll_interval_us;
            download->action_pending = true;
        } else {
            uds_download_fail(
                download,
                ESP_ERR_TIMEOUT
            );
        }
    } else if (event->type == UDS_CLIENT_EVENT_NEGATIVE_RESPONSE) {
        const uint8_t nrc =
            event->response.negative_response_code;
        const uds_download_nrc_action_t action =
            uds_download_classify_nrc(
                download->state,
                nrc
            );

        download->last_nrc_action = action;

        if (((action == UDS_DOWNLOAD_NRC_RETRY) ||
             (action == UDS_DOWNLOAD_NRC_WAIT_AND_RETRY)) &&
            uds_download_retry_action(
                download,
                nrc,
                (nrc == UDS_NRC_REQUIRED_TIME_DELAY_NOT_EXPIRED)
                    ? UDS_DOWNLOAD_SECURITY_DELAY_RETRY_US
                    : UDS_DOWNLOAD_NRC_RETRY_DELAY_US
            )) {

            /* The same stage is submitted after its NRC-specific delay. */
        } else if ((action == UDS_DOWNLOAD_NRC_RESTART_SECURITY) &&
                   (download->config.security_level != 0U)) {

            download->security_resume_state = download->state;
            download->security_unlocked = false;
            download->state =
                UDS_DOWNLOAD_REQUESTING_SECURITY_SEED;
            download->action_pending = true;
            download->last_negative_response_code = nrc;
        } else {
            download->last_negative_response_code = nrc;
            uds_download_fail(
                download,
                (event->result != ESP_OK)
                    ? event->result
                    : ESP_FAIL
            );
        }
    } else if ((download->state != UDS_DOWNLOAD_CANCELLED) &&
               ((event->type ==
                 UDS_CLIENT_EVENT_NEGATIVE_RESPONSE) ||
                (event->type == UDS_CLIENT_EVENT_TIMEOUT) ||
                (event->type ==
                 UDS_CLIENT_EVENT_TRANSPORT_ERROR) ||
                (event->type ==
                 UDS_CLIENT_EVENT_PROTOCOL_ERROR))) {

        download->last_negative_response_code =
            event->response.negative_response_code;

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
