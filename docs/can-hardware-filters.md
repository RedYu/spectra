# Hardware CAN RX filters

CAN Logger and CAN Analyzer expose the same device-wide filter configuration.
These are not browser display filters: rejected frames never reach the router,
monitor, SD logger or WebSocket. Previously buffered frames are not removed.
TX is not filtered. CAN acknowledgement behavior is not changed by acceptance
filtering; use listen-only mode when the device must not acknowledge traffic.

## Capabilities

| Channel | Exposed filters | Comparison |
| --- | --- | --- |
| Primary / ESP32-S3 TWAI | 1 full-width mask | One 11-bit or 29-bit ID/mask |
| Secondary / MCP2518FD | 32 independent filters | Each row selects 11-bit or 29-bit ID/mask; rows are ORed |

The API derives capacity from the SoC and MCP2518FD driver constants. The TWAI
dual 16-bit mode is not exposed in this version: it cannot compare all 29 bits
of an extended identifier. ESP-IDF additionally checks frame format in its TWAI
receive path; ID comparison is performed by hardware.

A mask bit of 1 means compare, 0 means ignore:

- ID `123`, mask `7FF`, standard: exactly `0x123`.
- ID `120`, mask `7F0`, standard: IDs `0x120` through `0x12F`.
- ID `18DAF110`, mask `1FFFFFFF`, extended: exactly that 29-bit ID.

`All messages` restores accept-all. Secondary also supports `No messages`, an
empty filter bank. Primary reject-all is not exposed by this driver API.
For Primary, ID=0 and mask=0 is ESP-IDF's special accept-all configuration,
including both frame formats. A zero mask with a nonzero ID selects all IDs
of the chosen format. Secondary zero masks still compare the selected format.

## Runtime behavior

Apply requires confirmation and replaces the selected channel's complete bank.
Primary quiesces its RX task and reconfigures the controller; pending TX can be
aborted. Secondary disables filters while replacing the bank under the driver
lock. Frames can be lost during either operation, and already queued RX events
can still appear after Apply. Do not change filters during an important capture.

All input is validated before hardware writes. Secondary reads back its active
bank before replacement and attempts rollback on SPI error. If rollback also
fails, it attempts to disable reception and reports failure; readback is required
before assuming any filter state. It cannot guarantee hardware state after a
persistent SPI fault.

Filters are **runtime only**, not persisted by the general Save settings button.
Changing CAN settings, restarting interfaces or rebooting can restore defaults.
Use Reload after such operations. Folding a panel does not change reception or
transmission. Fold state is stored per page in browser localStorage, if available.

## HTTP API

`GET /api/can/filters` returns `{ "buses": [...] }`. Each bus entry contains:
`bus` (0 Primary / 1 Secondary), `capacity`, `available`, `accept_all`,
`reject_all_supported`, `persistent` (false), `error` and `filters`.
Each filter contains `index`, `id`, `mask`, `extended`. An unavailable bus has
no usable filter snapshot; its inputs must remain disabled.

`POST /api/can/filters`, `Content-Type: application/json`:

```json
{
  "bus": 1,
  "accept_all": false,
  "filters": [
    {"id": 291, "mask": 2047, "extended": false},
    {"id": 1110, "mask": 2047, "extended": false}
  ]
}
```

JSON uses decimal integers; the browser editor uses hexadecimal. Slots are
assigned consecutively from zero on POST. For accept-all send `accept_all:true`
and `filters:[]`. For Secondary reject-all send `accept_all:false,filters:[]`.
Primary requires exactly one row unless accept-all is requested.

Success is HTTP 200; malformed values are 400, unavailable/controller failures
409, body outside 1..4096 bytes 413, unsupported content type 415. A timeout can
occur after application: GET the state before retrying. GET is not polled in the
background: the panel loads on first opening and provides manual Reload.

This endpoint uses the existing local web server security model, with no new
authentication. Do not expose the device to untrusted networks.

## Bench verification still required

1. Send two known IDs on a bench, apply one exact ID and verify only it reaches
   the monitor, WebSocket and newly written SCL records.
2. Test standard and extended IDs, partial masks and 32 Secondary rows.
3. Verify accept-all restoration, Secondary reject-all, interface restart and
   unavailable-interface responses.
4. Exercise malformed requests without any change to the previous filter bank.
5. Check captures during load, and driver rollback under injected SPI failures.

Reference: [ESP-IDF TWAI filtering documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/peripherals/twai.html).
