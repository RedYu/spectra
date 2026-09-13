/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */
#include "xcp_commands.h"

#include <string.h>

#include "xcp_protocol.h"

static uint16_t xcp_command_read_u16(
    const uint8_t *data,
    bool big_endian
)
{
    return big_endian
        ? ((uint16_t)data[0] << 8U) | data[1]
        : ((uint16_t)data[1] << 8U) | data[0];
}

static uint32_t xcp_command_read_u32(
    const uint8_t *data,
    bool big_endian
)
{
    uint32_t value = 0U;

    for (size_t index = 0U; index < 4U; ++index) {
        const size_t source = big_endian ? index : 3U - index;
        value = (value << 8U) | data[source];
    }

    return value;
}

static void xcp_command_write_u32(
    uint8_t *data,
    uint32_t value,
    bool big_endian
)
{
    for (size_t index = 0U; index < 4U; ++index) {
        const size_t destination = big_endian ? index : 3U - index;
        data[destination] = (uint8_t)(value >> ((3U - index) * 8U));
    }
}
static esp_err_t xcp_command_encode_empty(
    uint8_t command,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    return xcp_protocol_encode_command(
        command, NULL, 0U, buffer, capacity, encoded_size
    );
}
esp_err_t xcp_command_encode_connect(
    uint8_t mode,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    return xcp_protocol_encode_command(
        XCP_COMMAND_CONNECT,
        &mode,
        sizeof(mode),
        buffer,
        capacity,
        encoded_size
    );
}
esp_err_t xcp_command_encode_disconnect(
    uint8_t *buffer, size_t capacity, size_t *encoded_size
)
{
    return xcp_command_encode_empty(
        XCP_COMMAND_DISCONNECT, buffer, capacity, encoded_size
    );
}
esp_err_t xcp_command_encode_get_status(
    uint8_t *buffer, size_t capacity, size_t *encoded_size
)
{
    return xcp_command_encode_empty(
        XCP_COMMAND_GET_STATUS, buffer, capacity, encoded_size
    );
}
esp_err_t xcp_command_encode_synch(
    uint8_t *buffer, size_t capacity, size_t *encoded_size
)
{
    return xcp_command_encode_empty(
        XCP_COMMAND_SYNCH, buffer, capacity, encoded_size
    );
}

esp_err_t xcp_command_encode_get_communication_mode_info(
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    return xcp_command_encode_empty(
        XCP_COMMAND_GET_COMM_MODE_INFO,
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t xcp_command_encode_get_id(
    uint8_t identification_type,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    return xcp_protocol_encode_command(
        XCP_COMMAND_GET_ID,
        &identification_type,
        sizeof(identification_type),
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t xcp_command_encode_set_mta(
    uint8_t address_extension,
    uint32_t address,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    uint8_t parameters[7] = {
        0U,
        address_extension,
        0U,
    };

    xcp_command_write_u32(
        &parameters[3],
        address,
        byte_order_big_endian
    );

    return xcp_protocol_encode_command(
        XCP_COMMAND_SET_MTA,
        parameters,
        sizeof(parameters),
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t xcp_command_encode_upload(
    uint8_t element_count,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if (element_count == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    return xcp_protocol_encode_command(
        XCP_COMMAND_UPLOAD,
        &element_count,
        sizeof(element_count),
        buffer,
        capacity,
        encoded_size
    );
}
esp_err_t xcp_command_encode_short_upload(
    uint8_t element_count,
    uint8_t address_extension,
    uint32_t address,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if (element_count == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t parameters[7] = {
        element_count,
        0U,
        address_extension,
    };

    xcp_command_write_u32(
        &parameters[3],
        address,
        byte_order_big_endian
    );
    return xcp_protocol_encode_command(
        XCP_COMMAND_SHORT_UPLOAD,
        parameters,
        sizeof(parameters),
        buffer,
        capacity,
        encoded_size
    );
}

static esp_err_t xcp_command_encode_download_packet(
    uint8_t command,
    uint8_t element_count,
    const uint8_t *data,
    size_t data_size,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if ((element_count == 0U) ||
        (data == NULL) ||
        (data_size == 0U) ||
        (data_size > (XCP_CAN_FD_CTO_MAX_SIZE - 2U))) {

        return ESP_ERR_INVALID_ARG;
    }

    uint8_t parameters[XCP_CAN_FD_CTO_MAX_SIZE - 1U] = {0};
    parameters[0] = element_count;
    memcpy(&parameters[1], data, data_size);

    return xcp_protocol_encode_command(
        command,
        parameters,
        data_size + 1U,
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t xcp_command_encode_download(
    uint8_t element_count,
    const uint8_t *data,
    size_t data_size,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    return xcp_command_encode_download_packet(
        XCP_COMMAND_DOWNLOAD,
        element_count,
        data,
        data_size,
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t xcp_command_encode_download_next(
    uint8_t remaining_element_count,
    const uint8_t *data,
    size_t data_size,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    return xcp_command_encode_download_packet(
        XCP_COMMAND_DOWNLOAD_NEXT,
        remaining_element_count,
        data,
        data_size,
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t xcp_command_decode_get_status_response(
    const xcp_packet_t *packet,
    bool byte_order_big_endian,
    xcp_get_status_response_t *response
)
{
    if ((packet == NULL) ||
        (response == NULL) ||
        (packet->type != XCP_PACKET_RESPONSE) ||
        (packet->payload_length < 5U)) {

        return ESP_ERR_INVALID_RESPONSE;
    }

    *response = (xcp_get_status_response_t) {
        .session_status = packet->payload[0],
        .resource_protection_status = packet->payload[1],
        .session_configuration_id =
            xcp_command_read_u16(
                &packet->payload[3],
                byte_order_big_endian
            ),
    };

    return ESP_OK;
}

esp_err_t xcp_command_decode_get_communication_mode_info_response(
    const xcp_packet_t *packet,
    xcp_get_communication_mode_info_response_t *response
)
{
    if ((packet == NULL) ||
        (response == NULL) ||
        (packet->type != XCP_PACKET_RESPONSE) ||
        (packet->payload_length < 7U)) {

        return ESP_ERR_INVALID_RESPONSE;
    }

    *response = (xcp_get_communication_mode_info_response_t) {
        .communication_mode_optional = packet->payload[1],
        .maximum_block_size = packet->payload[3],
        .minimum_separation_time = packet->payload[4],
        .queue_size = packet->payload[5],
        .driver_version = packet->payload[6],
    };

    return ESP_OK;
}

esp_err_t xcp_command_decode_get_id_response(
    const xcp_packet_t *packet,
    bool byte_order_big_endian,
    xcp_get_id_response_t *response
)
{
    if ((packet == NULL) ||
        (response == NULL) ||
        (packet->type != XCP_PACKET_RESPONSE) ||
        (packet->payload_length < 7U)) {

        return ESP_ERR_INVALID_RESPONSE;
    }

    *response = (xcp_get_id_response_t) {
        .transfer_mode = packet->payload[0],
        .length = xcp_command_read_u32(
            &packet->payload[3],
            byte_order_big_endian
        ),
    };

    return ESP_OK;
}
