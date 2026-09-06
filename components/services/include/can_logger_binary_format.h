/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_LOGGER_SCL_VERSION            (1U)
#define CAN_LOGGER_SCL_FILE_HEADER_SIZE   (32U)
#define CAN_LOGGER_SCL_EVENT_HEADER_SIZE  (56U)

#define CAN_LOGGER_SCL_EVENT_MAX_SIZE \
    (CAN_LOGGER_SCL_EVENT_HEADER_SIZE + \
     CAN_FRAME_FD_DATA_MAX_LENGTH)

/*
 * "SCL1" stored exactly as four bytes.
 */
#define CAN_LOGGER_SCL_MAGIC_0  ('S')
#define CAN_LOGGER_SCL_MAGIC_1  ('C')
#define CAN_LOGGER_SCL_MAGIC_2  ('L')
#define CAN_LOGGER_SCL_MAGIC_3  ('1')

typedef enum
{
    CAN_LOGGER_SCL_RECORD_CAN_EVENT = 1,

} can_logger_scl_record_type_t;

/**
 * @brief Encode the fixed SCL file header.
 *
 * All multibyte fields are stored in little-endian byte order.
 *
 * @param[out] buffer Destination buffer.
 * @param[in] capacity Destination capacity.
 * @param[in] start_unix_us Unix time at recording start, or zero when
 * system time is unavailable.
 * @param[in] start_monotonic_us ESP monotonic time at recording start.
 * @param[out] encoded_size Number of bytes written.
 */
esp_err_t can_logger_scl_encode_file_header(
    uint8_t *buffer,
    size_t capacity,
    uint64_t start_unix_us,
    uint64_t start_monotonic_us,
    size_t *encoded_size
);

/**
 * @brief Encode one CAN event record.
 *
 * The payload immediately follows the fixed event header for every
 * non-remote frame with a non-zero data length. Remote frames contain
 * no payload.
 *
 * Both the original frame timestamp and the logger-relative capture
 * timestamp are preserved.
 *
 * @param[in] event CAN router event.
 * @param[in] captured_at_us Monotonic time at which the logger
 * subscriber captured the event.
 * @param[in] session_started_us Recording start monotonic time.
 * @param[out] buffer Destination buffer.
 * @param[in] capacity Destination capacity.
 * @param[out] encoded_size Number of bytes written.
 */
esp_err_t can_logger_scl_encode_event(
    const can_event_t *event,
    uint64_t captured_at_us,
    uint64_t session_started_us,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);

#ifdef __cplusplus
}
#endif
