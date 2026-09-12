/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ISOTP_CLASSIC_FRAME_DATA_LENGTH  (8U)
#define ISOTP_FD_FRAME_DATA_MAX_LENGTH   (64U)

#define ISOTP_SEQUENCE_NUMBER_MAX        (0x0FU)
#define ISOTP_MESSAGE_LENGTH_12_BIT_MAX  (0x0FFFU)

/**
 * @brief ISO-TP protocol control information type.
 */
typedef enum
{
    ISOTP_PCI_SINGLE_FRAME = 0,
    ISOTP_PCI_FIRST_FRAME,
    ISOTP_PCI_CONSECUTIVE_FRAME,
    ISOTP_PCI_FLOW_CONTROL,

} isotp_pci_type_t;

/**
 * @brief Flow-control status transmitted by an ISO-TP receiver.
 */
typedef enum
{
    ISOTP_FLOW_STATUS_CONTINUE_TO_SEND = 0,
    ISOTP_FLOW_STATUS_WAIT,
    ISOTP_FLOW_STATUS_OVERFLOW,

} isotp_flow_status_t;

/**
 * @brief Decoded ISO-TP protocol control information.
 */
typedef struct
{
    isotp_pci_type_t type;

    /** Offset of message payload bytes inside the CAN payload. */
    uint8_t payload_offset;

    /**
     * Single Frame payload length, First Frame total message length,
     * or Consecutive Frame payload length.
     */
    uint32_t payload_length;

    uint8_t sequence_number;
    isotp_flow_status_t flow_status;
    uint8_t block_size;
    uint8_t st_min;

} isotp_pci_t;

/**
 * @brief Decode ISO-TP protocol control information from a CAN payload.
 *
 * Normal addressing is used. Padding bytes are permitted after the
 * meaningful Single Frame or Flow Control content.
 */
esp_err_t isotp_protocol_decode(
    const uint8_t *frame_data,
    size_t frame_data_length,
    isotp_pci_t *pci
);

/**
 * @brief Encode a Single Frame header.
 *
 * The function selects the one-byte Classical CAN header or the
 * two-byte CAN FD escape header. The caller copies payload bytes at
 * the returned offset.
 */
esp_err_t isotp_protocol_encode_single_frame(
    size_t payload_length,
    size_t frame_capacity,
    uint8_t *frame_data,
    uint8_t *payload_offset
);

/**
 * @brief Encode a First Frame header.
 *
 * A 12-bit message length is used up to 4095 bytes. Larger messages
 * use the six-byte extended-length header.
 */
esp_err_t isotp_protocol_encode_first_frame(
    uint32_t message_length,
    size_t frame_capacity,
    uint8_t *frame_data,
    uint8_t *payload_offset
);

/**
 * @brief Encode a Consecutive Frame header.
 */
esp_err_t isotp_protocol_encode_consecutive_frame(
    uint8_t sequence_number,
    uint8_t *frame_data,
    uint8_t *payload_offset
);

/**
 * @brief Encode a Flow Control header.
 */
esp_err_t isotp_protocol_encode_flow_control(
    isotp_flow_status_t status,
    uint8_t block_size,
    uint8_t st_min,
    uint8_t *frame_data,
    uint8_t *payload_offset
);

/**
 * @brief Convert an ISO-TP STmin byte to microseconds.
 */
esp_err_t isotp_protocol_st_min_to_us(
    uint8_t st_min,
    uint32_t *microseconds
);

#ifdef __cplusplus
}
#endif
