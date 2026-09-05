<!--
SPDX-License-Identifier: GPL-3.0-only
Copyright (C) 2026 Yurii Ridkovets
-->

# Shared CAN frame and event model

This document specifies the hardware-independent CAN types declared in
`can_frame.h`. The model is shared by both CAN interfaces and by the router,
monitor, WebSocket stream, and future logger and diagnostic protocols.

## Purpose

Spectra uses two different controllers:

- Primary: ESP32-S3 TWAI, supporting Classical CAN;
- Secondary: MCP2518FD, supporting Classical CAN and CAN FD.

Their native structures are converted into one representation before frames
leave their interface services:

```text
TWAI native frame ---- TWAI adapter -----+
                                         v
                                    can_frame_t
                                         ^
MCP2518FD frame -- MCP2518FD adapter ----+
                                         |
                                         v
                                    can_event_t
                                         |
                                         v
                                      CAN router
```

Consumers must use the common model instead of depending on either hardware
driver.

## Limits and sentinel values

| Constant | Value | Meaning |
|---|---:|---|
| `CAN_FRAME_CLASSIC_DATA_MAX_LENGTH` | 8 | Maximum Classical CAN payload |
| `CAN_FRAME_FD_DATA_MAX_LENGTH` | 64 | Maximum CAN FD payload |
| `CAN_FRAME_STANDARD_ID_MAX` | `0x7FF` | Largest 11-bit identifier |
| `CAN_FRAME_EXTENDED_ID_MAX` | `0x1FFFFFFF` | Largest 29-bit identifier |
| `CAN_EVENT_SEQUENCE_INITIAL` | 1 | Initial router event sequence |
| `CAN_TRANSACTION_ID_NONE` | 0 | No application transaction assigned |
| `CAN_NATIVE_SEQUENCE_NONE` | `UINT32_MAX` | No driver-native TX sequence |

Sentinel values must be tested symbolically. Application code should use the
named constants instead of duplicating their numeric values.

## Bus identifier

`can_bus_id_t` identifies the logical interface:

| Value | Enumerator | Interface |
|---:|---|---|
| 0 | `CAN_BUS_PRIMARY` | ESP32-S3 TWAI, Classical CAN only |
| 1 | `CAN_BUS_SECONDARY` | MCP2518FD, Classical CAN and CAN FD |

`CAN_BUS_COUNT` is a boundary value and is not a valid bus identifier.

The `bus` field means source bus for received frames and destination bus for
transmit requests.

## Frame direction

`can_frame_direction_t` has two values:

| Value | Enumerator | Meaning |
|---:|---|---|
| 0 | `CAN_FRAME_DIRECTION_RX` | Received from the physical bus |
| 1 | `CAN_FRAME_DIRECTION_TX` | Submitted to or transmitted on the bus |

Direction belongs to `can_event_t`, because the same `can_frame_t` can describe
either a received frame or a transmit request.

## Frame flags

The `flags` field is a bit mask of `can_frame_flags_t` values:

| Mask | Enumerator | Meaning |
|---:|---|---|
| `0x01` | `CAN_FRAME_FLAG_EXTENDED_ID` | Use a 29-bit identifier instead of 11-bit |
| `0x02` | `CAN_FRAME_FLAG_REMOTE` | Classical CAN Remote Transmission Request |
| `0x04` | `CAN_FRAME_FLAG_FD` | CAN FD frame |
| `0x08` | `CAN_FRAME_FLAG_BRS` | Switch to the configured data bitrate |
| `0x10` | `CAN_FRAME_FLAG_ESI` | Transmitter is error-passive |

Only bits in `CAN_FRAME_FLAGS_MASK` are valid.

### Valid combinations

- `EXTENDED_ID` can be used with Classical CAN and CAN FD.
- `REMOTE` can be used only with Classical CAN.
- `FD` can be used only on `CAN_BUS_SECONDARY`.
- `BRS` and `ESI` are valid only when `FD` is set.
- CAN FD remote frames are invalid because CAN FD has no RTR frame type.

