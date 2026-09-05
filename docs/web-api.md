<!--
SPDX-License-Identifier: GPL-3.0-only
Copyright (C) 2026 Yurii Ridkovets
-->

# Spectra Web API

This document describes the HTTP API currently registered by the Spectra
firmware. It covers system, power, network, settings, and file operations.
The live CAN WebSocket protocol is documented separately.

## Base address

The API is available over the same HTTP server as the embedded Web UI:

```text
http://spectra.device/api/
http://spectra-XXXXXX.local/api/
http://172.16.10.1/api/
```

The actual address depends on whether the client uses the device SoftAP, USB
RNDIS, or a shared Wi-Fi network.

## Conventions

- JSON request bodies use `Content-Type: application/json`.
- JSON responses use UTF-8.
- Boolean values are JSON `true` or `false`, not strings or integers.
- Integer byte counters may exceed the precise integer range of some
  JavaScript environments as the device runs; clients should not assume that
  every counter remains small forever.
- Query-string paths must be URL encoded.
- API consumers must tolerate additional JSON members in future firmware.
- A successful GET normally returns `200 OK` with a resource object.
- A successful state-changing operation returns a JSON result object.

Error responses produced by the shared Web API helper have this general form:

```json
{
  "success": false,
  "message": "Human-readable error description"
}
```

Clients should primarily use the HTTP status for control flow and treat
`message` as diagnostic text rather than a stable machine-readable code.

## Endpoint summary

| Method | Endpoint | Purpose |
|---|---|---|
| `GET` | `/api/system` | System identity, runtime, memory, storage, and reset state |
| `POST` | `/api/system/restart` | Schedule a graceful restart |
| `GET` | `/api/power` | PMIC, charger, rail, interrupt, and battery state |
| `GET` | `/api/network` | Wi-Fi, USB RNDIS, DNS, and mDNS state |
| `POST` | `/api/network/wifi/scan` | Start an asynchronous Wi-Fi scan |
| `GET` | `/api/settings` | Read the active configuration |
| `PUT` | `/api/settings` | Validate and apply configuration |
| `POST` | `/api/settings/save` | Persist the active configuration |
| `POST` | `/api/settings/reload` | Reload and apply persistent configuration |
| `DELETE` | `/api/settings/wifi/sta/credentials` | Delete stored Station credentials |
| `GET` | `/api/files` | List a storage directory |
| `GET` | `/api/files/download` | Download one file |

## System API

### `GET /api/system`

Returns a snapshot of device identity and system health.

```bash
curl http://spectra.device/api/system
```

Response members:

| Field | Type | Meaning |
|---|---|---|
| `device_name` | string | Product display name |
| `device_id` | string | Device identifier |
| `serial_number` | string | Device serial number |
| `hardware_version` | string | Hardware revision |
| `firmware_version` | string | Running firmware version |
| `chip_model` | string | ESP chip model |
| `chip_revision` | number | Chip revision |
| `chip_cores` | number | Number of CPU cores |
| `cpu_frequency_mhz` | number | Configured CPU frequency |
| `cpu_usage` | number | Measured CPU usage |
| `uptime_sec` | number | Uptime in seconds |
| `reset_reason` | number | Native reset-reason value |
| `reset_reason_name` | string | Human-readable reset reason |
| `flash_size` | number | Flash capacity in bytes |
| `free_heap` | number | Current free heap in bytes |
| `minimum_free_heap` | number | Lowest free heap observed |
| `psram_size` | number | Total PSRAM in bytes |
| `psram_free` | number | Current free PSRAM in bytes |
| `psram_minimum_free` | number | Lowest free PSRAM observed |
| `chip_temperature_valid` | boolean | Whether the temperature value is valid |
| `chip_temperature_celsius` | number/null | Chip temperature |
| `storage_ready` | boolean | Internal storage is ready |
| `sd_card_mounted` | boolean | SD filesystem is mounted |
| `sd_card_info_available` | boolean | SD capacity information is valid |
| `sd_card_filesystem` | string | SD filesystem description |
| `sd_card_total_bytes` | number | Total SD capacity |
| `sd_card_used_bytes` | number | Used SD capacity |
| `sd_card_free_bytes` | number | Free SD capacity |
| `internet_available` | boolean | Backend connectivity check succeeded |
| `ota_available` | boolean | OTA capability/status reported by firmware |

