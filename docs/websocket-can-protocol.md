<!--
SPDX-License-Identifier: GPL-3.0-only
Copyright (C) 2026 Yurii Ridkovets
-->

# CAN WebSocket protocol

This document describes version 1 of the Spectra CAN WebSocket protocol.

## Endpoint and message types

Connect to:

```text
ws://<device-address>/ws/can
```

The current implementation supports one active client. Send an explicit
subscription after connecting.

| Direction | WebSocket message | Purpose |
|---|---|---|
| Client to device | Text/JSON | Subscription and pause control |
| Device to client | Text/JSON | Acknowledgement, errors, statistics |
| Device to client | Binary | Batches of CAN events |
| Both | Ping/Pong control frame | Connection health |

All multibyte binary fields use little-endian byte order.

## Subscription command

All fields are required:

```json
{
  "command": "subscribe",
  "primary": true,
  "secondary": true,
  "rx": true,
  "tx": true,
  "paused": false
}
```

`primary` and `secondary` select buses; `rx` and `tx` select directions.
Set `paused` to `true` to suspend delivery without stopping CAN reception or
closing the socket. Send the command again with `paused` set to `false` to
resume.

The server confirms the applied state:

```json
{"type":"subscription","primary":true,"secondary":true,"rx":true,"tx":true,"paused":false}
```

An invalid or incomplete command produces:

```json
{"type":"error","code":"invalid_command"}
```

The current maximum command payload is 256 bytes.

## Stream statistics

The server periodically sends a text message:

```json
{
  "type": "stream_statistics",
  "queued_events": 12809,
  "sent_events": 12801,
  "dropped_events": 0,
  "send_failures": 0,
  "filtered_events": 0,
  "queue_current": 8,
  "queue_peak": 15,
  "queue_capacity": 512,
  "sent_batches": 1109,
  "sent_binary_bytes": 613888,
  "batch_events_total": 12801,
  "batch_events_peak": 32,
  "batch_events_average": 11
}
```

| Field | Meaning |
|---|---|
| `queued_events` | Events accepted into the stream queue |
| `sent_events` | Events included in successfully submitted batches |
| `dropped_events` | Events rejected because the queue was full |
| `send_failures` | Failed WebSocket send operations |
| `filtered_events` | Events excluded by the subscription |
| `queue_current` | Current queue occupancy |
| `queue_peak` | Highest observed queue occupancy |
| `queue_capacity` | Queue capacity |
| `sent_batches` | Successfully submitted binary batches |
| `sent_binary_bytes` | Submitted binary bytes, including batch headers |
| `batch_events_total` | Total events placed in submitted batches |
| `batch_events_peak` | Largest submitted batch in events |
| `batch_events_average` | Integer average events per submitted batch |

Counters are cumulative for the current service run. Queue capacity, batch
limits, batching delay, and statistics interval are implementation details.

## Binary batch format

Every binary message contains an 8-byte batch header followed by
`event_count` variable-length records.

### Batch header (8 bytes)

| Offset | Size | Type | Field | Value |
|---:|---:|---|---|---|
| 0 | 1 | `uint8` | `version` | `1` |
| 1 | 1 | `uint8` | `message_type` | `1` (`EVENT_BATCH`) |
| 2 | 2 | `uint16` | `event_count` | Number of records |
| 4 | 4 | `uint32` | `payload_size` | Combined record bytes; header excluded |

The WebSocket payload size must equal `8 + payload_size`.

### Event record (40-byte header plus data)

| Offset | Size | Type | Field |
|---:|---:|---|---|
| 0 | 1 | `uint8` | `event_type` |
| 1 | 1 | `uint8` | `bus` |
| 2 | 1 | `uint8` | `direction` |
| 3 | 1 | `uint8` | `flags` |
| 4 | 1 | `uint8` | `dlc` |
| 5 | 1 | `uint8` | `data_length` |
| 6 | 1 | `uint8` | `timestamp_source` |
| 7 | 1 | — | Reserved |
| 8 | 4 | `uint32` | `event_sequence` |
| 12 | 4 | `uint32` | `transaction_id` |
| 16 | 4 | `uint32` | `native_sequence` |
| 20 | 4 | `uint32` | `identifier` |
| 24 | 4 | `int32` | `result` (`esp_err_t`) |
| 28 | 8 | `uint64` | `timestamp_us` |
| 36 | 4 | — | Reserved |
| 40 | `data_length` | bytes | CAN payload |

Record size is `40 + data_length`. Receivers must ignore reserved bytes.

## Encoded values

### Event types

