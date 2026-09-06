/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "can_logger_binary_format.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CAN_LOGGER_SCL_ENDIAN_MARKER  (0x12345678UL)

#define CAN_LOGGER_SCL_FILE_FLAGS_NONE  (0U)

_Static_assert(
    CAN_LOGGER_SCL_EVENT_MAX_SIZE <= UINT16_MAX,
    "SCL event record size must fit into uint16_t"
);

_Static_assert(
    sizeof(esp_err_t) <= sizeof(uint32_t),
    "SCL result field cannot represent esp_err_t"
);

static void can_logger_scl_write_u16_le(
    uint8_t *destination,
    uint16_t value
)
{
    destination[0] =
        (uint8_t)(value & 0xFFU);

    destination[1] =
        (uint8_t)((value >> 8U) & 0xFFU);
}

static void can_logger_scl_write_u32_le(
    uint8_t *destination,
    uint32_t value
)
{
    destination[0] =
        (uint8_t)(value & 0xFFU);

    destination[1] =
        (uint8_t)((value >> 8U) & 0xFFU);

    destination[2] =
        (uint8_t)((value >> 16U) & 0xFFU);

    destination[3] =
        (uint8_t)((value >> 24U) & 0xFFU);
}

static void can_logger_scl_write_u64_le(
    uint8_t *destination,
    uint64_t value
)
{
    for (size_t index = 0U;
         index < sizeof(value);
         ++index) {

        destination[index] =
            (uint8_t)(
                (value >> (index * 8U)) &
                UINT64_C(0xFF)
            );
    }
}

static bool can_logger_scl_event_valid(
    const can_event_t *event
)
{
    if (event == NULL) {
        return false;
    }

    if ((uint32_t)event->type >=
        (uint32_t)CAN_EVENT_TYPE_COUNT) {

        return false;
    }

    if ((uint32_t)event->direction >
        (uint32_t)CAN_FRAME_DIRECTION_TX) {

        return false;
    }

    if ((uint32_t)event->frame.timestamp_source >
        (uint32_t)CAN_TIMESTAMP_SOURCE_HARDWARE) {

        return false;
    }

    if (can_frame_validate(
            &event->frame
        ) != ESP_OK) {

        return false;
    }

    /*
     * Received events must describe received frames. Transmission
     * lifecycle events must describe transmitted frames.
     */
    if ((event->type == CAN_EVENT_RX) &&
        (event->direction != CAN_FRAME_DIRECTION_RX)) {

        return false;
    }

    if ((event->type != CAN_EVENT_RX) &&
        (event->direction != CAN_FRAME_DIRECTION_TX)) {

        return false;
    }

    /*
     * Received frames do not belong to a transmission transaction.
     */
    if ((event->type == CAN_EVENT_RX) &&
        (event->transaction_id != CAN_TRANSACTION_ID_NONE)) {

        return false;
    }

    /*
     * Transmission lifecycle events require a transaction identifier.
     */
    if ((event->type != CAN_EVENT_RX) &&
        (event->transaction_id == CAN_TRANSACTION_ID_NONE)) {

        return false;
    }

    return true;
}