`BRS` indicates the frame-level request to switch bitrate. Whether it can be
transmitted also depends on the active Secondary interface configuration.

## `can_frame_t`

```c
typedef struct
{
    can_bus_id_t bus;
    uint32_t identifier;
    uint32_t flags;
    uint8_t dlc;
    uint8_t data_length;
    uint8_t data[64];
    uint64_t timestamp_us;
    can_timestamp_source_t timestamp_source;
} can_frame_t;
```

### `bus`

Selects Primary or Secondary. CAN FD is rejected on Primary even if the other
fields are otherwise valid.

### `identifier`

Contains only the CAN identifier, not controller-specific control bits:

- without `EXTENDED_ID`: `0x000` through `0x7FF`;
- with `EXTENDED_ID`: `0x00000000` through `0x1FFFFFFF`.

### `flags`

Describes identifier format and CAN frame properties. Unknown bits make the
frame invalid.

### `dlc`

Contains the raw four-bit Data Length Code, from 0 through 15. It is retained
separately from `data_length` so monitoring and logging can preserve the value
that appeared on the bus.

### `data_length`

Contains the actual number of payload bytes represented by the frame:

- 0 through 8 for Classical CAN data frames;
- one of the DLC-representable lengths from 0 through 64 for CAN FD;
- always 0 for remote frames.

### `data`

Contains up to 64 bytes. Only the first `data_length` bytes are meaningful.
Remote frames contain no payload.

For deterministic output and to avoid accidentally exposing stale memory,
constructors and adapters should normally zero-initialize the complete frame.

### `timestamp_us` and `timestamp_source`

The timestamp is expressed in microseconds. Its origin is explicit:

| Value | Enumerator | Meaning |
|---:|---|---|
| 0 | `CAN_TIMESTAMP_SOURCE_NONE` | Timestamp unavailable |
| 1 | `CAN_TIMESTAMP_SOURCE_SOFTWARE` | Assigned by ESP software |
| 2 | `CAN_TIMESTAMP_SOURCE_HARDWARE` | Assigned by the CAN controller |

When the source is `NONE`, consumers must ignore `timestamp_us`. Hardware and
software timestamps can have different time bases; values from different
sources must not be compared unless synchronization is defined separately.

## DLC versus payload length

DLC is a code, while `data_length` is a byte count.

### Classical CAN

| DLC | Stored payload length |
|---:|---:|
| 0–8 | Same as DLC |
| 9–15 | 8 bytes |

Raw Classical DLC values 9 through 15 may be preserved for received-frame
monitoring and logging, although they represent no more than eight data bytes.

### CAN FD

| DLC | Bytes | DLC | Bytes |
|---:|---:|---:|---:|
| 0 | 0 | 8 | 8 |
| 1 | 1 | 9 | 12 |
| 2 | 2 | 10 | 16 |
| 3 | 3 | 11 | 20 |
| 4 | 4 | 12 | 24 |
| 5 | 5 | 13 | 32 |
| 6 | 6 | 14 | 48 |
| 7 | 7 | 15 | 64 |

A requested CAN FD application payload of 9 bytes cannot be encoded directly.
It must use DLC 9, be padded to 12 bytes, and set `data_length` to 12.

## DLC helper functions

### `can_frame_dlc_to_length()`

```c
esp_err_t can_frame_dlc_to_length(
    uint8_t dlc,
    bool fd,
    uint8_t *data_length
);
```

Converts a raw DLC to its represented byte count.

- `fd == false` uses Classical CAN interpretation and clamps DLC 9–15 to 8.
- `fd == true` uses the CAN FD mapping table.
- `dlc > 15` returns `ESP_ERR_INVALID_ARG`.
- A null output pointer returns `ESP_ERR_INVALID_ARG`.
- The output is cleared to zero before validation.

Example:

```c
uint8_t length = 0U;

ESP_ERROR_CHECK(
    can_frame_dlc_to_length(
        13U,
        true,
        &length
    )
);

/* length == 32 */
```

### `can_frame_length_to_dlc()`

