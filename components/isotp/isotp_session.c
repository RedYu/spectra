/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "isotp_session.h"

#include <limits.h>
#include <string.h>

static bool isotp_session_link_data_length_valid(
    uint8_t link_data_length
);

static size_t isotp_session_address_size(
    const isotp_session_t *session
);

static void isotp_session_clear_action(
    isotp_session_action_t *action
);

static esp_err_t isotp_session_fail(
    isotp_session_t *session,
    isotp_session_error_t error,
    esp_err_t result,
    isotp_session_action_t *action
);

static esp_err_t isotp_session_create_flow_control(
    isotp_session_t *session,
    isotp_flow_status_t status,
    isotp_session_action_t *action
);

static esp_err_t isotp_session_create_consecutive_frame(
    isotp_session_t *session,
    isotp_session_action_t *action
);

static esp_err_t isotp_session_receive_single_frame(
    isotp_session_t *session,
    const uint8_t *frame_data,
    const isotp_pci_t *pci,
    isotp_session_action_t *action
);

static esp_err_t isotp_session_receive_first_frame(
    isotp_session_t *session,
    const uint8_t *frame_data,
    size_t frame_data_length,
    const isotp_pci_t *pci,
    isotp_session_action_t *action
);

static esp_err_t isotp_session_receive_consecutive_frame(
    isotp_session_t *session,
    const uint8_t *frame_data,
    size_t frame_data_length,
    const isotp_pci_t *pci,
    uint64_t now_us,
    isotp_session_action_t *action
);

static esp_err_t isotp_session_receive_flow_control(
    isotp_session_t *session,
    const isotp_pci_t *pci,
    uint64_t now_us,
    isotp_session_action_t *action
);

esp_err_t isotp_session_init(
    isotp_session_t *session,
    const isotp_session_config_t *config,
    uint8_t *receive_buffer,
    size_t receive_capacity
)
{
    if ((session == NULL) ||
        (config == NULL) ||
        (receive_buffer == NULL) ||
        (receive_capacity == 0U) ||
        (config->addressing_mode > ISOTP_ADDRESSING_MIXED) ||
        !isotp_session_link_data_length_valid(
            config->link_data_length
        ) ||
        ((config->addressing_mode != ISOTP_ADDRESSING_NORMAL) &&
         (config->link_data_length <= 3U))) {

        return ESP_ERR_INVALID_ARG;
    }

    uint32_t receive_st_min_us = 0U;

    if (isotp_protocol_st_min_to_us(
            config->receive_st_min,
            &receive_st_min_us
        ) != ESP_OK) {

        return ESP_ERR_INVALID_ARG;
    }

    (void)receive_st_min_us;

    memset(
        session,
        0,
        sizeof(*session)
    );

    session->config = *config;

    if (session->config.maximum_wait_frames == 0U) {
        session->config.maximum_wait_frames =
            ISOTP_SESSION_DEFAULT_MAX_WAIT;
    }

    if (session->config.flow_control_timeout_us == 0U) {
        session->config.flow_control_timeout_us =
            ISOTP_SESSION_DEFAULT_TIMEOUT_US;
    }

    if (session->config.consecutive_frame_timeout_us == 0U) {
        session->config.consecutive_frame_timeout_us =
            ISOTP_SESSION_DEFAULT_TIMEOUT_US;
    }

    session->receive_buffer = receive_buffer;
    session->receive_capacity = receive_capacity;
    session->state = ISOTP_SESSION_IDLE;

    return ESP_OK;
}

void isotp_session_reset(
    isotp_session_t *session
)
{
    if (session == NULL) {
        return;
    }

    const isotp_session_config_t config = session->config;
    uint8_t *receive_buffer = session->receive_buffer;
    const size_t receive_capacity = session->receive_capacity;

    memset(
        session,
        0,
        sizeof(*session)
    );

    session->config = config;
    session->receive_buffer = receive_buffer;
    session->receive_capacity = receive_capacity;
    session->state = ISOTP_SESSION_IDLE;
}

