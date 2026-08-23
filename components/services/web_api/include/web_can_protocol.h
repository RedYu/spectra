#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file web_can_protocol.h
 * @brief Binary WebSocket protocol for CAN events.
 *
 * All multibyte integer fields use little-endian byte order.
 */

#define WEB_CAN_PROTOCOL_VERSION              (1U)

#define WEB_CAN_BATCH_HEADER_SIZE             (8U)
#define WEB_CAN_EVENT_HEADER_SIZE             (40U)

#define WEB_CAN_EVENT_MAX_ENCODED_SIZE        \
    (                                         \
        WEB_CAN_EVENT_HEADER_SIZE +           \
        CAN_FRAME_FD_DATA_MAX_LENGTH          \
    )

/**
 * @brief Binary WebSocket message type.
 */
typedef enum
{
    WEB_CAN_MESSAGE_EVENT_BATCH = 1,

} web_can_message_type_t;

/**
 * @brief Calculate the encoded size of one CAN event.
 *
 * @param[in] event CAN event.
 *
 * @return Encoded size in bytes, or zero if event is NULL or invalid.
 */
size_t web_can_protocol_event_size(
    const can_event_t *event
);

/**
 * @brief Encode one CAN event.
 *
 * The function writes only the event record. It does not write a batch
 * header.
 *
 * @param[in] event CAN event to encode.
 * @param[out] buffer Destination buffer.
 * @param[in] capacity Destination capacity.
 * @param[out] encoded_size Number of bytes written.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid pointers
 * or event fields, or ESP_ERR_INVALID_SIZE when the destination buffer
 * is too small.
 */
esp_err_t web_can_protocol_encode_event(
    const can_event_t *event,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);

/**
 * @brief Write a CAN event-batch header.
 *
 * The event records must immediately follow this header.
 *
 * @param[out] buffer Destination buffer.
 * @param[in] capacity Destination capacity.
 * @param[in] event_count Number of event records in the batch.
 * @param[in] payload_size Combined size of all event records.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for an invalid event
 * count, or ESP_ERR_INVALID_SIZE if the destination is too small or
 * payload_size cannot be represented by the protocol.
 */
esp_err_t web_can_protocol_write_batch_header(
    uint8_t *buffer,
    size_t capacity,
    uint16_t event_count,
    size_t payload_size
);

#ifdef __cplusplus
}
#endif
