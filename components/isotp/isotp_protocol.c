/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "isotp_protocol.h"

#include <stdbool.h>
#include <string.h>

#define ISOTP_PCI_TYPE_SHIFT                   (4U)
#define ISOTP_PCI_VALUE_MASK                   (0x0FU)
#define ISOTP_SINGLE_FRAME_CLASSIC_MAX_LENGTH  (7U)
#define ISOTP_SINGLE_FRAME_FD_MAX_LENGTH       (62U)
#define ISOTP_FIRST_FRAME_HEADER_SIZE          (2U)
#define ISOTP_FIRST_FRAME_EXTENDED_HEADER_SIZE (6U)
#define ISOTP_CONSECUTIVE_FRAME_HEADER_SIZE    (1U)
#define ISOTP_FLOW_CONTROL_HEADER_SIZE         (3U)

static bool isotp_protocol_frame_length_valid(
    size_t frame_data_length
)
{
    return
        (frame_data_length > 0U) &&
        (frame_data_length <= ISOTP_FD_FRAME_DATA_MAX_LENGTH);
}

static bool isotp_protocol_st_min_valid(
    uint8_t st_min
)
{
    return
        (st_min <= 0x7FU) ||
        ((st_min >= 0xF1U) && (st_min <= 0xF9U));
}

