/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "uds_client.h"

#include <string.h>

#include "esp_timer.h"

static void uds_client_transport_callback(
    const isotp_service_event_t *event,
    void *context
);

static void uds_client_notify(
    uds_client_t *client,
    uds_client_event_type_t type,
    const uds_response_t *response,
    esp_err_t result,
    isotp_session_error_t transport_error
);

static esp_err_t uds_client_start_request(
    uds_client_t *client,
    uint8_t service_id,
    const uint8_t *parameters,
    size_t parameter_length,
    bool response_expected,
    uint64_t now_us
);

esp_err_t uds_client_open(
    uds_client_t *client,
    const uds_client_config_t *config
)
{
    if ((client == NULL) ||
        (config == NULL) ||
        (config->callback == NULL) ||
        (config->transport.transmit_buffer == NULL) ||
        (config->transport.transmit_capacity < 2U)) {

        return ESP_ERR_INVALID_ARG;
    }

    memset(
        client,
        0,
        sizeof(*client)
    );

    client->config = *config;

    if (client->config.p2_timeout_us == 0U) {
        client->config.p2_timeout_us =
            UDS_CLIENT_DEFAULT_P2_TIMEOUT_US;
    }

    if (client->config.p2_star_timeout_us == 0U) {
        client->config.p2_star_timeout_us =
            UDS_CLIENT_DEFAULT_P2_STAR_TIMEOUT_US;
    }

    client->config.transport.callback =
        uds_client_transport_callback;
    client->config.transport.callback_context = client;

    const esp_err_t result =
        isotp_service_open_channel(
            &client->config.transport,
            &client->channel_id
        );

    if (result != ESP_OK) {
        client->state = UDS_CLIENT_CLOSED;
        client->last_result = result;
        return result;
    }

    client->state = UDS_CLIENT_IDLE;
    client->last_result = ESP_OK;

    return ESP_OK;
}