Temperature and SD-card capacity fields must be used only when their matching
validity flag is true.

### `POST /api/system/restart`

Schedules a graceful restart after a 500 ms delay. The delay allows the HTTP
response to be transmitted before shutdown processing begins.

```bash
curl -X POST http://spectra.device/api/system/restart
```

No request body is required. A newly accepted request returns `202 Accepted`:

```json
{
  "success": true,
  "message": "Graceful restart scheduled"
}
```

If a restart is already scheduled, the endpoint returns `409 Conflict`:

```json
{
  "success": false,
  "message": "Restart is already scheduled"
}
```

The system service performs the restart asynchronously. Persistent settings
must be saved before calling this endpoint if the client expects unsaved
runtime changes to survive the restart.

## Power API

### `GET /api/power`

Returns the current power subsystem snapshot.

```bash
curl http://spectra.device/api/power
```

The response is grouped into objects describing:

- the AXP313A controller and raw status;
- enabled output rails and configured voltages;
- DC/DC and LDO state;
- power-on and wake-up sources;
- interrupt state and masks;
- shutdown and long-press behavior;
- undervoltage, overvoltage, overcurrent, and overtemperature protection;
- battery/charger measurement and presence information when available.

Common nested fields include `enabled`, `active`, `status_raw`,
`configured_voltages_mv`, `outputs`, `interrupts`, `power_control`,
`wakeup_control`, `shutdown_control`, and `protection`.

This endpoint is diagnostic and read-only. Hardware-dependent sections can be
unavailable or report invalid measurements; clients must honor the validity and
enabled fields rather than interpreting zero as a valid measurement.

## Network API

### `GET /api/network`

Returns all currently known network-interface and local-name state.

```bash
curl http://spectra.device/api/network
```

Top-level sections include:

| Section | Contents |
|---|---|
| `wifi` | Wi-Fi initialization and operating mode |
| `wifi.softap` | SoftAP enable state, SSID, channel, address, and clients |
| `wifi.station` | STA enable/connection state, SSID, RSSI, and IPv4 configuration |
| `wifi.scan` | Asynchronous scan state and discovered networks |
| `usb_rndis` | USB network state and IPv4 configuration |
| `dns` | Local DNS service state and name |
| `mdns` | mDNS state, hostname, service, and instance name |

Frequently used fields include `initialized`, `started`, `enabled`,
`connected`, `host_connected`, `mode`, `ssid`, `channel`, `rssi`,
`mac_address`, `ip_address`, `netmask`, `gateway`, and `client_count`.

The SoftAP `clients` array contains `mac_address`, `rssi`, and `ip_address` for
each known client. IPv4 address pools are represented using `start` and `end`.

The `wifi.scan` object has this shape:

```json
{
  "state": "complete",
  "result_count": 2,
  "truncated": false,
  "last_error": 0,
  "last_error_name": "ESP_OK",
  "networks": [
    {
      "ssid": "Example",
      "bssid": "00:11:22:33:44:55",
      "rssi": -48,
      "channel": 6,
      "password_required": true
    }
  ]
}
```

`state` is `idle`, `running`, `complete`, `error`, or `unknown`. The `networks`
array is populated only when the state is `complete`. `result_count` reports
the scan result count; `truncated` indicates that not every discovered network
fit in the service result buffer.

### `POST /api/network/wifi/scan`

Starts an asynchronous Wi-Fi scan. Scanning is not performed inside the HTTP
handler, so the initial response does not contain results.

```bash
curl -X POST http://spectra.device/api/network/wifi/scan
```

