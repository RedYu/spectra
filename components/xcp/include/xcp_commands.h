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
#define XCP_COMMAND_CONNECT       (0xFFU)
#define XCP_COMMAND_DISCONNECT    (0xFEU)
#define XCP_COMMAND_GET_STATUS    (0xFDU)
#define XCP_COMMAND_SYNCH         (0xFCU)
#define XCP_COMMAND_SHORT_UPLOAD  (0xF4U)
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
esp_err_t xcp_command_encode_short_upload(
    uint8_t element_count,
    uint8_t address_extension,
    uint32_t address,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
#ifdef __cplusplus
}
#endif