esp_err_t uds_client_close(
    uds_client_t *client
)
{
    if (client == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((client->state == UDS_CLIENT_CLOSED) ||
        (client->channel_id == ISOTP_SERVICE_CHANNEL_ID_NONE)) {

        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result =
        isotp_service_close_channel(
            client->channel_id
        );

    if (result == ESP_OK) {
        client->channel_id = ISOTP_SERVICE_CHANNEL_ID_NONE;
        client->state = UDS_CLIENT_CLOSED;
        client->deadline_us = 0U;
    }

    return result;
}

esp_err_t uds_client_request(
    uds_client_t *client,
    uint8_t service_id,
    const uint8_t *parameters,
    size_t parameter_length,
    uint64_t now_us
)
{
    return uds_client_start_request(
        client,
        service_id,
        parameters,
        parameter_length,
        true,
        now_us
    );
}

static esp_err_t uds_client_start_request(
    uds_client_t *client,
    uint8_t service_id,
    const uint8_t *parameters,
    size_t parameter_length,
    bool response_expected,
    uint64_t now_us
)
{
    (void)now_us;

    if (client == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((client->state != UDS_CLIENT_IDLE) &&
        (client->state != UDS_CLIENT_COMPLETE) &&
        (client->state != UDS_CLIENT_NEGATIVE_RESPONSE) &&
        (client->state != UDS_CLIENT_ERROR)) {

        return ESP_ERR_INVALID_STATE;
    }

    size_t request_size = 0U;
    esp_err_t result =
        uds_protocol_encode_request(
            service_id,
            parameters,
            parameter_length,
            client->config.transport.transmit_buffer,
            client->config.transport.transmit_capacity,
            &request_size
        );

    if (result != ESP_OK) {
        return result;
    }

    client->request_service_id = service_id;
    client->response_expected = response_expected;
    client->state = UDS_CLIENT_TRANSMITTING;
    client->deadline_us = 0U;
    client->last_result = ESP_OK;
    client->last_negative_response_code = 0U;

    result =
        isotp_service_send(
            client->channel_id,
            client->config.transport.transmit_buffer,
            request_size
        );

    if (result != ESP_OK) {
        client->state = UDS_CLIENT_ERROR;
        client->last_result = result;
        return result;
    }

    return ESP_OK;
}

esp_err_t uds_client_poll(
    uds_client_t *client,
    uint64_t now_us
)
{
    if (client == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (((client->state == UDS_CLIENT_WAIT_RESPONSE) ||
         (client->state == UDS_CLIENT_RESPONSE_PENDING)) &&
        (now_us >= client->deadline_us)) {

        client->state = UDS_CLIENT_ERROR;
        client->last_result = ESP_ERR_TIMEOUT;
        client->deadline_us = 0U;

        uds_client_notify(
            client,
            UDS_CLIENT_EVENT_TIMEOUT,
            NULL,
            ESP_ERR_TIMEOUT,
            ISOTP_SESSION_ERROR_NONE
        );

        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t uds_client_diagnostic_session_control(
    uds_client_t *client,
    uint8_t session_type,
    bool suppress_positive_response,
    uint64_t now_us
)
{
    const uint8_t parameter =
        session_type |
        (suppress_positive_response
            ? UDS_SUPPRESS_POSITIVE_RESPONSE
            : 0U);

    if ((session_type == 0U) ||
        ((session_type & UDS_SUPPRESS_POSITIVE_RESPONSE) != 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    return uds_client_start_request(
        client,
        UDS_SERVICE_DIAGNOSTIC_SESSION_CONTROL,
        &parameter,
        sizeof(parameter),
        !suppress_positive_response,
        now_us
    );
}

esp_err_t uds_client_ecu_reset(
    uds_client_t *client,
    uint8_t reset_type,
    bool suppress_positive_response,
    uint64_t now_us
)
{
    const uint8_t parameter =
        reset_type |
        (suppress_positive_response
            ? UDS_SUPPRESS_POSITIVE_RESPONSE
            : 0U);

    if ((reset_type == 0U) ||
        ((reset_type & UDS_SUPPRESS_POSITIVE_RESPONSE) != 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    return uds_client_start_request(
        client,
        UDS_SERVICE_ECU_RESET,
        &parameter,
        sizeof(parameter),
        !suppress_positive_response,
        now_us
    );
}

esp_err_t uds_client_tester_present(
    uds_client_t *client,
    bool suppress_positive_response,
    uint64_t now_us
)
{
    const uint8_t parameter =
        suppress_positive_response
            ? UDS_SUPPRESS_POSITIVE_RESPONSE
            : 0U;

    return uds_client_start_request(
        client,
        UDS_SERVICE_TESTER_PRESENT,
        &parameter,
        sizeof(parameter),
        !suppress_positive_response,
        now_us
    );
}

esp_err_t uds_client_read_data_by_identifier(
    uds_client_t *client,
    uint16_t identifier,
    uint64_t now_us
)
{
    const uint8_t parameters[2] = {
        (uint8_t)(identifier >> 8U),
        (uint8_t)identifier,
    };

    return uds_client_request(
        client,
        UDS_SERVICE_READ_DATA_BY_IDENTIFIER,
        parameters,
        sizeof(parameters),
        now_us
    );
}

esp_err_t uds_client_write_data_by_identifier(
    uds_client_t *client,
    uint16_t identifier,
    const uint8_t *data,
    size_t data_length,
    uint64_t now_us
)
{
    if ((data == NULL) ||
        (data_length == 0U) ||
        (data_length > UDS_CLIENT_WRITE_DATA_MAX_LENGTH)) {

        return ESP_ERR_INVALID_ARG;
    }

    uint8_t parameters[2U + UDS_CLIENT_WRITE_DATA_MAX_LENGTH];

    parameters[0] = (uint8_t)(identifier >> 8U);
    parameters[1] = (uint8_t)identifier;

    memcpy(
        &parameters[2],
        data,
        data_length
    );

    return uds_client_request(
        client,
        UDS_SERVICE_WRITE_DATA_BY_IDENTIFIER,
        parameters,
        2U + data_length,
        now_us
    );
}

esp_err_t uds_client_read_dtc_information(
    uds_client_t *client,
    uint8_t subfunction,
    uint8_t status_mask,
    uint64_t now_us
)
{
    uint8_t request[3] = {0};
    size_t request_size = 0U;
    const esp_err_t result =
        uds_request_encode_read_dtc_information(
            subfunction,
            status_mask,
            request,
            sizeof(request),
            &request_size
        );

    if (result != ESP_OK) {
        return result;
    }

    return uds_client_request(
        client,
        request[0],
        &request[1],
        request_size - 1U,
        now_us
    );
}

esp_err_t uds_client_clear_diagnostic_information(
    uds_client_t *client,
    uint32_t group_of_dtc,
    uint64_t now_us
)
{
    uint8_t request[4] = {0};
    size_t request_size = 0U;
    const esp_err_t result =
        uds_request_encode_clear_diagnostic_information(
            group_of_dtc,
            request,
            sizeof(request),
            &request_size
        );

    if (result != ESP_OK) {
        return result;
    }

    return uds_client_request(
        client,
        request[0],
        &request[1],
        request_size - 1U,
        now_us
    );
}

bool uds_client_busy(
    const uds_client_t *client
)
{
    if (client == NULL) {
        return false;
    }

    return (client->state == UDS_CLIENT_TRANSMITTING) ||
           (client->state == UDS_CLIENT_WAIT_RESPONSE) ||
           (client->state == UDS_CLIENT_RESPONSE_PENDING);
}

static void uds_client_transport_callback(
    const isotp_service_event_t *event,
    void *context
)
{
    uds_client_t *client = context;

    if ((event == NULL) || (client == NULL)) {
        return;
    }

    switch (event->type) {
        case ISOTP_SERVICE_EVENT_TRANSMITTED:
            if (client->state == UDS_CLIENT_TRANSMITTING) {
                if (client->response_expected) {
                    client->state = UDS_CLIENT_WAIT_RESPONSE;
                    client->deadline_us =
                        (uint64_t)esp_timer_get_time() +
                        client->config.p2_timeout_us;
                } else {
                    client->state = UDS_CLIENT_COMPLETE;
                    client->deadline_us = 0U;
                    uds_client_notify(
                        client,
                        UDS_CLIENT_EVENT_TRANSMITTED,
                        NULL,
                        ESP_OK,
                        ISOTP_SESSION_ERROR_NONE
                    );
                }
            }
            break;

        case ISOTP_SERVICE_EVENT_RECEIVED:
        {
            if ((client->state != UDS_CLIENT_WAIT_RESPONSE) &&
                (client->state != UDS_CLIENT_RESPONSE_PENDING)) {

                return;
            }

            uds_response_t response = {0};
            const esp_err_t result =
                uds_protocol_decode_response(
                    event->payload,
                    event->payload_length,
                    client->request_service_id,
                    &response
                );

            if (result != ESP_OK) {
                client->state = UDS_CLIENT_ERROR;
                client->last_result = result;
                client->deadline_us = 0U;
                uds_client_notify(
                    client,
                    UDS_CLIENT_EVENT_PROTOCOL_ERROR,
                    NULL,
                    result,
                    ISOTP_SESSION_ERROR_NONE
                );
                return;
            }

            if (!response.positive &&
                (response.negative_response_code ==
                 UDS_NRC_RESPONSE_PENDING)) {

                client->state = UDS_CLIENT_RESPONSE_PENDING;
                client->last_negative_response_code =
                    response.negative_response_code;
                client->deadline_us =
                    (uint64_t)esp_timer_get_time() +
                    client->config.p2_star_timeout_us;
                uds_client_notify(
                    client,
                    UDS_CLIENT_EVENT_RESPONSE_PENDING,
                    &response,
                    ESP_OK,
                    ISOTP_SESSION_ERROR_NONE
                );
                return;
            }

            client->deadline_us = 0U;
            client->last_result = ESP_OK;
            client->last_negative_response_code =
                response.negative_response_code;

            if (response.positive) {
                client->state = UDS_CLIENT_COMPLETE;
                uds_client_notify(
                    client,
                    UDS_CLIENT_EVENT_RESPONSE,
                    &response,
                    ESP_OK,
                    ISOTP_SESSION_ERROR_NONE
                );
            } else {
                client->state = UDS_CLIENT_NEGATIVE_RESPONSE;
                uds_client_notify(
                    client,
                    UDS_CLIENT_EVENT_NEGATIVE_RESPONSE,
                    &response,
                    ESP_OK,
                    ISOTP_SESSION_ERROR_NONE
                );
            }
            break;
        }

        case ISOTP_SERVICE_EVENT_ERROR:
            client->state = UDS_CLIENT_ERROR;
            client->last_result = event->result;
            client->deadline_us = 0U;
            uds_client_notify(
                client,
                UDS_CLIENT_EVENT_TRANSPORT_ERROR,
                NULL,
                event->result,
                event->session_error
            );
            break;

        default:
            break;
    }
}

static void uds_client_notify(
    uds_client_t *client,
    uds_client_event_type_t type,
    const uds_response_t *response,
    esp_err_t result,
    isotp_session_error_t transport_error
)
{
    if (client->config.callback == NULL) {
        return;
    }

    uds_client_event_t event = {
        .type = type,
        .result = result,
        .transport_error = transport_error,
    };

    if (response != NULL) {
        event.response = *response;
    }

    client->config.callback(
        &event,
        client->config.callback_context
    );
}