esp_err_t isotp_session_start_transmit(
    isotp_session_t *session,
    const uint8_t *payload,
    size_t payload_length,
    uint64_t now_us,
    isotp_session_action_t *action
)
{
    (void)now_us;

    if ((session == NULL) ||
        (payload == NULL) ||
        (payload_length == 0U) ||
        (action == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    isotp_session_clear_action(action);

    if (session->state != ISOTP_SESSION_IDLE) {
        return ESP_ERR_INVALID_STATE;
    }

    session->transmit_buffer = payload;
    session->transmit_size = payload_length;
    session->transmit_sequence_number = 1U;

    memset(
        action->frame_data,
        session->config.padding_byte,
        session->config.link_data_length
    );

    const size_t address_size =
        isotp_session_address_size(session);

    if (address_size != 0U) {
        action->frame_data[0] =
            session->config.transmit_address;
    }

    uint8_t payload_offset = 0U;
    esp_err_t result =
        isotp_protocol_encode_single_frame(
            payload_length,
            session->config.link_data_length - address_size,
            &action->frame_data[address_size],
            &payload_offset
        );

    payload_offset += (uint8_t)address_size;

    if (result == ESP_OK) {
        memcpy(
            &action->frame_data[payload_offset],
            payload,
            payload_length
        );

        session->transmit_pending_size = payload_length;
        session->state = ISOTP_SESSION_TX_SENDING_SINGLE_FRAME;
        action->type = ISOTP_SESSION_ACTION_SEND_FRAME;
        action->frame_data_length = session->config.link_data_length;

        return ESP_OK;
    }

    if ((result != ESP_ERR_INVALID_SIZE) ||
        session->config.functional_transmit ||
        (payload_length > UINT32_MAX)) {

        isotp_session_reset(session);
        return result;
    }

    result =
        isotp_protocol_encode_first_frame(
            (uint32_t)payload_length,
            session->config.link_data_length - address_size,
            &action->frame_data[address_size],
            &payload_offset
        );

    payload_offset += (uint8_t)address_size;

    if (result != ESP_OK) {
        isotp_session_reset(session);
        return result;
    }

    const size_t first_payload_size =
        session->config.link_data_length - payload_offset;

    memcpy(
        &action->frame_data[payload_offset],
        payload,
        first_payload_size
    );

    session->transmit_pending_size = first_payload_size;
    session->state = ISOTP_SESSION_TX_SENDING_FIRST_FRAME;
    action->type = ISOTP_SESSION_ACTION_SEND_FRAME;
    action->frame_data_length = session->config.link_data_length;

    return ESP_OK;
}

esp_err_t isotp_session_receive_frame(
    isotp_session_t *session,
    const uint8_t *frame_data,
    size_t frame_data_length,
    uint64_t now_us,
    isotp_session_action_t *action
)
{
    if ((session == NULL) ||
        (frame_data == NULL) ||
        (action == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    isotp_session_clear_action(action);

    const size_t address_size =
        isotp_session_address_size(session);

    if ((frame_data_length <= address_size) ||
        ((address_size != 0U) &&
         (frame_data[0] != session->config.receive_address))) {

        return isotp_session_fail(
            session,
            ISOTP_SESSION_ERROR_PROTOCOL,
            ESP_ERR_INVALID_RESPONSE,
            action
        );
    }

    isotp_pci_t pci = {0};
    const esp_err_t result =
        isotp_protocol_decode(
            &frame_data[address_size],
            frame_data_length - address_size,
            &pci
        );

    if (result != ESP_OK) {
        return isotp_session_fail(
            session,
            ISOTP_SESSION_ERROR_PROTOCOL,
            result,
            action
        );
    }

    pci.payload_offset += (uint8_t)address_size;

    switch (pci.type) {
        case ISOTP_PCI_SINGLE_FRAME:
            return isotp_session_receive_single_frame(
                session,
                frame_data,
                &pci,
                action
            );

        case ISOTP_PCI_FIRST_FRAME:
            return isotp_session_receive_first_frame(
                session,
                frame_data,
                frame_data_length,
                &pci,
                action
            );

        case ISOTP_PCI_CONSECUTIVE_FRAME:
            return isotp_session_receive_consecutive_frame(
                session,
                frame_data,
                frame_data_length,
                &pci,
                now_us,
                action
            );

        case ISOTP_PCI_FLOW_CONTROL:
            return isotp_session_receive_flow_control(
                session,
                &pci,
                now_us,
                action
            );

        default:
            return isotp_session_fail(
                session,
                ISOTP_SESSION_ERROR_PROTOCOL,
                ESP_ERR_INVALID_RESPONSE,
                action
            );
    }
}

esp_err_t isotp_session_frame_transmitted(
    isotp_session_t *session,
    uint64_t now_us,
    isotp_session_action_t *action
)
{
    if ((session == NULL) ||
        (action == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    isotp_session_clear_action(action);

    switch (session->state) {
        case ISOTP_SESSION_RX_SENDING_FLOW_CONTROL:
            session->state =
                ISOTP_SESSION_RX_WAIT_CONSECUTIVE_FRAME;
            session->deadline_us =
                now_us +
                session->config.consecutive_frame_timeout_us;
            return ESP_OK;

        case ISOTP_SESSION_RX_SENDING_OVERFLOW:
            return isotp_session_fail(
                session,
                ISOTP_SESSION_ERROR_BUFFER_OVERFLOW,
                ESP_ERR_NO_MEM,
                action
            );

        case ISOTP_SESSION_TX_SENDING_SINGLE_FRAME:
            session->transmit_offset = session->transmit_size;
            session->state = ISOTP_SESSION_TX_COMPLETE;
            action->type = ISOTP_SESSION_ACTION_TRANSMIT_COMPLETE;
            action->message_length = session->transmit_size;
            return ESP_OK;

        case ISOTP_SESSION_TX_SENDING_FIRST_FRAME:
            session->transmit_offset +=
                session->transmit_pending_size;
            session->transmit_pending_size = 0U;
            session->state = ISOTP_SESSION_TX_WAIT_FLOW_CONTROL;
            session->deadline_us =
                now_us +
                session->config.flow_control_timeout_us;
            return ESP_OK;

        case ISOTP_SESSION_TX_SENDING_CONSECUTIVE_FRAME:
            session->transmit_offset +=
                session->transmit_pending_size;
            session->transmit_pending_size = 0U;

            if (session->transmit_offset >= session->transmit_size) {
                session->state = ISOTP_SESSION_TX_COMPLETE;
                action->type = ISOTP_SESSION_ACTION_TRANSMIT_COMPLETE;
                action->message_length = session->transmit_size;
                return ESP_OK;
            }

            session->transmit_sequence_number =
                (session->transmit_sequence_number + 1U) &
                ISOTP_SEQUENCE_NUMBER_MAX;
            session->transmit_block_count++;

            if ((session->transmit_block_size != 0U) &&
                (session->transmit_block_count >=
                    session->transmit_block_size)) {

                session->state = ISOTP_SESSION_TX_WAIT_FLOW_CONTROL;
                session->deadline_us =
                    now_us +
                    session->config.flow_control_timeout_us;
                return ESP_OK;
            }

            if (session->transmit_st_min_us == 0U) {
                return isotp_session_create_consecutive_frame(
                    session,
                    action
                );
            }

            session->state = ISOTP_SESSION_TX_WAIT_ST_MIN;
            session->deadline_us =
                now_us + session->transmit_st_min_us;
            return ESP_OK;

        default:
            return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t isotp_session_poll(
    isotp_session_t *session,
    uint64_t now_us,
    isotp_session_action_t *action
)
{
    if ((session == NULL) ||
        (action == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    isotp_session_clear_action(action);

    if ((session->state == ISOTP_SESSION_TX_WAIT_ST_MIN) &&
        (now_us >= session->deadline_us)) {

        return isotp_session_create_consecutive_frame(
            session,
            action
        );
    }

    if (((session->state == ISOTP_SESSION_TX_WAIT_FLOW_CONTROL) ||
         (session->state ==
            ISOTP_SESSION_RX_WAIT_CONSECUTIVE_FRAME)) &&
        (now_us >= session->deadline_us)) {

        return isotp_session_fail(
            session,
            ISOTP_SESSION_ERROR_TIMEOUT,
            ESP_ERR_TIMEOUT,
            action
        );
    }

    return ESP_OK;
}

static bool isotp_session_link_data_length_valid(
    uint8_t link_data_length
)
{
    switch (link_data_length) {
        case 8U:
        case 12U:
        case 16U:
        case 20U:
        case 24U:
        case 32U:
        case 48U:
        case 64U:
            return true;

        default:
            return false;
    }
}

static size_t isotp_session_address_size(
    const isotp_session_t *session
)
{
    return session->config.addressing_mode ==
        ISOTP_ADDRESSING_NORMAL ? 0U : 1U;
}

static void isotp_session_clear_action(
    isotp_session_action_t *action
)
{
    memset(
        action,
        0,
        sizeof(*action)
    );
}

static esp_err_t isotp_session_fail(
    isotp_session_t *session,
    isotp_session_error_t error,
    esp_err_t result,
    isotp_session_action_t *action
)
{
    session->state = ISOTP_SESSION_ERROR;
    session->error = error;
    action->type = ISOTP_SESSION_ACTION_ERROR;
    action->error = error;

    return result;
}

static esp_err_t isotp_session_create_flow_control(
    isotp_session_t *session,
    isotp_flow_status_t status,
    isotp_session_action_t *action
)
{
    memset(
        action->frame_data,
        session->config.padding_byte,
        session->config.link_data_length
    );

    const size_t address_size =
        isotp_session_address_size(session);

    if (address_size != 0U) {
        action->frame_data[0] =
            session->config.transmit_address;
    }

    uint8_t payload_offset = 0U;
    const esp_err_t result =
        isotp_protocol_encode_flow_control(
            status,
            session->config.receive_block_size,
            session->config.receive_st_min,
            &action->frame_data[address_size],
            &payload_offset
        );

    (void)payload_offset;

    if (result != ESP_OK) {
        return isotp_session_fail(
            session,
            ISOTP_SESSION_ERROR_PROTOCOL,
            result,
            action
        );
    }

    session->state =
        status == ISOTP_FLOW_STATUS_OVERFLOW
            ? ISOTP_SESSION_RX_SENDING_OVERFLOW
            : ISOTP_SESSION_RX_SENDING_FLOW_CONTROL;
    action->type = ISOTP_SESSION_ACTION_SEND_FRAME;
    action->frame_data_length = session->config.link_data_length;

    return ESP_OK;
}

static esp_err_t isotp_session_create_consecutive_frame(
    isotp_session_t *session,
    isotp_session_action_t *action
)
{
    memset(
        action->frame_data,
        session->config.padding_byte,
        session->config.link_data_length
    );

    const size_t address_size =
        isotp_session_address_size(session);

    if (address_size != 0U) {
        action->frame_data[0] =
            session->config.transmit_address;
    }

    uint8_t payload_offset = 0U;
    const esp_err_t result =
        isotp_protocol_encode_consecutive_frame(
            session->transmit_sequence_number,
            &action->frame_data[address_size],
            &payload_offset
        );

    payload_offset += (uint8_t)address_size;

    if (result != ESP_OK) {
        return isotp_session_fail(
            session,
            ISOTP_SESSION_ERROR_PROTOCOL,
            result,
            action
        );
    }

    const size_t remaining =
        session->transmit_size - session->transmit_offset;
    const size_t frame_capacity =
        session->config.link_data_length - payload_offset;
    const size_t payload_size =
        (remaining < frame_capacity) ?
            remaining : frame_capacity;

    memcpy(
        &action->frame_data[payload_offset],
        &session->transmit_buffer[session->transmit_offset],
        payload_size
    );

    session->transmit_pending_size = payload_size;
    session->state = ISOTP_SESSION_TX_SENDING_CONSECUTIVE_FRAME;
    action->type = ISOTP_SESSION_ACTION_SEND_FRAME;
    action->frame_data_length = session->config.link_data_length;

    return ESP_OK;
}

static esp_err_t isotp_session_receive_single_frame(
    isotp_session_t *session,
    const uint8_t *frame_data,
    const isotp_pci_t *pci,
    isotp_session_action_t *action
)
{
    if (session->state != ISOTP_SESSION_IDLE) {
        return isotp_session_fail(
            session,
            ISOTP_SESSION_ERROR_UNEXPECTED_FRAME,
            ESP_ERR_INVALID_STATE,
            action
        );
    }

    if (pci->payload_length > session->receive_capacity) {
        return isotp_session_fail(
            session,
            ISOTP_SESSION_ERROR_BUFFER_OVERFLOW,
            ESP_ERR_NO_MEM,
            action
        );
    }

    memcpy(
        session->receive_buffer,
        &frame_data[pci->payload_offset],
        pci->payload_length
    );

    session->receive_size = pci->payload_length;
    session->receive_expected_size = pci->payload_length;
    session->state = ISOTP_SESSION_RX_COMPLETE;
    action->type = ISOTP_SESSION_ACTION_RECEIVE_COMPLETE;
    action->message_length = pci->payload_length;

    return ESP_OK;
}

static esp_err_t isotp_session_receive_first_frame(
    isotp_session_t *session,
    const uint8_t *frame_data,
    size_t frame_data_length,
    const isotp_pci_t *pci,
    isotp_session_action_t *action
)
{
    if (session->state != ISOTP_SESSION_IDLE) {
        return isotp_session_fail(
            session,
            ISOTP_SESSION_ERROR_UNEXPECTED_FRAME,
            ESP_ERR_INVALID_STATE,
            action
        );
    }

    if (pci->payload_length > session->receive_capacity) {
        return isotp_session_create_flow_control(
            session,
            ISOTP_FLOW_STATUS_OVERFLOW,
            action
        );
    }

    const size_t first_payload_size =
        frame_data_length - pci->payload_offset;

    memcpy(
        session->receive_buffer,
        &frame_data[pci->payload_offset],
        first_payload_size
    );

    session->receive_size = first_payload_size;
    session->receive_expected_size = pci->payload_length;
    session->receive_sequence_number = 1U;
    session->receive_block_count = 0U;

    return isotp_session_create_flow_control(
        session,
        ISOTP_FLOW_STATUS_CONTINUE_TO_SEND,
        action
    );
}

static esp_err_t isotp_session_receive_consecutive_frame(
    isotp_session_t *session,
    const uint8_t *frame_data,
    size_t frame_data_length,
    const isotp_pci_t *pci,
    uint64_t now_us,
    isotp_session_action_t *action
)
{
    if (session->state !=
        ISOTP_SESSION_RX_WAIT_CONSECUTIVE_FRAME) {

        return isotp_session_fail(
            session,
            ISOTP_SESSION_ERROR_UNEXPECTED_FRAME,
            ESP_ERR_INVALID_STATE,
            action
        );
    }

    if (pci->sequence_number !=
        session->receive_sequence_number) {

        return isotp_session_fail(
            session,
            ISOTP_SESSION_ERROR_SEQUENCE,
            ESP_ERR_INVALID_RESPONSE,
            action
        );
    }

    const size_t remaining =
        session->receive_expected_size - session->receive_size;
    const size_t available =
        frame_data_length - pci->payload_offset;
    const size_t payload_size =
        (remaining < available) ? remaining : available;

    memcpy(
        &session->receive_buffer[session->receive_size],
        &frame_data[pci->payload_offset],
        payload_size
    );

    session->receive_size += payload_size;

    if (session->receive_size >=
        session->receive_expected_size) {

        session->state = ISOTP_SESSION_RX_COMPLETE;
        action->type = ISOTP_SESSION_ACTION_RECEIVE_COMPLETE;
        action->message_length = session->receive_expected_size;
        return ESP_OK;
    }

    session->receive_sequence_number =
        (session->receive_sequence_number + 1U) &
        ISOTP_SEQUENCE_NUMBER_MAX;
    session->receive_block_count++;
    session->deadline_us =
        now_us +
        session->config.consecutive_frame_timeout_us;

    if ((session->config.receive_block_size != 0U) &&
        (session->receive_block_count >=
            session->config.receive_block_size)) {

        session->receive_block_count = 0U;
        return isotp_session_create_flow_control(
            session,
            ISOTP_FLOW_STATUS_CONTINUE_TO_SEND,
            action
        );
    }

    return ESP_OK;
}

static esp_err_t isotp_session_receive_flow_control(
    isotp_session_t *session,
    const isotp_pci_t *pci,
    uint64_t now_us,
    isotp_session_action_t *action
)
{
    if (session->state != ISOTP_SESSION_TX_WAIT_FLOW_CONTROL) {
        return isotp_session_fail(
            session,
            ISOTP_SESSION_ERROR_UNEXPECTED_FRAME,
            ESP_ERR_INVALID_STATE,
            action
        );
    }

    switch (pci->flow_status) {
        case ISOTP_FLOW_STATUS_CONTINUE_TO_SEND:
            session->transmit_block_size = pci->block_size;
            session->transmit_block_count = 0U;
            session->transmit_wait_count = 0U;

            if (isotp_protocol_st_min_to_us(
                    pci->st_min,
                    &session->transmit_st_min_us
                ) != ESP_OK) {

                return isotp_session_fail(
                    session,
                    ISOTP_SESSION_ERROR_PROTOCOL,
                    ESP_ERR_INVALID_RESPONSE,
                    action
                );
            }

            return isotp_session_create_consecutive_frame(
                session,
                action
            );

        case ISOTP_FLOW_STATUS_WAIT:
            session->transmit_wait_count++;

            if (session->transmit_wait_count >
                session->config.maximum_wait_frames) {

                return isotp_session_fail(
                    session,
                    ISOTP_SESSION_ERROR_WAIT_LIMIT,
                    ESP_ERR_INVALID_RESPONSE,
                    action
                );
            }

            session->deadline_us =
                now_us +
                session->config.flow_control_timeout_us;
            return ESP_OK;

        case ISOTP_FLOW_STATUS_OVERFLOW:
            return isotp_session_fail(
                session,
                ISOTP_SESSION_ERROR_FLOW_CONTROL_OVERFLOW,
                ESP_ERR_NO_MEM,
                action
            );

        default:
            return isotp_session_fail(
                session,
                ISOTP_SESSION_ERROR_PROTOCOL,
                ESP_ERR_INVALID_RESPONSE,
                action
            );
    }
}