A successfully started scan returns `202 Accepted`:

```json
{
  "success": true,
  "message": "Wi-Fi scan started"
}
```

Poll `GET /api/network` and inspect `wifi.scan.state` until it becomes
`complete` or `error`. Starting another scan while one is running returns
`409 Conflict`. If the Wi-Fi scan service is unavailable or cannot start, the
endpoint returns `503 Service Unavailable`.

## Settings API

### `GET /api/settings`

Returns the active configuration. Wi-Fi Station passwords are never returned.
Only a non-secret credential identifier and whether credentials are configured
are exposed.

```bash
curl http://spectra.device/api/settings
```

Representative response:

```json
{
  "schema_version": 1,
  "device": {
    "target": "spectra",
    "name": "Modern Automotive CAN Analyzer"
  },
  "display": {
    "brightness": 80
  },
  "logging": {
    "sd_enabled": true,
    "tag_levels": {
      "warning": "wifi,can_twai_driver,can_service",
      "info": "",
      "debug": "",
      "disabled": "memory"
    }
  },
  "ui": {
    "animations_enabled": false,
    "theme": "light"
  },
  "network": {
    "wifi_ap": {
      "enabled": true,
      "ssid": "Spectra",
      "password": ""
    },
    "wifi_sta": {
      "enabled": true,
      "ssid": "Example",
      "credential_id": "0123456789abcdef",
      "credentials_configured": true
    },
    "usb_rndis": {
      "enabled": false
    }
  },
  "can": {
    "primary": {
      "enabled": true,
      "bitrate": 500000,
      "listen_only": true
    },
    "secondary": {
      "enabled": true,
      "nominal_bitrate": 500000,
      "data_bitrate": 2000000,
      "fd_enabled": true,
      "brs_enabled": true,
      "listen_only": true
    }
  }
}
```

The exact returned Wi-Fi password representation is intentionally not a way to
retrieve stored credentials.

### `PUT /api/settings`

Validates and applies settings to running services.

```bash
curl -X PUT http://spectra.device/api/settings \
  -H "Content-Type: application/json" \
  --data-binary @device_config.json
```

The safest client behavior is read-modify-write:

1. retrieve the current document with `GET /api/settings`;
2. modify the required fields;
3. send the resulting document with `PUT /api/settings`;
4. call `/api/settings/save` when the change must survive restart.

The response reports `success`, a human-readable `message`, and
`restart_required`. Applying settings updates runtime state but is distinct
from persistence.

Validation includes, among other constraints:

- display brightness range;
- supported UI theme values;
- Wi-Fi SSID/password requirements;
- supported Primary CAN bitrate;
- supported Secondary nominal/data bitrates;
- CAN FD and BRS compatibility;
- valid logging tag-level strings.

Example changing both CAN channels:

```json
{
  "can": {
    "primary": {
      "enabled": true,
      "bitrate": 500000,
      "listen_only": true
    },
    "secondary": {
      "enabled": true,
      "nominal_bitrate": 500000,
      "data_bitrate": 2000000,
      "fd_enabled": true,
      "brs_enabled": true,
      "listen_only": true
    }
  }
}
```

Do not assume a change was persisted merely because it was applied
successfully.

### `POST /api/settings/save`

Persists the active settings to internal storage.

```bash
curl -X POST http://spectra.device/api/settings/save
```

No request body is required. The operation returns a JSON success/message
response. Save failures can be caused by storage state or filesystem I/O.

### `POST /api/settings/reload`

Discards unsaved runtime edits, loads the persistent configuration, validates
it, and applies it to the running services.

```bash
curl -X POST http://spectra.device/api/settings/reload
```

No request body is required. The response also indicates whether a restart is
required.

### `DELETE /api/settings/wifi/sta/credentials`

Deletes the stored Wi-Fi Station credentials from their separate NVS storage.

```bash
curl -X DELETE \
  http://spectra.device/api/settings/wifi/sta/credentials
```

