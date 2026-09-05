/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "can_frame.h"

#include <stddef.h>

static const uint8_t s_can_fd_dlc_lengths[] = {
    0U,
    1U,
    2U,
    3U,
    4U,
    5U,
    6U,
    7U,
    8U,
    12U,
    16U,
    20U,
    24U,
    32U,
    48U,
    64U,
};

_Static_assert(
    sizeof(s_can_fd_dlc_lengths) /
    sizeof(s_can_fd_dlc_lengths[0]) == 16U,
    "CAN FD DLC table must contain 16 entries"
);

esp_err_t can_frame_dlc_to_length(
    uint8_t dlc,
    bool fd,
    uint8_t *data_length
)
{
    if (data_length == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *data_length = 0U;

    if (dlc > 15U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (fd) {
        *data_length =
            s_can_fd_dlc_lengths[dlc];

        return ESP_OK;
    }

    /*
     * Classical CAN preserves raw DLC values from 9 through 15 for
     * monitoring and logging, but these values still represent no more
     * than eight payload bytes.
     */
    *data_length =
        (dlc <=
         CAN_FRAME_CLASSIC_DATA_MAX_LENGTH)
            ? dlc
            : CAN_FRAME_CLASSIC_DATA_MAX_LENGTH;

    return ESP_OK;
}

esp_err_t can_frame_length_to_dlc(
    uint8_t data_length,
    bool fd,
    uint8_t *dlc,
    uint8_t *encoded_length
)
{
    if ((dlc == NULL) ||
        (encoded_length == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *dlc = 0U;
    *encoded_length = 0U;

    if (!fd) {
        if (data_length >
            CAN_FRAME_CLASSIC_DATA_MAX_LENGTH) {

            return ESP_ERR_INVALID_SIZE;
        }

        *dlc = data_length;
        *encoded_length = data_length;

        return ESP_OK;
    }

    if (data_length >
        CAN_FRAME_FD_DATA_MAX_LENGTH) {

        return ESP_ERR_INVALID_SIZE;
    }

    for (uint8_t candidate_dlc = 0U;
         candidate_dlc <
            (uint8_t)(
                sizeof(s_can_fd_dlc_lengths) /
                sizeof(s_can_fd_dlc_lengths[0])
            );
         ++candidate_dlc) {

        const uint8_t candidate_length =
            s_can_fd_dlc_lengths[
                candidate_dlc
            ];

        if (data_length <=
            candidate_length) {

            *dlc =
                candidate_dlc;

            *encoded_length =
                candidate_length;

            return ESP_OK;
        }
    }

    /*
     * The size check above should make this path unreachable.
     */
    return ESP_ERR_INVALID_SIZE;
}

esp_err_t can_frame_validate(
    const can_frame_t *frame
)
{
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((frame->bus < CAN_BUS_PRIMARY) ||
        (frame->bus >= CAN_BUS_COUNT)) {

        return ESP_ERR_INVALID_ARG;
    }

    if ((frame->timestamp_source <
         CAN_TIMESTAMP_SOURCE_NONE) ||
        (frame->timestamp_source >
         CAN_TIMESTAMP_SOURCE_HARDWARE)) {

        return ESP_ERR_INVALID_ARG;
    }

    if ((frame->flags &
         ~((uint32_t)CAN_FRAME_FLAGS_MASK)) != 0U) {

        return ESP_ERR_INVALID_ARG;
    }

    const bool extended =
        (frame->flags &
         CAN_FRAME_FLAG_EXTENDED_ID) != 0U;

    const bool remote =
        (frame->flags &
         CAN_FRAME_FLAG_REMOTE) != 0U;

    const bool fd =
        (frame->flags &
         CAN_FRAME_FLAG_FD) != 0U;

    const bool brs =
        (frame->flags &
         CAN_FRAME_FLAG_BRS) != 0U;

    const bool esi =
        (frame->flags &
         CAN_FRAME_FLAG_ESI) != 0U;

    if (extended) {
        if (frame->identifier >
            CAN_FRAME_EXTENDED_ID_MAX) {

            return ESP_ERR_INVALID_ARG;
        }
    } else {
        if (frame->identifier >
            CAN_FRAME_STANDARD_ID_MAX) {

            return ESP_ERR_INVALID_ARG;
        }
    }

    if (frame->dlc > 15U) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * The primary ESP32-S3 TWAI interface supports Classical CAN only.
     * CAN FD frames must use the secondary MCP2518FD interface.
     */
    if (fd &&
        (frame->bus == CAN_BUS_PRIMARY)) {

        return ESP_ERR_INVALID_ARG;
    }

    /*
     * Remote frames exist only in Classical CAN. CAN FD removed the
     * Remote Transmission Request frame type.
     */
    if (remote &&
        fd) {

        return ESP_ERR_INVALID_ARG;
    }

    /*
     * BRS and ESI are meaningful only for CAN FD frames.
     */
    if (!fd &&
        (brs || esi)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (remote) {
        /*
         * A remote frame may retain its requested length in DLC but
         * does not contain payload bytes.
         */
        if (frame->data_length != 0U) {
            return ESP_ERR_INVALID_SIZE;
        }

        return ESP_OK;
    }

    uint8_t expected_length = 0U;

    const esp_err_t length_result =
        can_frame_dlc_to_length(
            frame->dlc,
            fd,
            &expected_length
        );

    if (length_result != ESP_OK) {
        return length_result;
    }

    if (fd) {
        if (frame->data_length >
            CAN_FRAME_FD_DATA_MAX_LENGTH) {

            return ESP_ERR_INVALID_SIZE;
        }
    } else {
        if (frame->data_length >
            CAN_FRAME_CLASSIC_DATA_MAX_LENGTH) {

            return ESP_ERR_INVALID_SIZE;
        }
    }

    /*
     * The shared model stores the number of bytes physically carried
     * by the frame. CAN FD callers must therefore pad non-representable
     * application payload lengths to the length encoded by the DLC.
     */
    if (frame->data_length !=
        expected_length) {

        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}
