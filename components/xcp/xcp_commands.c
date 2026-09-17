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

static void xcp_command_write_u16(
    uint8_t *data,
    uint16_t value,
    bool big_endian
)
{
    data[big_endian ? 0U : 1U] = (uint8_t)(value >> 8U);
    data[big_endian ? 1U : 0U] = (uint8_t)value;
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

esp_err_t xcp_command_encode_get_seed(
    uint8_t mode,
    uint8_t resource,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    const uint8_t parameters[] = {mode, resource};

    if ((mode > 1U) ||
        ((resource != XCP_RESOURCE_CAL_PAG) &&
         (resource != XCP_RESOURCE_DAQ) &&
         (resource != XCP_RESOURCE_STIM) &&
         (resource != XCP_RESOURCE_PGM))) {

        return ESP_ERR_INVALID_ARG;
    }

    return xcp_protocol_encode_command(
        XCP_COMMAND_GET_SEED,
        parameters,
        sizeof(parameters),
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t xcp_command_encode_unlock(
    const uint8_t *key,
    size_t key_size,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if ((key == NULL) ||
        (key_size == 0U) ||
        (key_size > UINT8_MAX) ||
        (key_size > (XCP_CAN_FD_CTO_MAX_SIZE - 2U))) {

        return ESP_ERR_INVALID_ARG;
    }

    uint8_t parameters[XCP_CAN_FD_CTO_MAX_SIZE - 1U] = {0};
    parameters[0] = (uint8_t)key_size;
    memcpy(&parameters[1], key, key_size);

    return xcp_protocol_encode_command(
        XCP_COMMAND_UNLOCK,
        parameters,
        key_size + 1U,
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t xcp_command_encode_set_calibration_page(
    uint8_t mode,
    uint8_t segment,
    uint8_t page,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    const uint8_t parameters[] = {mode, 0U, segment, page};
    return xcp_protocol_encode_command(
        XCP_COMMAND_SET_CAL_PAGE,
        parameters,
        sizeof(parameters),
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t xcp_command_encode_get_calibration_page(
    uint8_t mode,
    uint8_t segment,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    const uint8_t parameters[] = {mode, 0U, segment};
    return xcp_protocol_encode_command(
        XCP_COMMAND_GET_CAL_PAGE,
        parameters,
        sizeof(parameters),
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t xcp_command_encode_daq_list(
    uint8_t command,
    uint16_t daq_list,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if ((command != XCP_COMMAND_CLEAR_DAQ_LIST) &&
        (command != XCP_COMMAND_GET_DAQ_LIST_MODE)) {

        return ESP_ERR_INVALID_ARG;
    }

    uint8_t parameters[3] = {0};
    xcp_command_write_u16(&parameters[1], daq_list, byte_order_big_endian);
    return xcp_protocol_encode_command(
        command, parameters, sizeof(parameters), buffer, capacity, encoded_size
    );
}

esp_err_t xcp_command_encode_set_daq_pointer(
    uint16_t daq_list,
    uint8_t odt,
    uint8_t entry,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    uint8_t parameters[5] = {0};
    xcp_command_write_u16(&parameters[1], daq_list, byte_order_big_endian);
    parameters[3] = odt;
    parameters[4] = entry;
    return xcp_protocol_encode_command(
        XCP_COMMAND_SET_DAQ_PTR,
        parameters,
        sizeof(parameters),
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t xcp_command_encode_write_daq(
    uint8_t bit_offset,
    uint8_t element_size,
    uint8_t address_extension,
    uint32_t address,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if (element_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t parameters[7] = {bit_offset, element_size, address_extension};
    xcp_command_write_u32(&parameters[3], address, byte_order_big_endian);
    return xcp_protocol_encode_command(
        XCP_COMMAND_WRITE_DAQ,
        parameters,
        sizeof(parameters),
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t xcp_command_encode_set_daq_list_mode(
    uint8_t mode,
    uint16_t daq_list,
    uint16_t event_channel,
    uint8_t prescaler,
    uint8_t priority,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    uint8_t parameters[7] = {mode};
    xcp_command_write_u16(&parameters[1], daq_list, byte_order_big_endian);
    xcp_command_write_u16(&parameters[3], event_channel, byte_order_big_endian);
    parameters[5] = prescaler;
    parameters[6] = priority;
    return xcp_protocol_encode_command(
        XCP_COMMAND_SET_DAQ_LIST_MODE,
        parameters,
        sizeof(parameters),
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t xcp_command_encode_start_stop_daq_list(
    uint8_t mode,
    uint16_t daq_list,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if (mode > 2U) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t parameters[3] = {mode};
    xcp_command_write_u16(&parameters[1], daq_list, byte_order_big_endian);
    return xcp_protocol_encode_command(
        XCP_COMMAND_START_STOP_DAQ_LIST,
        parameters,
        sizeof(parameters),
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t xcp_command_encode_start_stop_synchronization(
    uint8_t mode,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if (mode > 3U) {
        return ESP_ERR_INVALID_ARG;
    }

    return xcp_protocol_encode_command(
        XCP_COMMAND_START_STOP_SYNCH,
        &mode,
        sizeof(mode),
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t xcp_command_encode_allocate_daq(
    uint16_t count,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if (count == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t parameters[3] = {0};
    xcp_command_write_u16(&parameters[1], count, byte_order_big_endian);
    return xcp_protocol_encode_command(
        XCP_COMMAND_ALLOC_DAQ,
        parameters,
        sizeof(parameters),
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t xcp_command_encode_free_daq(
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    return xcp_command_encode_empty(
        XCP_COMMAND_FREE_DAQ, buffer, capacity, encoded_size
    );
}

esp_err_t xcp_command_encode_allocate_odt(
    uint16_t daq_list,
    uint8_t count,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if (count == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t parameters[4] = {0};
    xcp_command_write_u16(&parameters[1], daq_list, byte_order_big_endian);
    parameters[3] = count;
    return xcp_protocol_encode_command(
        XCP_COMMAND_ALLOC_ODT,
        parameters,
        sizeof(parameters),
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t xcp_command_encode_allocate_odt_entry(
    uint16_t daq_list,
    uint8_t odt,
    uint8_t count,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if (count == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t parameters[5] = {0};
    xcp_command_write_u16(&parameters[1], daq_list, byte_order_big_endian);
    parameters[3] = odt;
    parameters[4] = count;
    return xcp_protocol_encode_command(
        XCP_COMMAND_ALLOC_ODT_ENTRY,
        parameters,
        sizeof(parameters),
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t xcp_command_encode_program_start(
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    return xcp_command_encode_empty(
        XCP_COMMAND_PROGRAM_START, buffer, capacity, encoded_size
    );
}

esp_err_t xcp_command_encode_program_clear(
    uint8_t mode,
    uint32_t range,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    uint8_t parameters[7] = {mode, 0U, 0U};
    xcp_command_write_u32(&parameters[3], range, byte_order_big_endian);
    return xcp_protocol_encode_command(
        XCP_COMMAND_PROGRAM_CLEAR,
        parameters,
        sizeof(parameters),
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t xcp_command_encode_program_packet(
    uint8_t command,
    uint8_t element_count,
    const uint8_t *data,
    size_t data_size,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if ((command != XCP_COMMAND_PROGRAM) &&
        (command != XCP_COMMAND_PROGRAM_NEXT) &&
        (command != XCP_COMMAND_PROGRAM_MAX)) {

        return ESP_ERR_INVALID_ARG;
    }

    return xcp_command_encode_download_packet(
        command,
        element_count,
        data,
        data_size,
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t xcp_command_encode_program_reset(
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    return xcp_command_encode_empty(
        XCP_COMMAND_PROGRAM_RESET, buffer, capacity, encoded_size
    );
}

esp_err_t xcp_command_encode_program_prepare(
    uint16_t code_size,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if (code_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t parameters[3] = {0};
    xcp_command_write_u16(&parameters[1], code_size, byte_order_big_endian);
    return xcp_protocol_encode_command(
        XCP_COMMAND_PROGRAM_PREPARE,
        parameters,
        sizeof(parameters),
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t xcp_command_encode_program_format(
    uint8_t compression,
    uint8_t encryption,
    uint8_t programming,
    uint8_t access,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    const uint8_t parameters[] = {
        compression,
        encryption,
        programming,
        access,
    };
    return xcp_protocol_encode_command(
        XCP_COMMAND_PROGRAM_FORMAT,
        parameters,
        sizeof(parameters),
        buffer,
        capacity,
        encoded_size
    );
}

esp_err_t xcp_command_encode_program_verify(
    uint8_t mode,
    uint8_t type,
    uint32_t value,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    uint8_t parameters[7] = {mode, type, 0U};
    xcp_command_write_u32(&parameters[3], value, byte_order_big_endian);
    return xcp_protocol_encode_command(
        XCP_COMMAND_PROGRAM_VERIFY,
        parameters,
        sizeof(parameters),
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