This is intentionally separate from editing the public JSON configuration.
After deletion, Station mode cannot authenticate until new credentials are
provided and saved through the supported settings workflow.

## Files API

The files API operates on explicitly selected storage volumes:

| `volume` value | Storage | Mount point |
|---|---|---|
| `internal` | SPIFFS | `/storage` |
| `sd` | SD card | `/sdcard` |

API paths are volume-relative and must begin with `/`. The implementation
validates and resolves paths below the selected mount point.

### `GET /api/files`

Lists a directory with pagination.

Query parameters:

| Parameter | Required | Meaning |
|---|---|---|
| `volume` | yes | `internal` or `sd` |
| `path` | yes | URL-encoded volume-relative directory path |
| `offset` | no | First entry to return; default 0 |
| `limit` | no | Maximum entries; default 16, maximum 32 |

Example:

```bash
curl "http://spectra.device/api/files?volume=sd&path=%2F&offset=0&limit=16"
```

Response shape:

```json
{
  "volume": "sd",
  "path": "/",
  "offset": 0,
  "count": 2,
  "has_more": false,
  "entries": [
    {
      "name": "logs",
      "type": "directory",
      "size": 0
    },
    {
      "name": "session.asc",
      "type": "file",
      "size": 18432
    }
  ]
}
```

Entry size is meaningful for files. Directory size should not be interpreted
as a recursive byte total.

### `GET /api/files/download`

Downloads one file.

Query parameters:

| Parameter | Required | Meaning |
|---|---|---|
| `volume` | yes | `internal` or `sd` |
| `path` | yes | URL-encoded absolute path within that volume |

Example:

```bash
curl -o session.asc \
  "http://spectra.device/api/files/download?volume=sd&path=%2Fsession.asc"
```

The response body contains raw file bytes rather than JSON. The server supplies
download headers, including the file name, when the request succeeds.

Typical failures include an invalid volume/path, unavailable SD card, missing
file, an attempt to download a directory, and filesystem I/O failure.

## HTTP status handling

Clients should be prepared for:

| Status | Meaning |
|---:|---|
| `200` | Request completed successfully |
| `400` | Malformed JSON, missing/invalid query, or invalid setting |
| `404` | Resource, path, or file not found |
| `409` | Current service/device state conflicts with the operation |
| `413` | Request body exceeds the accepted limit |
| `500` | Allocation, serialization, storage, or internal service failure |
| `503` | Required service or removable storage is unavailable |

Not every handler necessarily uses every status above. Always parse the
returned message for diagnostics and avoid retrying validation errors without
changing the request.

## Security considerations

The current local HTTP interface must be treated as a trusted-network
interface:

- do not expose the device HTTP port directly to the public Internet;
- do not place passwords or tokens in query strings;
- remember that plain HTTP and `ws://` do not encrypt traffic;
- validate all paths and JSON even when requests originate from the embedded
  UI;
- keep Wi-Fi credentials in their dedicated NVS storage;
- add authentication and TLS before enabling remote control or Internet-facing
  deployments.

## WebSocket API

Live CAN events do not use REST. Connect to:

```text
ws://spectra.device/ws/can
```

Subscription commands, statistics messages, and the binary event format are
defined in [websocket-can-protocol.md](websocket-can-protocol.md).

## API evolution

This document describes the routes currently registered by the firmware. A new
operation should not be documented as available until its URI handler is
implemented and registered.

Future API changes should follow these rules:

1. Additive response fields remain backward compatible.
2. Existing field meanings must not change silently.
3. Breaking schemas require an explicit API-versioning strategy.
4. Errors intended for programmatic handling should gain stable error codes.
5. State-changing routes should distinguish runtime application from durable
   persistence.

## Related documentation

- [Firmware architecture](architecture.md)
- [Shared CAN frame and event model](can-frame-model.md)
- [CAN WebSocket protocol](websocket-can-protocol.md)