```c
esp_err_t can_frame_length_to_dlc(
    uint8_t data_length,
    bool fd,
    uint8_t *dlc,
    uint8_t *encoded_length
);
```

Finds the smallest DLC capable of representing the requested byte count.

- Classical lengths 0–8 map directly.
- Classical lengths above 8 return `ESP_ERR_INVALID_SIZE`.
- CAN FD lengths are rounded up to the next representable size.
- CAN FD lengths above 64 return `ESP_ERR_INVALID_SIZE`.
- Null output pointers return `ESP_ERR_INVALID_ARG`.
- Both outputs are cleared before validation.

Example with required padding:

```c
uint8_t dlc = 0U;
uint8_t encoded_length = 0U;

ESP_ERROR_CHECK(
    can_frame_length_to_dlc(
        9U,
        true,
        &dlc,
        &encoded_length
    )
);

/* dlc == 9, encoded_length == 12 */
```

The caller must initialize the three padding bytes before transmitting.

## Frame validation

```c
esp_err_t can_frame_validate(
    const can_frame_t *frame
);
```

Validation checks:

1. the pointer is not null;
2. `bus` is within the defined range;
3. `timestamp_source` is valid;
4. no unknown flag bits are set;
5. the identifier fits its selected 11-bit or 29-bit format;
6. DLC is between 0 and 15;
7. CAN FD is not assigned to Primary;
8. remote and CAN FD flags are not combined;
9. BRS and ESI appear only on CAN FD frames;
10. remote frames have zero `data_length`;
11. data length fits the selected frame format;
12. data length exactly matches the length represented by DLC.

Invalid fields and combinations return `ESP_ERR_INVALID_ARG`. Invalid payload
sizes or a DLC/data-length mismatch return `ESP_ERR_INVALID_SIZE`.

Validation proves that the common frame is internally consistent. It does not
prove that the selected interface is running, that its bitrate is correct, or
that a physical bus will acknowledge the transmission.

## `can_event_t`

```c
typedef struct
{
    can_event_type_t type;
    can_frame_direction_t direction;
    can_frame_t frame;
    uint32_t event_sequence;
    uint32_t transaction_id;
    uint32_t native_sequence;
    esp_err_t result;
} can_event_t;
```

An event adds delivery and transmit-lifecycle information to a frame.

### Event types

| Value | Enumerator | Direction | Meaning |
|---:|---|---|---|
| 0 | `CAN_EVENT_RX` | RX | Frame received from a bus |
| 1 | `CAN_EVENT_TX_QUEUED` | TX | Asynchronous TX request accepted |
| 2 | `CAN_EVENT_TX_COMPLETED` | TX | Controller confirmed success |
| 3 | `CAN_EVENT_TX_FAILED` | TX | Transmission ended unsuccessfully |
| 4 | `CAN_EVENT_TX_ABORTED` | TX | Pending transmission was cancelled |

`CAN_EVENT_TYPE_COUNT` is a boundary value, not an event type.

### `event_sequence`

The router assigns a globally increasing value beginning with
`CAN_EVENT_SEQUENCE_INITIAL`. Unsigned 32-bit wraparound is valid.

Consumers can use the sequence to notice missing events, but a gap does not by
itself prove loss on the physical bus. Filtering, queue overflow, pausing,
transport errors, or consumer-side loss can also create gaps.

### `transaction_id`

The router assigns a non-zero transaction ID to a logical transmit request.
Its `TX_QUEUED`, `TX_COMPLETED`, `TX_FAILED`, and `TX_ABORTED` events retain the
same value. Received events use `CAN_TRANSACTION_ID_NONE`.

Use this field to associate asynchronous completion with the original request
or application context.

### `native_sequence`

This is a driver/controller-specific sequence used to correlate the hardware
completion with the pending router transaction. When unavailable it is
`CAN_NATIVE_SEQUENCE_NONE`.

Native sequences can wrap and may be reused after completion. They are not
application transaction IDs and should not be persisted as globally unique
identifiers.

### `result`