| Value | Meaning |
|---:|---|
| 0 | RX frame |
| 1 | TX queued |
| 2 | TX completed |
| 3 | TX failed |
| 4 | TX aborted |

### Bus, direction, and timestamp source

| Field | Value 0 | Value 1 | Value 2 |
|---|---|---|---|
| `bus` | Primary | Secondary | — |
| `direction` | RX | TX | — |
| `timestamp_source` | None | Software | Hardware |

Software and hardware timestamps can use different time bases. Do not assume
that timestamps from the two buses are directly comparable unless firmware
explicitly synchronizes them.

### Frame flags

| Mask | Meaning |
|---:|---|
| `0x01` | Extended 29-bit identifier |
| `0x02` | Classical CAN remote frame |
| `0x04` | CAN FD frame |
| `0x08` | CAN FD bit-rate switching |
| `0x10` | CAN FD error-state indicator |

Standard identifiers must not exceed `0x7FF`; extended identifiers must not
exceed `0x1FFFFFFF`.

## DLC and data length

`dlc` is the encoded CAN Data Length Code. `data_length` is the number of
payload bytes following the record header.

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

For a remote frame, `data_length` is zero because it has no payload, while
`dlc` still carries the requested Classical CAN length.

## Transaction and sequence fields

- `transaction_id` correlates all lifecycle events for one logical transmit
  request. Zero means no transaction ID.
- `native_sequence` is a controller/driver transmit sequence when available.
  `0xFFFFFFFF` means no native sequence.
- `result` is a signed ESP-IDF error code, primarily relevant to failed and
  aborted TX events. Decode it using the ESP-IDF version used by the firmware.
- `event_sequence` is a monotonically increasing 32-bit value, initially 1.
  Unsigned wraparound is valid.

A sequence gap does not necessarily mean a physical CAN-frame loss. Sequence
numbers are assigned before client filtering. Gaps can therefore result from
filters, pause/resume, a full stream queue, encoding/send failures, a lost
connection, or client-side decoding loss. Interpret them together with
`filtered_events`, `dropped_events`, and `send_failures`.

## Ping/Pong

The service uses standard WebSocket control frames. A Ping receives a Pong
with the same payload. Protocol version 1 has no JSON `ping` command. Browser
JavaScript cannot manually create WebSocket Ping control frames.

## Minimal JavaScript decoder

```javascript
const socket = new WebSocket(`ws://${location.host}/ws/can`);
socket.binaryType = "arraybuffer";

socket.addEventListener("open", () => socket.send(JSON.stringify({
    command: "subscribe",
    primary: true,
    secondary: true,
    rx: true,
    tx: true,
    paused: false,
})));

socket.addEventListener("message", ({ data }) => {
    if (typeof data === "string") {
        console.log(JSON.parse(data));
        return;
    }

    const view = new DataView(data);
    const version = view.getUint8(0);
    const type = view.getUint8(1);
    const count = view.getUint16(2, true);
    const payloadSize = view.getUint32(4, true);

    if (version !== 1 || type !== 1 || data.byteLength !== 8 + payloadSize) {
        throw new Error("Invalid CAN event batch");
    }

    let offset = 8;
    const events = [];

    for (let index = 0; index < count; index += 1) {
        if (offset + 40 > data.byteLength) throw new Error("Truncated header");

        const length = view.getUint8(offset + 5);
        const end = offset + 40 + length;
        if (end > data.byteLength) throw new Error("Truncated payload");

        events.push({
            eventType: view.getUint8(offset),
            bus: view.getUint8(offset + 1),
            direction: view.getUint8(offset + 2),
            flags: view.getUint8(offset + 3),
            dlc: view.getUint8(offset + 4),
            dataLength: length,
            timestampSource: view.getUint8(offset + 6),
            eventSequence: view.getUint32(offset + 8, true),
            transactionId: view.getUint32(offset + 12, true),
            nativeSequence: view.getUint32(offset + 16, true),
            identifier: view.getUint32(offset + 20, true),
            result: view.getInt32(offset + 24, true),
            timestampUs: view.getBigUint64(offset + 28, true),
            payload: new Uint8Array(data.slice(offset + 40, end)),
        });
        offset = end;
    }

    if (offset !== data.byteLength) throw new Error("Trailing batch data");
    console.log(events);
});
```

`timestampUs` is a JavaScript `BigInt`.

## Compatibility requirements

- Verify `version` and `message_type` before decoding.
- Use `payload_size` and `data_length` for bounds checks.
- Do not assume a fixed number of events per batch.
- The fields and values above are stable for protocol version 1.
- Queue sizes, batch limits, and timing parameters are not wire-contract fields.
