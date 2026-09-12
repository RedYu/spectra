/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */
#include "xcp_commands.h"
#include "xcp_protocol.h"
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

    for (size_t index = 0U; index < 4U; ++index) {
        const size_t destination =
            byte_order_big_endian
                ? 3U + index
                : 6U - index;

        parameters[destination] =
            (uint8_t)(address >> ((3U - index) * 8U));
    }
    return xcp_protocol_encode_command(
        XCP_COMMAND_SHORT_UPLOAD,
        parameters,
        sizeof(parameters),
        buffer,
        capacity,
        encoded_size
    );
}
