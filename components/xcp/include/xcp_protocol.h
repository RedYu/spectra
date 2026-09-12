/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XCP_CAN_CLASSIC_CTO_MAX_SIZE  (8U)
#define XCP_CAN_FD_CTO_MAX_SIZE       (64U)
#define XCP_PID_RES   (0xFFU)
#define XCP_PID_ERR   (0xFEU)
#define XCP_PID_EV    (0xFDU)
#define XCP_PID_SERV  (0xFCU)
typedef enum
{
    XCP_PACKET_RESPONSE = 0,
    XCP_PACKET_ERROR,
    XCP_PACKET_EVENT,
    XCP_PACKET_SERVICE,
    XCP_PACKET_DAQ,
} xcp_packet_type_t;
typedef struct
{
    xcp_packet_type_t type;
    uint8_t packet_identifier;
    uint8_t code;
    const uint8_t *payload;
    size_t payload_length;
} xcp_packet_t;
typedef struct
{
    uint8_t resource;
    uint8_t communication_mode_basic;
    uint8_t maximum_cto;
    uint16_t maximum_dto;
    uint8_t protocol_layer_version;
    uint8_t transport_layer_version;
    bool byte_order_big_endian;
    uint8_t address_granularity;
} xcp_connect_response_t;
esp_err_t xcp_protocol_encode_command(
    uint8_t command,
    const uint8_t *parameters,
    size_t parameter_length,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_protocol_decode_packet(
    const uint8_t *data,
    size_t size,
    xcp_packet_t *packet
);
esp_err_t xcp_protocol_decode_connect_response(
    const xcp_packet_t *packet,
    xcp_connect_response_t *response
);
const char *xcp_protocol_error_name(uint8_t error_code);
#ifdef __cplusplus
}
#endif