- RX normally uses `ESP_OK`.
- TX queued normally uses `ESP_OK`.
- Successful TX completion uses `ESP_OK`.
- Failed or aborted TX events carry the relevant `esp_err_t` value.

## Event lifecycle example

A successful asynchronous transmission can produce:

```text
Application calls can_router_transmit()
    |
    +-- transaction_id = 42
    |
    v
TX_QUEUED
    event_sequence = 100
    transaction_id = 42
    native_sequence = 7
    result = ESP_OK
    |
    v
Controller transmits and reports completion
    |
    v
TX_COMPLETED
    event_sequence = 104
    transaction_id = 42
    native_sequence = 7
    result = ESP_OK
```

Sequences 101–103 can belong to unrelated RX or TX events. Correlation must use
`transaction_id`, not adjacency in the event stream.

If shutdown or reconfiguration cancels the request, its terminal event is
`TX_ABORTED`. If the controller attempts transmission but cannot complete it,
the terminal event is `TX_FAILED`.

## Construction examples

### Classical CAN transmit frame

```c
can_frame_t frame = {
    .bus = CAN_BUS_PRIMARY,
    .identifier = 0x123U,
    .flags = 0U,
    .dlc = 8U,
    .data_length = 8U,
    .data = {0x01U, 0x02U, 0x03U, 0x04U,
             0x05U, 0x06U, 0x07U, 0x08U},
    .timestamp_us = 0U,
    .timestamp_source = CAN_TIMESTAMP_SOURCE_NONE,
};

ESP_ERROR_CHECK(can_frame_validate(&frame));
```

### Extended CAN FD frame with BRS

```c
can_frame_t frame = {
    .bus = CAN_BUS_SECONDARY,
    .identifier = 0x18DAF110U,
    .flags =
        CAN_FRAME_FLAG_EXTENDED_ID |
        CAN_FRAME_FLAG_FD |
        CAN_FRAME_FLAG_BRS,
    .dlc = 10U,
    .data_length = 16U,
    .timestamp_us = 0U,
    .timestamp_source = CAN_TIMESTAMP_SOURCE_NONE,
};

/* Initialize all 16 payload bytes before transmission. */
ESP_ERROR_CHECK(can_frame_validate(&frame));
```

### Classical remote frame

```c
can_frame_t frame = {
    .bus = CAN_BUS_PRIMARY,
    .identifier = 0x321U,
    .flags = CAN_FRAME_FLAG_REMOTE,
    .dlc = 8U,
    .data_length = 0U,
    .timestamp_us = 0U,
    .timestamp_source = CAN_TIMESTAMP_SOURCE_NONE,
};

ESP_ERROR_CHECK(can_frame_validate(&frame));
```

## Adapter behavior

The TWAI adapter:

- always assigns `CAN_BUS_PRIMARY` to received frames;
- rejects CAN FD, BRS, and ESI for transmission;
- preserves Classical RTR semantics;
- marks a non-zero driver RX timestamp as software-generated;
- clears the timestamp when constructing a TX driver frame.

Because the TWAI-native structure has no separate raw DLC, common Classical
frames with DLC 9–15 cannot be converted for TWAI transmission without losing
information and are rejected.

The MCP2518FD adapter:

- always assigns `CAN_BUS_SECONDARY` to received frames;
- supports Classical CAN and CAN FD;
- preserves extended, RTR, FD, BRS, and ESI properties where valid;
- marks valid controller timestamps as hardware-generated;
- clears the timestamp when constructing a TX driver frame.

## Consumer rules

Code receiving a common frame or event should:

1. validate untrusted or externally constructed frames;
2. inspect `data_length`, not DLC, when iterating payload bytes;
3. inspect `timestamp_source` before using the timestamp;
4. use `transaction_id` to correlate TX lifecycle events;
5. treat `native_sequence` as a driver detail;
6. copy the structure before retaining it beyond a callback's lifetime;
7. avoid assuming that unused payload bytes are initialized;
8. tolerate event-sequence wraparound using unsigned arithmetic.

## Related documentation

- [Firmware architecture](architecture.md)
- [CAN WebSocket protocol](websocket-can-protocol.md)