esp_err_t can_logger_scl_encode_file_header(
    uint8_t *buffer,
    size_t capacity,
    uint64_t start_unix_us,
    uint64_t start_monotonic_us,
    size_t *encoded_size
)
{
    if (encoded_size != NULL) {
        *encoded_size = 0U;
    }

    if ((buffer == NULL) ||
        (encoded_size == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (capacity <
        CAN_LOGGER_SCL_FILE_HEADER_SIZE) {

        return ESP_ERR_INVALID_SIZE;
    }

    memset(
        buffer,
        0,
        CAN_LOGGER_SCL_FILE_HEADER_SIZE
    );

    buffer[0] = CAN_LOGGER_SCL_MAGIC_0;
    buffer[1] = CAN_LOGGER_SCL_MAGIC_1;
    buffer[2] = CAN_LOGGER_SCL_MAGIC_2;
    buffer[3] = CAN_LOGGER_SCL_MAGIC_3;

    can_logger_scl_write_u16_le(
        &buffer[4],
        CAN_LOGGER_SCL_VERSION
    );

    can_logger_scl_write_u16_le(
        &buffer[6],
        CAN_LOGGER_SCL_FILE_HEADER_SIZE
    );

    can_logger_scl_write_u32_le(
        &buffer[8],
        CAN_LOGGER_SCL_ENDIAN_MARKER
    );

    can_logger_scl_write_u32_le(
        &buffer[12],
        CAN_LOGGER_SCL_FILE_FLAGS_NONE
    );

    can_logger_scl_write_u64_le(
        &buffer[16],
        start_unix_us
    );

    can_logger_scl_write_u64_le(
        &buffer[24],
        start_monotonic_us
    );

    *encoded_size =
        CAN_LOGGER_SCL_FILE_HEADER_SIZE;

    return ESP_OK;
}

esp_err_t can_logger_scl_encode_event(
    const can_event_t *event,
    uint64_t captured_at_us,
    uint64_t session_started_us,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if (encoded_size != NULL) {
        *encoded_size = 0U;
    }

    if ((event == NULL) ||
        (buffer == NULL) ||
        (encoded_size == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (!can_logger_scl_event_valid(event)) {
        return ESP_ERR_INVALID_ARG;
    }

    const can_frame_t *frame =
        &event->frame;

    const size_t payload_size =
        frame->data_length;

    const size_t record_size =
        CAN_LOGGER_SCL_EVENT_HEADER_SIZE +
        payload_size;

    if ((record_size >
         CAN_LOGGER_SCL_EVENT_MAX_SIZE) ||
        (record_size > UINT16_MAX)) {

        return ESP_ERR_INVALID_SIZE;
    }

    if (capacity < record_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    const uint64_t relative_capture_time_us =
        captured_at_us >= session_started_us
            ? captured_at_us -
              session_started_us
            : 0U;

    memset(
        buffer,
        0,
        CAN_LOGGER_SCL_EVENT_HEADER_SIZE
    );

    can_logger_scl_write_u16_le(
        &buffer[0],
        (uint16_t)record_size
    );

    buffer[2] =
        (uint8_t)CAN_LOGGER_SCL_RECORD_CAN_EVENT;

    buffer[3] =
        (uint8_t)event->type;

    buffer[4] =
        (uint8_t)frame->bus;

    buffer[5] =
        (uint8_t)event->direction;

    buffer[6] =
        (uint8_t)frame->timestamp_source;

    buffer[7] =
        frame->dlc;

    buffer[8] =
        frame->data_length;

    /*
     * Bytes 9 through 11 are reserved.
     */

    can_logger_scl_write_u32_le(
        &buffer[12],
        frame->flags
    );

    can_logger_scl_write_u32_le(
        &buffer[16],
        frame->identifier
    );

    can_logger_scl_write_u32_le(
        &buffer[20],
        event->event_sequence
    );

    can_logger_scl_write_u32_le(
        &buffer[24],
        event->transaction_id
    );

    can_logger_scl_write_u32_le(
        &buffer[28],
        event->native_sequence
    );

    /*
     * Preserve the signed esp_err_t bit pattern in a fixed-width
     * 32-bit field.
     */
    can_logger_scl_write_u32_le(
        &buffer[32],
        (uint32_t)(int32_t)event->result
    );

    /*
     * Bytes 36 through 39 are reserved.
     */

    can_logger_scl_write_u64_le(
        &buffer[40],
        frame->timestamp_us
    );

    can_logger_scl_write_u64_le(
        &buffer[48],
        relative_capture_time_us
    );

    if (payload_size > 0U) {
        memcpy(
            &buffer[
                CAN_LOGGER_SCL_EVENT_HEADER_SIZE
            ],
            frame->data,
            payload_size
        );
    }

    *encoded_size =
        record_size;

    return ESP_OK;
}
