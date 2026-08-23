#include "web_can_protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(
    sizeof(esp_err_t) == sizeof(int32_t),
    "Web CAN protocol requires a 32-bit esp_err_t"
);

_Static_assert(
    CAN_FRAME_FLAGS_MASK <= UINT8_MAX,
    "CAN frame flags must fit into the binary protocol flag byte"
);

static void web_can_protocol_write_u16_le(
    uint8_t *destination,
    uint16_t value
)
{
    destination[0] =
        (uint8_t)(value & 0xFFU);

    destination[1] =
        (uint8_t)((value >> 8U) & 0xFFU);
}

static void web_can_protocol_write_u32_le(
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

static void web_can_protocol_write_u64_le(
    uint8_t *destination,
    uint64_t value
)
{
    for (uint32_t index = 0U;
         index < 8U;
         ++index) {

        destination[index] =
            (uint8_t)(
                (value >> (index * 8U)) &
                0xFFU
            );
    }
}

static bool web_can_protocol_event_type_valid(
    can_event_type_t type
)
{
    return
        (type >= CAN_EVENT_RX) &&
        (type < CAN_EVENT_TYPE_COUNT);
}

static bool web_can_protocol_direction_valid(
    can_frame_direction_t direction
)
{
    return
        (direction == CAN_FRAME_DIRECTION_RX) ||
        (direction == CAN_FRAME_DIRECTION_TX);
}

static bool web_can_protocol_timestamp_source_valid(
    can_timestamp_source_t source
)
{
    return
        (source >= CAN_TIMESTAMP_SOURCE_NONE) &&
        (source <= CAN_TIMESTAMP_SOURCE_HARDWARE);
}

static esp_err_t web_can_protocol_validate_event(
    const can_event_t *event
)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!web_can_protocol_event_type_valid(
            event->type
        )) {

        return ESP_ERR_INVALID_ARG;
    }

    if (!web_can_protocol_direction_valid(
            event->direction
        )) {

        return ESP_ERR_INVALID_ARG;
    }

    switch (event->type) {
        case CAN_EVENT_RX:
            if ((event->direction != CAN_FRAME_DIRECTION_RX) ||
                (event->transaction_id != CAN_TRANSACTION_ID_NONE)) {

                return ESP_ERR_INVALID_ARG;
            }

            break;

        case CAN_EVENT_TX_QUEUED:
        case CAN_EVENT_TX_COMPLETED:
        case CAN_EVENT_TX_FAILED:
        case CAN_EVENT_TX_ABORTED:
            if ((event->direction != CAN_FRAME_DIRECTION_TX) ||
                (event->transaction_id == CAN_TRANSACTION_ID_NONE)) {

                return ESP_ERR_INVALID_ARG;
            }

            break;

        default:
            return ESP_ERR_INVALID_ARG;
    }

    if (!web_can_protocol_timestamp_source_valid(
            event->frame.timestamp_source
        )) {

        return ESP_ERR_INVALID_ARG;
    }

    if ((event->frame.flags &
         ~((uint32_t)CAN_FRAME_FLAGS_MASK)) != 0U) {

        return ESP_ERR_INVALID_ARG;
    }

    return can_frame_validate(
        &event->frame
    );
}

size_t web_can_protocol_event_size(
    const can_event_t *event
)
{
    if (web_can_protocol_validate_event(
            event
        ) != ESP_OK) {

        return 0U;
    }

    return
        WEB_CAN_EVENT_HEADER_SIZE +
        event->frame.data_length;
}

esp_err_t web_can_protocol_encode_event(
    const can_event_t *event,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
)
{
    if ((event == NULL) ||
        (buffer == NULL) ||
        (encoded_size == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *encoded_size = 0U;

    const esp_err_t validation_result =
        web_can_protocol_validate_event(
            event
        );

    if (validation_result != ESP_OK) {
        return validation_result;
    }

    const size_t required_size =
        WEB_CAN_EVENT_HEADER_SIZE +
        event->frame.data_length;

    if (capacity < required_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    /*
     * Event header:
     *
     *  0: event type
     *  1: bus
     *  2: direction
     *  3: frame flags
     *  4: DLC
     *  5: payload length
     *  6: timestamp source
     *  7: reserved
     *  8: event sequence
     * 12: transaction ID
     * 16: native sequence
     * 20: CAN identifier
     * 24: esp_err_t result
     * 28: timestamp in microseconds
     * 36: reserved
     */
    memset(
        buffer,
        0,
        WEB_CAN_EVENT_HEADER_SIZE
    );

    buffer[0] =
        (uint8_t)event->type;

    buffer[1] =
        (uint8_t)event->frame.bus;

    buffer[2] =
        (uint8_t)event->direction;

    buffer[3] =
        (uint8_t)event->frame.flags;

    buffer[4] =
        event->frame.dlc;

    buffer[5] =
        event->frame.data_length;

    buffer[6] =
        (uint8_t)event->frame.timestamp_source;

    web_can_protocol_write_u32_le(
        &buffer[8],
        event->event_sequence
    );

    web_can_protocol_write_u32_le(
        &buffer[12],
        event->transaction_id
    );

    web_can_protocol_write_u32_le(
        &buffer[16],
        event->native_sequence
    );

    web_can_protocol_write_u32_le(
        &buffer[20],
        event->frame.identifier
    );

    web_can_protocol_write_u32_le(
        &buffer[24],
        (uint32_t)(int32_t)event->result
    );

    web_can_protocol_write_u64_le(
        &buffer[28],
        event->frame.timestamp_us
    );

    if (event->frame.data_length > 0U) {
        memcpy(
            &buffer[WEB_CAN_EVENT_HEADER_SIZE],
            event->frame.data,
            event->frame.data_length
        );
    }

    *encoded_size =
        required_size;

    return ESP_OK;
}

esp_err_t web_can_protocol_write_batch_header(
    uint8_t *buffer,
    size_t capacity,
    uint16_t event_count,
    size_t payload_size
)
{
    if (buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (event_count == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (capacity < WEB_CAN_BATCH_HEADER_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

#if SIZE_MAX > UINT32_MAX
        if (payload_size > UINT32_MAX) {
            return ESP_ERR_INVALID_SIZE;
        }
#endif

    memset(
        buffer,
        0,
        WEB_CAN_BATCH_HEADER_SIZE
    );

    /*
     * Batch header:
     *
     * 0: protocol version
     * 1: message type
     * 2: event count
     * 4: combined event-record size
     */
    buffer[0] =
        WEB_CAN_PROTOCOL_VERSION;

    buffer[1] =
        (uint8_t)WEB_CAN_MESSAGE_EVENT_BATCH;

    web_can_protocol_write_u16_le(
        &buffer[2],
        event_count
    );

    web_can_protocol_write_u32_le(
        &buffer[4],
        (uint32_t)payload_size
    );

    return ESP_OK;
}
