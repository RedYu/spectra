/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */
#include "xcp_protocol.h"
#include <string.h>
esp_err_t xcp_protocol_encode_command(
    uint8_t command,
    const uint8_t *parameters,
    size_t parameter_length,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if ((command < 0xC0U) ||
        ((parameters == NULL) && (parameter_length != 0U)) ||
        (buffer == NULL) || (encoded_size == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((parameter_length == SIZE_MAX) ||
        (capacity < (parameter_length + 1U))) {
        return ESP_ERR_INVALID_SIZE;
    }
    buffer[0] = command;
    if (parameter_length != 0U) {
        memcpy(&buffer[1], parameters, parameter_length);
    }
    *encoded_size = parameter_length + 1U;
    return ESP_OK;
}
esp_err_t xcp_protocol_decode_packet(
    const uint8_t *data,
    size_t size,
    xcp_packet_t *packet
)
{
    if ((data == NULL) || (size == 0U) || (packet == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(packet, 0, sizeof(*packet));
    packet->packet_identifier = data[0];
    size_t header_size = 1U;
    switch (data[0]) {
        case XCP_PID_RES:
            packet->type = XCP_PACKET_RESPONSE;
            break;
        case XCP_PID_ERR:
            packet->type = XCP_PACKET_ERROR;
            header_size = 2U;
            break;
        case XCP_PID_EV:
            packet->type = XCP_PACKET_EVENT;
            header_size = 2U;
            break;
        case XCP_PID_SERV:
            packet->type = XCP_PACKET_SERVICE;
            header_size = 2U;
            break;
        default:
            packet->type = XCP_PACKET_DAQ;
            break;
    }
    if (size < header_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (header_size == 2U) {
        packet->code = data[1];
    }
    packet->payload = &data[header_size];
    packet->payload_length = size - header_size;
    return ESP_OK;
}
esp_err_t xcp_protocol_decode_connect_response(
    const xcp_packet_t *packet,
    xcp_connect_response_t *response
)
{
    if ((packet == NULL) || (response == NULL) ||
        (packet->type != XCP_PACKET_RESPONSE) ||
        (packet->payload == NULL) ||
        (packet->payload_length < 7U)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    memset(response, 0, sizeof(*response));
    response->resource = packet->payload[0];
    response->communication_mode_basic = packet->payload[1];
    response->maximum_cto = packet->payload[2];
    response->byte_order_big_endian =
        (packet->payload[1] & 0x01U) != 0U;
    response->maximum_dto =
        response->byte_order_big_endian
            ? ((uint16_t)packet->payload[3] << 8U) |
              (uint16_t)packet->payload[4]
            : (uint16_t)packet->payload[3] |
              ((uint16_t)packet->payload[4] << 8U);
    response->protocol_layer_version = packet->payload[5];
    response->transport_layer_version = packet->payload[6];
    response->address_granularity =
        (packet->payload[1] >> 1U) & 0x03U;
    return ((response->maximum_cto != 0U) &&
            (response->maximum_dto != 0U))
        ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}
const char *xcp_protocol_error_name(uint8_t error_code)
{
    switch (error_code) {
        case 0x00U: return "Command synchronization";
        case 0x10U: return "Command busy";
        case 0x11U: return "DAQ active";
        case 0x12U: return "Program active";
        case 0x20U: return "Command unknown";
        case 0x21U: return "Command syntax";
        case 0x22U: return "Parameters out of range";
        case 0x23U: return "Write protected";
        case 0x24U: return "Access denied";
        case 0x25U: return "Access locked";
        case 0x26U: return "Page unavailable";
        case 0x27U: return "Mode unavailable";
        case 0x28U: return "Segment invalid";
        case 0x29U: return "Sequence error";
        case 0x2AU: return "DAQ configuration invalid";
        case 0x30U: return "Memory overflow";
        case 0x31U: return "Generic error";
        case 0x32U: return "Verify failed";
        default: return "Unknown XCP error";
    }
}