esp_err_t isotp_protocol_decode(
    const uint8_t *frame_data,
    size_t frame_data_length,
    isotp_pci_t *pci
)
{
    if ((frame_data == NULL) ||
        (pci == NULL) ||
        !isotp_protocol_frame_length_valid(
            frame_data_length
        )) {

        return ESP_ERR_INVALID_ARG;
    }

    memset(
        pci,
        0,
        sizeof(*pci)
    );

    const uint8_t type =
        frame_data[0] >> ISOTP_PCI_TYPE_SHIFT;

    switch (type) {
        case ISOTP_PCI_SINGLE_FRAME: {
            uint32_t payload_length =
                frame_data[0] & ISOTP_PCI_VALUE_MASK;
            uint8_t payload_offset = 1U;

            if ((frame_data_length >
                 ISOTP_CLASSIC_FRAME_DATA_LENGTH) &&
                (payload_length != 0U)) {

                return ESP_ERR_INVALID_RESPONSE;
            }

            if (payload_length == 0U) {
                if (frame_data_length < 2U) {
                    return ESP_ERR_INVALID_RESPONSE;
                }

                payload_length = frame_data[1];
                payload_offset = 2U;

                if ((payload_length <=
                     ISOTP_SINGLE_FRAME_CLASSIC_MAX_LENGTH) ||
                    (payload_length >
                     ISOTP_SINGLE_FRAME_FD_MAX_LENGTH)) {

                    return ESP_ERR_INVALID_RESPONSE;
                }
            }

            if (payload_length >
                frame_data_length - payload_offset) {

                return ESP_ERR_INVALID_RESPONSE;
            }

            pci->type = ISOTP_PCI_SINGLE_FRAME;
            pci->payload_offset = payload_offset;
            pci->payload_length = payload_length;
            break;
        }

        case ISOTP_PCI_FIRST_FRAME: {
            if (frame_data_length <
                ISOTP_FIRST_FRAME_HEADER_SIZE) {

                return ESP_ERR_INVALID_RESPONSE;
            }

            uint32_t message_length =
                ((uint32_t)(frame_data[0] &
                            ISOTP_PCI_VALUE_MASK) << 8U) |
                frame_data[1];
            uint8_t payload_offset =
                ISOTP_FIRST_FRAME_HEADER_SIZE;

            if (message_length == 0U) {
                if (frame_data_length <
                    ISOTP_FIRST_FRAME_EXTENDED_HEADER_SIZE) {

                    return ESP_ERR_INVALID_RESPONSE;
                }

                message_length =
                    ((uint32_t)frame_data[2] << 24U) |
                    ((uint32_t)frame_data[3] << 16U) |
                    ((uint32_t)frame_data[4] << 8U) |
                    frame_data[5];
                payload_offset =
                    ISOTP_FIRST_FRAME_EXTENDED_HEADER_SIZE;

                if (message_length <=
                    ISOTP_MESSAGE_LENGTH_12_BIT_MAX) {

                    return ESP_ERR_INVALID_RESPONSE;
                }
            }

            const uint32_t single_frame_capacity =
                frame_data_length <=
                ISOTP_CLASSIC_FRAME_DATA_LENGTH
                    ? ISOTP_SINGLE_FRAME_CLASSIC_MAX_LENGTH
                    : ISOTP_SINGLE_FRAME_FD_MAX_LENGTH;

            if ((message_length <= single_frame_capacity) ||
                (message_length <=
                 frame_data_length - payload_offset)) {

                return ESP_ERR_INVALID_RESPONSE;
            }

            pci->type = ISOTP_PCI_FIRST_FRAME;
            pci->payload_offset = payload_offset;
            pci->payload_length = message_length;
            break;
        }

        case ISOTP_PCI_CONSECUTIVE_FRAME:
            if (frame_data_length <=
                ISOTP_CONSECUTIVE_FRAME_HEADER_SIZE) {

                return ESP_ERR_INVALID_RESPONSE;
            }

            pci->type = ISOTP_PCI_CONSECUTIVE_FRAME;
            pci->payload_offset =
                ISOTP_CONSECUTIVE_FRAME_HEADER_SIZE;
            pci->payload_length =
                frame_data_length -
                ISOTP_CONSECUTIVE_FRAME_HEADER_SIZE;
            pci->sequence_number =
                frame_data[0] & ISOTP_PCI_VALUE_MASK;
            break;

        case ISOTP_PCI_FLOW_CONTROL:
            if (frame_data_length <
                ISOTP_FLOW_CONTROL_HEADER_SIZE) {

                return ESP_ERR_INVALID_RESPONSE;
            }

            pci->flow_status =
                (isotp_flow_status_t)(
                    frame_data[0] & ISOTP_PCI_VALUE_MASK
                );

            if ((pci->flow_status >
                 ISOTP_FLOW_STATUS_OVERFLOW) ||
                !isotp_protocol_st_min_valid(
                    frame_data[2]
                )) {

                return ESP_ERR_INVALID_RESPONSE;
            }

            pci->type = ISOTP_PCI_FLOW_CONTROL;
            pci->payload_offset =
                ISOTP_FLOW_CONTROL_HEADER_SIZE;
            pci->block_size = frame_data[1];
            pci->st_min = frame_data[2];
            break;

        default:
            return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

esp_err_t isotp_protocol_encode_single_frame(
    size_t payload_length,
    size_t frame_capacity,
    uint8_t *frame_data,
    uint8_t *payload_offset
)
{
    if ((frame_data == NULL) ||
        (payload_offset == NULL) ||
        !isotp_protocol_frame_length_valid(
            frame_capacity
        )) {

        return ESP_ERR_INVALID_ARG;
    }

    if ((payload_length > 0U) &&
        (payload_length <=
         ISOTP_SINGLE_FRAME_CLASSIC_MAX_LENGTH) &&
        (frame_capacity <=
         ISOTP_CLASSIC_FRAME_DATA_LENGTH) &&
        (payload_length + 1U <= frame_capacity)) {

        frame_data[0] = (uint8_t)payload_length;
        *payload_offset = 1U;

        return ESP_OK;
    }

    if ((frame_capacity <=
         ISOTP_CLASSIC_FRAME_DATA_LENGTH) ||
        (payload_length == 0U) ||
        (payload_length >
         ISOTP_SINGLE_FRAME_FD_MAX_LENGTH) ||
        (payload_length + 2U > frame_capacity)) {

        return ESP_ERR_INVALID_SIZE;
    }

    frame_data[0] = 0U;
    frame_data[1] = (uint8_t)payload_length;
    *payload_offset = 2U;

    return ESP_OK;
}

esp_err_t isotp_protocol_encode_first_frame(
    uint32_t message_length,
    size_t frame_capacity,
    uint8_t *frame_data,
    uint8_t *payload_offset
)
{
    if ((frame_data == NULL) ||
        (payload_offset == NULL) ||
        !isotp_protocol_frame_length_valid(
            frame_capacity
        )) {

        return ESP_ERR_INVALID_ARG;
    }

    const size_t single_frame_capacity =
        frame_capacity <= ISOTP_CLASSIC_FRAME_DATA_LENGTH
            ? frame_capacity - 1U
            : frame_capacity - 2U;

    if (message_length <= single_frame_capacity) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (message_length <=
        ISOTP_MESSAGE_LENGTH_12_BIT_MAX) {

        if (frame_capacity <=
            ISOTP_FIRST_FRAME_HEADER_SIZE) {

            return ESP_ERR_INVALID_SIZE;
        }

        frame_data[0] =
            (uint8_t)(
                (ISOTP_PCI_FIRST_FRAME <<
                 ISOTP_PCI_TYPE_SHIFT) |
                ((message_length >> 8U) &
                 ISOTP_PCI_VALUE_MASK)
            );
        frame_data[1] = (uint8_t)message_length;
        *payload_offset =
            ISOTP_FIRST_FRAME_HEADER_SIZE;

        return ESP_OK;
    }

    if (frame_capacity <=
        ISOTP_FIRST_FRAME_EXTENDED_HEADER_SIZE) {

        return ESP_ERR_INVALID_SIZE;
    }

    frame_data[0] =
        ISOTP_PCI_FIRST_FRAME <<
        ISOTP_PCI_TYPE_SHIFT;
    frame_data[1] = 0U;
    frame_data[2] = (uint8_t)(message_length >> 24U);
    frame_data[3] = (uint8_t)(message_length >> 16U);
    frame_data[4] = (uint8_t)(message_length >> 8U);
    frame_data[5] = (uint8_t)message_length;
    *payload_offset =
        ISOTP_FIRST_FRAME_EXTENDED_HEADER_SIZE;

    return ESP_OK;
}

esp_err_t isotp_protocol_encode_consecutive_frame(
    uint8_t sequence_number,
    uint8_t *frame_data,
    uint8_t *payload_offset
)
{
    if ((frame_data == NULL) ||
        (payload_offset == NULL) ||
        (sequence_number >
         ISOTP_SEQUENCE_NUMBER_MAX)) {

        return ESP_ERR_INVALID_ARG;
    }

    frame_data[0] =
        (uint8_t)(
            (ISOTP_PCI_CONSECUTIVE_FRAME <<
             ISOTP_PCI_TYPE_SHIFT) |
            sequence_number
        );
    *payload_offset =
        ISOTP_CONSECUTIVE_FRAME_HEADER_SIZE;

    return ESP_OK;
}

esp_err_t isotp_protocol_encode_flow_control(
    isotp_flow_status_t status,
    uint8_t block_size,
    uint8_t st_min,
    uint8_t *frame_data,
    uint8_t *payload_offset
)
{
    if ((frame_data == NULL) ||
        (payload_offset == NULL) ||
        (status < ISOTP_FLOW_STATUS_CONTINUE_TO_SEND) ||
        (status > ISOTP_FLOW_STATUS_OVERFLOW) ||
        !isotp_protocol_st_min_valid(
            st_min
        )) {

        return ESP_ERR_INVALID_ARG;
    }

    frame_data[0] =
        (uint8_t)(
            (ISOTP_PCI_FLOW_CONTROL <<
             ISOTP_PCI_TYPE_SHIFT) |
            status
        );
    frame_data[1] = block_size;
    frame_data[2] = st_min;
    *payload_offset =
        ISOTP_FLOW_CONTROL_HEADER_SIZE;

    return ESP_OK;
}

esp_err_t isotp_protocol_st_min_to_us(
    uint8_t st_min,
    uint32_t *microseconds
)
{
    if ((microseconds == NULL) ||
        !isotp_protocol_st_min_valid(
            st_min
        )) {

        return ESP_ERR_INVALID_ARG;
    }

    *microseconds =
        st_min <= 0x7FU
            ? (uint32_t)st_min * 1000U
            : (uint32_t)(st_min - 0xF0U) * 100U;

    return ESP_OK;
}
