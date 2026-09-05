/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "can_mcp2518fd_frame_adapter.h"

#include <stddef.h>
#include <string.h>

_Static_assert(
    CAN_FD_MCP2518FD_CLASSIC_DATA_MAX_LENGTH ==
    CAN_FRAME_CLASSIC_DATA_MAX_LENGTH,
    "MCP2518FD and shared Classical CAN payload sizes must match"
);

_Static_assert(
    CAN_FD_MCP2518FD_DATA_MAX_LENGTH ==
    CAN_FRAME_FD_DATA_MAX_LENGTH,
    "MCP2518FD and shared CAN FD payload sizes must match"
);

_Static_assert(
    CAN_FD_MCP2518FD_STANDARD_ID_MAX ==
    CAN_FRAME_STANDARD_ID_MAX,
    "MCP2518FD and shared standard identifier limits must match"
);

_Static_assert(
    CAN_FD_MCP2518FD_EXTENDED_ID_MAX ==
    CAN_FRAME_EXTENDED_ID_MAX,
    "MCP2518FD and shared extended identifier limits must match"
);

static bool can_mcp2518fd_identifier_valid(
    const can_fd_mcp2518fd_frame_t *frame
)
{
    if (frame == NULL) {
        return false;
    }

    if (frame->extended) {
        return frame->identifier <=
               CAN_FD_MCP2518FD_EXTENDED_ID_MAX;
    }

    return frame->identifier <=
           CAN_FD_MCP2518FD_STANDARD_ID_MAX;
}

esp_err_t can_mcp2518fd_frame_to_common(
    const can_fd_mcp2518fd_frame_t *source,
    can_frame_t *destination
)
{
    if ((source == NULL) ||
        (destination == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    memset(
        destination,
        0,
        sizeof(*destination)
    );

    if (!can_mcp2518fd_identifier_valid(
            source
        )) {

        return ESP_ERR_INVALID_ARG;
    }

    if (source->data_length >
        CAN_FD_MCP2518FD_DATA_MAX_LENGTH) {

        return ESP_ERR_INVALID_SIZE;
    }

    if (!source->fd_frame &&
        (source->data_length >
         CAN_FD_MCP2518FD_CLASSIC_DATA_MAX_LENGTH)) {

        return ESP_ERR_INVALID_SIZE;
    }

    if (source->fd_frame &&
        source->remote) {

        return ESP_ERR_INVALID_ARG;
    }

    if (!source->fd_frame &&
        (source->bit_rate_switch ||
         source->error_state_indicator)) {

        return ESP_ERR_INVALID_ARG;
    }

    uint8_t dlc = 0U;
    uint8_t encoded_length = 0U;

    const esp_err_t dlc_result =
        can_frame_length_to_dlc(
            source->data_length,
            source->fd_frame,
            &dlc,
            &encoded_length
        );

    if (dlc_result != ESP_OK) {
        return dlc_result;
    }

    /*
     * The MCP2518FD driver accepts only payload lengths directly
     * representable by a CAN DLC. Do not silently pad malformed source
     * frames inside the adapter.
     */
    if (encoded_length !=
        source->data_length) {

        return ESP_ERR_INVALID_SIZE;
    }

    destination->bus =
        CAN_BUS_SECONDARY;

    destination->identifier =
        source->identifier;

    destination->dlc =
        dlc;

    destination->data_length =
        source->remote
            ? 0U
            : source->data_length;

    if (source->extended) {
        destination->flags |=
            CAN_FRAME_FLAG_EXTENDED_ID;
    }

    if (source->remote) {
        destination->flags |=
            CAN_FRAME_FLAG_REMOTE;
    }

    if (source->fd_frame) {
        destination->flags |=
            CAN_FRAME_FLAG_FD;
    }

    if (source->bit_rate_switch) {
        destination->flags |=
            CAN_FRAME_FLAG_BRS;
    }

    if (source->error_state_indicator) {
        destination->flags |=
            CAN_FRAME_FLAG_ESI;
    }

    if (!source->remote &&
        (source->data_length > 0U)) {

        memcpy(
            destination->data,
            source->data,
            source->data_length
        );
    }

    destination->timestamp_us =
        source->timestamp_valid
            ? (uint64_t)source->timestamp
            : 0ULL;

    destination->timestamp_source =
        source->timestamp_valid
            ? CAN_TIMESTAMP_SOURCE_HARDWARE
            : CAN_TIMESTAMP_SOURCE_NONE;

    return can_frame_validate(
        destination
    );
}

esp_err_t can_mcp2518fd_frame_from_common(
    const can_frame_t *source,
    can_fd_mcp2518fd_frame_t *destination
)
{
    if ((source == NULL) ||
        (destination == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    memset(
        destination,
        0,
        sizeof(*destination)
    );

    if (source->bus !=
        CAN_BUS_SECONDARY) {

        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t validation_result =
        can_frame_validate(
            source
        );

    if (validation_result != ESP_OK) {
        return validation_result;
    }

    const bool extended =
        (source->flags &
         CAN_FRAME_FLAG_EXTENDED_ID) != 0U;

    const bool remote =
        (source->flags &
         CAN_FRAME_FLAG_REMOTE) != 0U;

    const bool fd =
        (source->flags &
         CAN_FRAME_FLAG_FD) != 0U;

    const bool brs =
        (source->flags &
         CAN_FRAME_FLAG_BRS) != 0U;

    const bool esi =
        (source->flags &
         CAN_FRAME_FLAG_ESI) != 0U;

    /*
     * The MCP2518FD driver frame currently stores payload length but
     * not raw Classical CAN DLC separately. Raw DLC values from 9
     * through 15 therefore cannot be preserved.
     */
    if (!fd &&
        (source->dlc >
         CAN_FD_MCP2518FD_CLASSIC_DATA_MAX_LENGTH)) {

        return ESP_ERR_INVALID_SIZE;
    }

    destination->identifier =
        source->identifier;

    destination->extended =
        extended;

    destination->remote =
        remote;

    destination->fd_frame =
        fd;

    destination->bit_rate_switch =
        brs;

    destination->error_state_indicator =
        esi;

    if (remote) {
        /*
         * For Classical RTR frames the driver uses data_length as the
         * requested DLC.
         */
        destination->data_length =
            source->dlc;
    } else {
        destination->data_length =
            source->data_length;

        if (source->data_length > 0U) {
            memcpy(
                destination->data,
                source->data,
                source->data_length
            );
        }
    }

    /*
     * Hardware timestamps are assigned by MCP2518FD for received and
     * successfully transmitted frames.
     */
    destination->timestamp = 0U;
    destination->timestamp_valid = false;

    return ESP_OK;
}
