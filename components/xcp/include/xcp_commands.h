/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "xcp_protocol.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XCP_COMMAND_CONNECT       (0xFFU)
#define XCP_COMMAND_DISCONNECT    (0xFEU)
#define XCP_COMMAND_GET_STATUS    (0xFDU)
#define XCP_COMMAND_SYNCH         (0xFCU)
#define XCP_COMMAND_GET_COMM_MODE_INFO (0xFBU)
#define XCP_COMMAND_GET_ID        (0xFAU)
#define XCP_COMMAND_SET_MTA       (0xF6U)
#define XCP_COMMAND_UPLOAD        (0xF5U)
#define XCP_COMMAND_SHORT_UPLOAD  (0xF4U)
#define XCP_COMMAND_DOWNLOAD      (0xF0U)
#define XCP_COMMAND_DOWNLOAD_NEXT (0xEFU)

typedef struct
{
    uint8_t session_status;
    uint8_t resource_protection_status;
    uint16_t session_configuration_id;

} xcp_get_status_response_t;

typedef struct
{
    uint8_t communication_mode_optional;
    uint8_t maximum_block_size;
    uint8_t minimum_separation_time;
    uint8_t queue_size;
    uint8_t driver_version;

} xcp_get_communication_mode_info_response_t;

typedef struct
{
    uint8_t transfer_mode;
    uint32_t length;

} xcp_get_id_response_t;
esp_err_t xcp_command_encode_connect(
    uint8_t mode, uint8_t *buffer, size_t capacity, size_t *encoded_size
);
esp_err_t xcp_command_encode_disconnect(
    uint8_t *buffer, size_t capacity, size_t *encoded_size
);
esp_err_t xcp_command_encode_get_status(
    uint8_t *buffer, size_t capacity, size_t *encoded_size
);
esp_err_t xcp_command_encode_synch(
    uint8_t *buffer, size_t capacity, size_t *encoded_size
);
esp_err_t xcp_command_encode_get_communication_mode_info(
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_get_id(
    uint8_t identification_type,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_set_mta(
    uint8_t address_extension,
    uint32_t address,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_upload(
    uint8_t element_count,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_short_upload(
    uint8_t element_count,
    uint8_t address_extension,
    uint32_t address,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_download(
    uint8_t element_count,
    const uint8_t *data,
    size_t data_size,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_download_next(
    uint8_t remaining_element_count,
    const uint8_t *data,
    size_t data_size,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_decode_get_status_response(
    const xcp_packet_t *packet,
    bool byte_order_big_endian,
    xcp_get_status_response_t *response
);
esp_err_t xcp_command_decode_get_communication_mode_info_response(
    const xcp_packet_t *packet,
    xcp_get_communication_mode_info_response_t *response
);
esp_err_t xcp_command_decode_get_id_response(
    const xcp_packet_t *packet,
    bool byte_order_big_endian,
    xcp_get_id_response_t *response
);
#ifdef __cplusplus
}
#endif
