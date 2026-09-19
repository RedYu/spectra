# Spectra

<div align="center">

![ESP-IDF Build](https://github.com/RedYu/spectra/actions/workflows/build.yml/badge.svg)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0.2%20%2B%20VFS%20fix-E7352C?logo=espressif)
![LVGL](https://img.shields.io/badge/LVGL-v9-00AEEF)
![Platform](https://img.shields.io/badge/Platform-ESP32--S3-blue)
![License](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)

**Modern Automotive CAN Analyzer**  
A modular dual-channel CAN and CAN FD diagnostic platform built with ESP32-S3, ESP-IDF, FreeRTOS, and LVGL 9.

</div>

---

## Overview

Spectra is a handheld automotive network analyzer based on the ESP32-S3. It combines a touch interface, two independent CAN channels, removable and internal storage, USB and Wi-Fi networking, power monitoring, and a layered firmware architecture.

The primary channel uses the ESP32-S3 TWAI controller for Classical CAN. The secondary channel uses an external MCP2518FD controller and supports both Classical CAN and CAN FD. Traffic from both interfaces is normalized into a shared frame model and distributed through a central CAN router to monitoring, recording, WebSocket streaming, transmission, and diagnostic protocol services.

Spectra is under active development. It is intended for diagnostics, monitoring, and development workflows and is not a safety-certified automotive control device.

## Features

### Platform and hardware

- ESP32-S3 running ESP-IDF 6.x and FreeRTOS
- ILI9488 480x320 display over SPI with DMA
- GT911 capacitive touch controller over a shared I2C bus
- LVGL 9 graphical interface with Light and Dark themes
- Shared board initialization, GPIO ISR service, and bus ownership
- AXP313A power-management support
- ETA6003 battery charger integration
- ADC-based battery-voltage monitoring
- Configurable Low and Critical battery-level thresholds
- Configurable display dimming and backlight-off idle timers
- Passive buzzer with configurable volume and asynchronous signals
- GPIO0 service/BOOT and GPIO47 power/user buttons with short, double, and
  long-press recognition
- Queue-based button action dispatch in the GUI task with audible feedback
- Graceful long-press power-off through the AXP313A PMIC
- MCP23017 I/O expander with centralized pin ownership
- Software-controlled 120-ohm termination for both CAN channels
- Internal SPIFFS and removable SD-card storage
- Selected task stacks and buffers allocated in PSRAM
- Centralized application task-priority configuration

### CAN communication

- Primary Classical CAN interface using ESP32-S3 TWAI
- Secondary Classical CAN and CAN FD interface using MCP2518FD over SPI
- TCAN1042HGV transceivers for both physical CAN channels
- Runtime enable, disable, bitrate, and listen-only configuration
- Runtime control of onboard 120-ohm termination on each CAN channel
- CAN FD nominal and data-phase bitrate configuration
- CAN FD BRS configuration
- Hardware timestamps from MCP2518FD
- MCP2518FD Transmit Event FIFO and transmission sequence tracking
- Transmission confirmations with application transaction/context tracking
- Acceptance-filter support
- Batched MCP2518FD receive processing
- Interrupt-driven MCP2518FD processing with CiVEC dispatch
- Driver and service loopback self-tests
- Bidirectional link test between the two CAN controllers
- Shared Classical CAN/CAN FD frame and event model
- TWAI and MCP2518FD frame adapters
- Central CAN router for RX, TX, confirmations, and subscriber delivery
- Queue, transmission, overflow, and drop statistics
- Runtime hardware RX filters for both CAN controllers
- Eight independent one-shot or periodic transmission jobs
- Configurable ID, DLC, and masked DATA increments

### CAN monitoring and streaming

- CAN monitor service subscribed to the central router
- Per-channel RX and TX statistics
- Per-identifier frame and byte counters
- Latest frame, timestamp, direction, and activity tracking
- Device CAN Monitor screen with Primary/Secondary filtering
- Compact live identifier table
- Detailed frame dialog with full CAN FD payload display
- Binary CAN event streaming over WebSocket
- Batched WebSocket event encoding
- WebSocket subscription filters for channel and direction
- Pause and resume without disconnecting the socket
- Stream queue, batch, payload, drop, and send statistics
- Browser CAN stream test page
- Browser CAN Logger with identifier and event tables
- Browser CAN Analyzer for live traffic and SCL recordings
- Configurable per-channel browser history with utilization indicators
- CSV, SCL, and ASC export of retained browser traffic
- Resizable identifier and event panels
- DBC Explorer with local and SD-card database loading
- Grouped message browser, signal selection, live physical-value decoding,
  history graphs, and byte/bit frame inspection
- Persistent customizable device and DBC-signal dashboards stored locally in
  the browser

### CAN recording

- High-throughput CAN logger subscribed to the central router
- PSRAM-backed queue and 16 KiB write buffering
- ASC text and versioned Spectra CAN Log (`.scl`) binary recordings
- Monotonic and synchronized wall-clock timestamps
- Timestamped recordings under `/sdcard/logs/can`
- Periodic buffered flush and full filesystem synchronization on stop
- Queue, write failure, and dropped-event statistics
- Sequential filesystem and raw SD-card read benchmarks
- Timed SCL and Spectra ASC traffic replay with pause, repetition, speed
  scaling, bus remapping, ID filtering, and selectable late-frame policy
- Replay transmission-confirmation, timing-lag, skip, drop, and failure
  statistics in the CAN Logger Web page

### CAN traffic replay

The CAN Logger Web page can replay an existing SCL or Spectra-generated ASC
recording directly from `/sdcard/logs/can`. Recordings are read as a stream;
the complete file is never loaded into RAM. ASC input uses a 4 KiB PSRAM read
buffer, while SCL records are decoded from their versioned binary headers.

Replay supports:

- file selection from `/logs/can` or manual entry of a nested path;
- `0.25x`, `0.5x`, `1x`, `2x`, `5x`, and `10x` timestamp scaling;
- maximum-speed submission limited by CAN arbitration and controller capacity;
- a start delay, finite repetition, or continuous replay until stopped;
- pause and resume without including paused time in the replay timeline;
- an inclusive recording-time range and CAN identifier range;
- source-channel, RX/TX direction, and Remote-frame filtering;
- preservation of the recorded bus or remapping to Primary or Secondary CAN;
- `WAIT`, `DROP_LATE`, and `STOP_ON_LAG` timing policies;
- final hardware transmission confirmation for every submitted frame;
- file progress, timing lag, completed, failed, dropped, and skipped counters.

CAN FD frames cannot be remapped to the Primary TWAI channel. Replay should be
started only after checking bitrate, operating mode, termination, and physical
bus conditions. The Web UI displays an explicit safety confirmation before it
starts transmitting. The complete behavior and API are documented in
[docs/can-replay.md](docs/can-replay.md).

### Diagnostic protocols

- ISO-TP over Classical CAN and CAN FD
- Single, First, Consecutive, and Flow Control frame processing
- Configurable physical addressing, IDs, block size, STmin, timeouts, CAN FD,
  BRS, padding, and complete-message buffers
- UDS client with P2/P2* timing and NRC `0x78` handling
- Diagnostic Session Control, ECU Reset, Tester Present, SecurityAccess,
  Read/Write Data By Identifier, DTC reading/clearing, and RoutineControl
- ReadMemoryByAddress, CommunicationControl,
  InputOutputControlByIdentifier, and ControlDTCSetting
- ReadScalingDataByIdentifier, RequestUpload, and bounded
  WriteMemoryByAddress requests
- Named support for Authentication, periodic and dynamically defined DIDs,
  RequestFileTransfer, timing control, secured transport, response-on-event,
  and link control through bounded raw requests
- Physical and functional UDS addressing with ISO-TP Single Frame enforcement
  for functional requests
- Functional multi-responder UDS discovery with a configurable response-ID
  range, responder timing, positive/negative result, and raw payload display
- Service-specific positive-response decoding for sessions, reset, DTC, DID,
  memory, SecurityAccess, routines, download/upload, transfer, authentication,
  dynamic and periodic DIDs, file transfer, timing, secured transport,
  Response On Event, and link control
- RequestDownload, TransferData, and RequestTransferExit primitives
- Non-blocking ECU programming pipeline with retry, cancellation, progress,
  long-running routine polling, reset, and session restoration
- Stage-aware NRC handling with delayed Busy/RequiredTimeDelay retries,
  TransferData sequence recovery, and SecurityAccess reacquisition
- ECU programming from BIN, Intel HEX, Motorola S-record, and BHX images stored
  under `/sdcard/firmwares`
- Streaming multi-section BHX parsing with big-endian `GHDR`/`SHDR` headers,
  embedded load addresses, and strict size and marker validation
- Persistent ECU profiles and DID catalogs stored as separate JSON files
- Typed DID decoding with byte order, scale, offset, units, ASCII, UTF-8,
  floating-point, signed, unsigned, and raw-byte values
- Timestamped ECU programming journals under `/sdcard/logs/firmware`
- Persistent ECU programming checkpoints with validated segment-boundary
  resume after reset, power loss, or transport failure
- Raw UDS service requests through the Web interface
- Dedicated ISO-TP/UDS diagnostics and UDS Programming pages
- Live decoded raw CAN trace for ISO-TP, UDS, and XCP diagnostics
- OBD-II console with live Mode 01 PIDs, DTCs, VIN, calibration data, and raw trace
- Stateful multi-session XCP master over CAN
- XCP RES, ERR, EV, SERV, and DAQ packet classification
- CONNECT and discovery response decoding with standard XCP errors
- CONNECT, DISCONNECT, GET_STATUS, GET_COMM_MODE_INFO, GET_ID, SET_MTA,
  UPLOAD, SHORT_UPLOAD, DOWNLOAD, DOWNLOAD_NEXT, and raw CTO execution
- Controlled XCP memory reads and bounded writes with explicit address policy
- XCP slave block-mode DOWNLOAD/PROGRAM transfers with MAX_CTO/MAX_BS checks
- XCP seed/key resource access and calibration-page control
- Dynamic DAQ configuration, raw DTO monitoring, and STIM transmission
- Browser-side A2L parsing with addressable measurement discovery, byte-order
  handling, linear conversions, units, and typed DAQ DTO decoding
- Named browser-persistent XCP ECU profiles containing CAN transport and compact
  DAQ measurement mappings
- Persistent browser programming checkpoints with the confirmed stage, next
  MTA address, byte count, and explicit operator-controlled restoration
- Autonomous device-side XCP programming from SD with streaming
  BIN/HEX/S-record/BHX parsing and progress monitoring
- SD-backed XCP programming journal with validated whole-block resume that
  never repeats erase automatically
- Explicit XCP programming start, clear, transfer, verify, and reset commands
- Dedicated browser XCP console
- Four independent XCP service sessions with a bounded command queue, one
  outstanding CTO per session, and transmit-confirmation tracking

### Storage and configuration

- JSON-based settings model and service
- Primary and Secondary CAN settings in the GUI and Web API
- Wi-Fi, display, battery, sound, time, logging, and UI settings in the device
  GUI and Web interface
- Settings persistence before controlled restart and shutdown
- UART and optional SD-card file logging
- Configurable per-tag ESP-IDF log levels
- Periodic SD-card log flushing
- Core-dump export from flash to SD card
- Graceful service shutdown and SD-card unmounting
- Browser-based internal-storage and SD-card file manager
- Folder and empty-file creation, uploads, deletion, and text-file preview
- SD-card formatting followed by recreation of application directories
- Directory-first sorting and tree navigation
- Resumable HTTP Range downloads with asynchronous SD-card buffering

### Connectivity

- USB RNDIS network interface
- Wi-Fi SoftAP and Station modes
- Wi-Fi network scanning while Station reconnection is active
- Wi-Fi credentials stored separately in NVS
- DHCP and local DNS services
- Per-device mDNS hostname
- Embedded Web UI and REST API
- Internet connectivity and firmware checks at startup and on explicit request
- Backend firmware-manifest discovery with device, firmware, and hardware
  identification headers
- Human-readable Wi-Fi disconnection reasons
- SNTP synchronization with configurable servers and POSIX timezone

## Hardware

| Component | Description |
| --- | --- |
| MCU | ESP32-S3, 240 MHz |
| Display | ILI9488, 480x320, SPI |
| Touch | GT911, shared I2C bus |
| Primary CAN | ESP32-S3 TWAI with TCAN1042HGV transceiver |
| Secondary CAN | MCP2518FD with TCAN1042HGV transceiver |
| MCP2518FD oscillator | 20 MHz |
| Power management | AXP313A PMIC |
| Battery charger | ETA6003 |
| Battery | Two 2200 mAh single-cell packs in parallel (1S2P, 4400 mAh nominal) |
| Battery monitoring | ADC voltage measurement through a 10 kOhm / 10 kOhm resistor divider on the current prototype |
| Audible feedback | Passive PWM-controlled buzzer |
| Reset button | CHIP_PU/EN to ground; immediate hardware reset |
| Service button | GPIO0 to ground; ROM download mode during reset and application actions after startup |
| Power/user button | GPIO47 to ground and AXP313A PWRON; wake/power-on plus application actions |
| I/O expansion | MCP23017 over I2C |
| CAN termination | Independently controlled 120-ohm resistors through MCP23017 |
| Internal storage | SPIFFS |
| Removable storage | SD card over SPI |
| USB connectivity | USB RNDIS network interface |
| Flash | 16 MB |

### Buttons and power control

Spectra uses three distinct button paths:

- The **Reset** button pulls ESP32-S3 `CHIP_PU/EN` low. It is an emergency
  hardware reset and does not run the controlled shutdown sequence.
- The **Service/BOOT** button pulls `GPIO0` low. Holding it during reset selects
  the ESP32-S3 ROM download mode. After startup, the application can assign
  short, double, and long-press actions to it.
- The **Power/User** button is observed on `GPIO47` and is also connected to the
  AXP313A `PWRON` input. It can power the board on from the PMIC off state and
  provides short, double, and long-press events while the application is
  running.

`button_service` monitors both GPIO buttons from one periodic `esp_timer`; it
does not create a dedicated FreeRTOS task. The current timing is a 10 ms poll,
40 ms debounce, 350 ms double-press window, 2 second long press, and 1 second
startup guard. `button_action_dispatcher` transfers events through a bounded
eight-entry queue and executes registered callbacks in the GUI task. Short,
double, and long presses use click, success, and warning buzzer signals.

A long press of the Power/User button starts the controlled shutdown sequence.
Services are stopped, pending settings and logs are flushed, and the SD card is
unmounted before the AXP313A software power-off command is issued. If the PMIC
command fails or power remains present, the firmware falls back to an ESP32-S3
restart instead of remaining in a partially stopped state.

The current prototype uses two 2200 mAh single-cell batteries in parallel. The
ETA6003 is a single-cell charger, so a 2S series battery configuration is not
supported. The physical 10 kOhm / 10 kOhm battery divider draws approximately
210 uA at 4.2 V even while the processor is off. A future hardware revision can
reduce this loss with a higher-value filtered divider or a switched divider.
The divider values in `board_config.h` must always match the assembled hardware
before battery-voltage calibration is evaluated.

When combining `GPIO47` with the PMIC `PWRON` signal, the board must prevent
back-powering between the ESP32-S3 and PMIC domains and must respect the voltage
domain of the selected ESP32-S3 module.

## Architecture

The detailed firmware architecture is documented in
[docs/architecture.md](docs/architecture.md).
The shared CAN types and validation rules are documented in
[docs/can-frame-model.md](docs/can-frame-model.md).
The diagnostic protocols are documented in
[docs/isotp.md](docs/isotp.md), [docs/uds.md](docs/uds.md),
[docs/obd2.md](docs/obd2.md), and [docs/xcp.md](docs/xcp.md).
Hardware acceptance filters and the HTTP API are
described in [docs/can-hardware-filters.md](docs/can-hardware-filters.md) and
[docs/web-api.md](docs/web-api.md). CAN recording playback is documented in
[docs/can-replay.md](docs/can-replay.md). Streaming firmware image readers are
described in [docs/firmware-images.md](docs/firmware-images.md).

```text
CAN bus                     CAN FD bus
   |                            |
   v                            v
TWAI driver              MCP2518FD driver
   |                            |
   v                            v
Primary CAN service      Secondary CAN service
          \                 /
           \               /
            v             v
                         CAN router
             /       /       /       \       \
            v       v       v         v       v
      CAN Monitor  Logger  Replay  WebSocket  Protocols
                                             /    |    \
                                          ISO-TP UDS   XCP
```

Repository layout:

```text
spectra/
├── main/                       # Application startup and service orchestration
├── components/
│   ├── app/                    # Application configuration
│   ├── app_task_config/        # Shared FreeRTOS task priorities
│   ├── board/                  # Board initialization and shared resources
│   ├── can/                    # Shared frame model and CAN router
│   ├── can_monitor/            # CAN monitoring and identifier statistics
│   ├── drivers/                # Hardware-specific drivers
│   ├── gui/                    # Screens, widgets, themes, styles, and assets
│   ├── isotp/                  # ISO-TP protocol, sessions, and channels
│   ├── lvgl_port/              # LVGL display and input integration
│   ├── models/                 # Thread-safe application state
│   ├── services/               # Application services and Web APIs
│   ├── uds/                    # UDS protocol, requests, and client
│   └── xcp/                    # Stateful XCP master, DAQ/STIM, CAL/PAG, PGM
├── web_src/                    # Developer Web UI sources
├── spiffs_data/                # Generated SPIFFS content and default configuration
├── partitions.csv
└── sdkconfig.defaults
```

The project separates responsibilities into layers:

- **Drivers** operate hardware and expose low-level state and statistics.
- **Services** own tasks, queues, synchronization, and long-lived resources.
- **CAN Router** normalizes delivery and isolates protocol consumers from hardware drivers.
- **CAN Monitor** aggregates bus and identifier activity without blocking reception.
- **Models** hold synchronized application state.
- **GUI** presents device state and sends actions to services.
- **Web API and WebSocket** expose configuration, diagnostics, files, and live CAN events.
- **I/O Expander service** owns MCP23017 outputs and prevents feature services
  from modifying unrelated pins.
- **Button service** recognizes physical button gestures from a shared timer;
  the action dispatcher moves callbacks into the GUI task and tracks queue
  usage and drops.
- **Shutdown service** coordinates restart and PMIC power-off paths so storage
  and long-lived services are stopped in a defined order.

Web UI files must be edited in `web_src/`. The build runs
`scripts/build_web.py` automatically and recreates `spiffs_data/www/` before
the SPIFFS image is generated. With `CONFIG_SPECTRA_WEB_CONTENT_GZIP=y`, the
staging directory contains only `.gz` resources; otherwise it contains copies
of the original developer files. Generated web resources are not tracked by
Git.

## CAN interfaces

### Primary CAN

The primary interface uses the ESP32-S3 TWAI controller and supports Classical CAN frames.

Supported nominal bitrates:

- 10 kbit/s
- 20 kbit/s
- 33.333 kbit/s
- 50 kbit/s
- 83.333 kbit/s
- 100 kbit/s
- 125 kbit/s
- 250 kbit/s
- 500 kbit/s
- 800 kbit/s
- 1 Mbit/s

### Secondary CAN and CAN FD

The secondary interface uses the MCP2518FD controller over a dedicated SPI bus. It can receive and transmit Classical CAN and CAN FD frames.

Supported nominal arbitration bitrates:

- 10 kbit/s
- 20 kbit/s
- 33.333 kbit/s
- 50 kbit/s
- 83.333 kbit/s
- 100 kbit/s
- 125 kbit/s
- 250 kbit/s
- 500 kbit/s
- 800 kbit/s
- 1 Mbit/s

Supported CAN FD data-phase bitrates when BRS is enabled:

- 1 Mbit/s
- 2 Mbit/s
- 4 Mbit/s
- 5 Mbit/s
- 8 Mbit/s when enabled by the build-time maximum data-bitrate configuration

Configuration includes:

- nominal arbitration bitrate;
- CAN FD data-phase bitrate;
- CAN FD enable;
- Bit Rate Switching (BRS);
- normal or listen-only operation;
- transmission retry policy;
- RX and TX FIFO sizing.

Without BRS, the configured data bitrate must equal the nominal bitrate. With BRS, the data bitrate must be higher than the nominal bitrate. Actual achievable bitrates depend on the MCP2518FD system clock and bit-timing solution. Every selected configuration is therefore validated by the driver rather than inferred only from the settings list.

## CAN Monitor

The device CAN Monitor screen is intended as a lightweight check that an automotive bus is active and that both interfaces are receiving traffic. It displays:

- combined or per-channel statistics;
- RX and TX counters;
- dropped events and queue utilization;
- tracked CAN identifiers;
- latest payload preview;
- full frame details on row selection;
- frame direction and age.

The screen intentionally uses a limited PSRAM-backed snapshot. This limits how many identifier rows are rendered by LVGL at once; it does not limit how many frames the drivers or router can process.

## Firmware image formats

The UDS programming workflow reads firmware images incrementally instead of
loading complete files into RAM. Supported formats are:

| Format | Extensions | Address source | Segments |
| --- | --- | --- | --- |
| Raw binary | `.bin` | Address entered by the user | One contiguous segment |
| Intel HEX | `.hex`, `.ihex` | Extended address records | Multiple segments |
| Motorola S-record | `.srec`, `.s19`, `.s28`, `.s37`, `.mot` | S-record addresses | Multiple segments |
| BHX | `.bhx` | Big-endian `SHDR` headers | Multiple sections |

BHX files begin with a 12-byte `GHDR` containing the format version and total
firmware-data size. Each following `SHDR` contains its own load address and
data size. The parser accepts version `1`, validates the `0xC0DECAFE` marker,
checks the sum of all section sizes, rejects truncated or trailing data, and
prevents address overflow and overlapping sections.

Every decoded segment is programmed independently with `RequestDownload`,
`TransferData`, and `RequestTransferExit`. Programming-session setup,
SecurityAccess, erase and verification routines, ECU reset, cancellation, and
journal creation remain coordinated for the complete image.

### Interrupted ECU programming resume

Automatic UDS programming stores a small atomic checkpoint at:

```text
/sdcard/logs/firmware/uds-resume.json
```

The checkpoint is updated only after the ECU positively acknowledges
`RequestTransferExit` for a complete image segment. It records the firmware
path and size, parsed segment count, number of confirmed segments, and the
acknowledged-block counter. SecurityAccess keys and seed/key algorithm data are
never written to it.

After an unexpected reset, power interruption, transport failure, or other
programming error, the UDS Programming page presents two explicit actions:

- **Resume interrupted programming** validates the original file and parsed
  image layout, re-enters the programming session, repeats SecurityAccess when
  configured, skips erase, and continues at the first incomplete segment;
- **Discard saved progress** removes the checkpoint and allows a new full
  programming attempt.

An interrupted segment is transferred again from its beginning. Consequently,
a raw BIN image, which is represented by one segment, restarts its transfer
from offset zero. This conservative behavior avoids assuming that an arbitrary
ECU supports partial-range `RequestDownload`. Successful programming and an
explicit cancellation remove the checkpoint; failures retain it for recovery.

### UDS addressing and service coverage

The manual UDS channel supports both physical and functional request
identifiers. Functional traffic is restricted to ISO-TP Single Frames, as a
multi-frame functional exchange cannot safely coordinate Flow Control from
multiple ECUs. The configured response identifier selects one physical ECU for
stateful requests.

The diagnostics page also provides separate multi-responder functional
discovery. It broadcasts a positive-response Tester Present request and
collects Single Frame responses from every ECU in a configurable CAN-ID range.
The result table shows each responder identifier, positive or negative result,
response time, and raw UDS payload. Multi-frame functional responses are not
collected; a physical channel must be opened for a complete ISO-TP exchange
with the selected ECU.

Typed allocation-free builders and client operations cover session control,
reset, Tester Present, SecurityAccess, DID reads and writes, DTC services,
memory reads and bounded writes, communication and DTC control, input/output
control, routines, scaling data, upload, and the download-transfer sequence.
Services whose records are OEM-specific remain accessible through the bounded
raw request editor without embedding manufacturer assumptions in the protocol
layer.

### XCP master

XCP runs directly over CAN or CAN FD and does not use ISO-TP. The service owns
four independent sessions, matches CAN transmission confirmations and slave
responses, enforces one outstanding CTO per session, and reports XCP errors,
timeouts, cancellation, command counters, and queue statistics.

The browser XCP console provides connection and discovery, MTA-based upload and
download, slave block-mode DOWNLOAD/PROGRAM, explicit address ranges for
memory writes, CAL/PAG page control, GET_SEED/UNLOCK, dynamic DAQ allocation
and list control, raw DTO observation, STIM transmission, and programming
commands.

The console parses A2L files locally without uploading them to the device.
Addressable `MEASUREMENT` objects can be mapped to a DTO PID and byte offset.
Incoming DTO values are decoded using the A2L type, byte order, linear
conversion, and physical unit. Named ECU profiles preserve transport settings,
the address extension, DAQ event channel, and compact measurement mappings in
browser local storage.

Successful programming stages update a browser-persistent checkpoint containing
the ECU profile, current stage, next MTA address, and confirmed byte count.
Restoring a checkpoint only restores state: reconnect, PGM unlock, ECU-state
validation, and continuation remain explicit operator actions. Erase and data
transfer are never repeated automatically. OEM seed-to-key logic and
ECU-specific erase parameters remain external to the firmware.

The autonomous SD workflow runs in a separate device task, owns its XCP
session, validates the source image, and records each ECU-confirmed block in
`/sdcard/logs/firmware/xcp-resume.json`. After interruption it validates the
same file and image layout, skips confirmed whole blocks, and continues at the
saved address without repeating erase. OEM seed-to-key integration is still
required for ECUs whose PGM resource is protected.

## Network and Web Interface

Spectra exposes USB RNDIS and Wi-Fi interfaces. Through USB RNDIS or the device SoftAP, the local DNS server resolves:

```text
http://spectra.device/
```

On a local Wi-Fi network, the device advertises a unique mDNS hostname:

```text
http://spectra-XXXXXX.local/
```

The USB device address is:

```text
http://172.16.10.1/
```

The file manager is available at:

```text
http://spectra.device/files
```

The CAN WebSocket endpoint is:

```text
ws://spectra.device/ws/can
```

The complete wire format is described in
[docs/websocket-can-protocol.md](docs/websocket-can-protocol.md).

The device diagnostics page is available at:

```text
http://spectra.device/diagnostics
```

The live CAN Logger, including traffic recording and replay controls, local
CAN Analyzer, and ISO-TP/UDS diagnostics are available at:

```text
http://spectra.device/can_logger
http://spectra.device/can_analyzer
http://spectra.device/isotp
http://spectra.device/obd2
```

Additional diagnostic and analysis tools are available at:

```text
http://spectra.device/dbc
http://spectra.device/uds_programming
http://spectra.device/xcp
```

CAN events are sent in a compact versioned binary protocol using little-endian multibyte fields. JSON control commands configure subscriptions and pause or resume streaming without reconnecting.

### REST API

Detailed request and response documentation is available in
[docs/web-api.md](docs/web-api.md).

| Method | Endpoint | Description |
| --- | --- | --- |
| `GET` | `/api/system` | Read system, CPU, memory, storage, and reset information |
| `GET` | `/api/diagnostics` | Read heap, FreeRTOS tasks, SD speed, CAN overflow/drop, queues, WebSocket, and logger diagnostics |
| `POST` | `/api/diagnostics/sd-benchmark` | Start an asynchronous SD-card benchmark |
| `POST` | `/api/system/restart` | Request a graceful device restart |
| `GET` | `/api/network` | Read Wi-Fi, USB RNDIS, DNS, and mDNS information |
| `POST` | `/api/network/wifi/scan` | Start a Wi-Fi network scan |
| `GET` | `/api/power` | Read PMIC and battery information |
| `GET` | `/api/settings` | Read current settings |
| `PUT` | `/api/settings` | Apply device, sound, network, and CAN settings |
| `POST` | `/api/settings/save` | Save settings to internal storage |
| `POST` | `/api/settings/reload` | Reload settings from internal storage |
| `DELETE` | `/api/settings/wifi/sta/credentials` | Remove stored Station credentials |
| `GET`, `POST` | `/api/files` | List entries or manage SD-card files and folders |
| `GET` | `/api/files/download` | Download a file |
| `GET`, `POST` | `/api/can/transmit` | Read, start, and stop CAN transmission jobs |
| `GET`, `POST` | `/api/can/filters` | Read and apply hardware CAN receive filters |
| `GET`, `POST` | `/api/can/replay` | Start, pause, resume, or stop SCL/ASC replay and read progress/statistics |
| `GET`, `POST` | `/api/isotp` | Configure an ISO-TP channel and exchange payloads |
| `GET`, `POST` | `/api/uds` | Configure UDS, execute requests, program firmware, and resume or discard interrupted programming |
| `GET`, `POST` | `/api/xcp` | Configure XCP sessions, memory/block transfers, DAQ/STIM, CAL/PAG, seed/key, and programming |
| `GET`, `POST` | `/api/ota` | Read OTA state, check the backend, stage images, cancel, or restart |

## Storage and configuration

| Storage | Mount point | Purpose |
| --- | --- | --- |
| SPIFFS | `/storage` | Web resources, settings, and internal application data |
| SD card | `/sdcard` | Logs, recordings, core dumps, and user files |

Important SD-card paths include:

| Path | Purpose |
| --- | --- |
| `/sdcard/firmwares` | BIN, Intel HEX, S-record, and BHX ECU images |
| `/sdcard/logs/firmware` | Programming journals and the persistent resume checkpoint |
| `/sdcard/logs/can` | ASC and SCL CAN recordings and replay sources |
| `/sdcard/config/uds/profiles` | Persistent ECU programming profiles |
| `/sdcard/config/uds/dids` | Persistent DID catalogs |
| `/sdcard/updates` | Local Spectra OTA images |

The device configuration is stored at:

```text
/storage/device_config.json
```

Wi-Fi Station passwords are never stored in this JSON file. Credentials are stored separately in NVS, while the configuration contains only the SSID and a non-secret credential identifier.

Example CAN configuration:

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
  },
  "sound": {
    "enabled": true,
    "volume_percent": 70
  }
}
```

## Building

### Requirements

- ESP-IDF `v6.0.2`
- ESP32-S3 target hardware
- Python and tools installed by ESP-IDF
- required upstream ESP-IDF VFS fix: `4a5d1af3de15fa291169652a9e8068f660715000`

> [!IMPORTANT]
> ESP-IDF `v6.0.2` contains a null-pointer dereference in `esp_vfs_select()` that can affect the HTTP server when the SD-card FAT filesystem is unregistered. Spectra applies the corresponding upstream fix during CI builds and requires the same fix for local `v6.0.2` builds.

Prepare ESP-IDF:

```bash
cd /path/to/esp-idf
git switch --detach v6.0.2
git switch -c v6.0.2-vfs-fix
git fetch --no-recurse-submodules origin \
    4a5d1af3de15fa291169652a9e8068f660715000
git cherry-pick FETCH_HEAD
```

Configure, build, and flash:

```bash
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

To exit the serial monitor, press `Ctrl+]`.

## Roadmap

### Completed foundation

- [x] Dual Classical CAN interfaces
- [x] MCP2518FD CAN FD driver and service
- [x] Runtime configuration for both CAN channels
- [x] Shared CAN frame and event model
- [x] Central CAN router
- [x] Transmission confirmations and queue statistics
- [x] CAN monitor service
- [x] Device CAN Monitor screen
- [x] Hardware receive filters for TWAI and MCP2518FD
- [x] One-shot and periodic CAN transmission jobs
- [x] Binary CAN event WebSocket stream
- [x] High-throughput ASC and SCL CAN logger
- [x] Browser CAN Logger and CAN Analyzer
- [x] Configurable browser buffers and CSV, SCL, and ASC export
- [x] SCL/ASC CAN traffic replay with time ranges, maximum-speed mode,
  filtering, remapping, and timing policies
- [x] DBC Explorer with live signal decoding, graphs, and bit inspection
- [x] Persistent customizable device and DBC-signal dashboards
- [x] Resumable SD-card downloads using HTTP Range
- [x] ISO-TP transport over Classical CAN and CAN FD
- [x] UDS client, browser diagnostics, DID catalogs, and ECU profiles
- [x] Physical and functional UDS addressing with extended typed and raw
  service requests
- [x] Multi-responder functional UDS discovery and service-specific positive
  response decoding
- [x] Automatic UDS ECU programming with routine polling and journals
- [x] Persistent UDS programming resume from confirmed image-segment boundaries
- [x] Streaming BIN, Intel HEX, Motorola S-record, and BHX readers and validators
- [x] Stateful XCP master, block transfer, DAQ/STIM, CAL/PAG, seed/key access,
  and programming controls
- [x] Browser-side A2L-aware XCP DAQ decoding, named ECU profiles, and
  persistent operator-controlled programming checkpoints
- [x] Autonomous SD-backed XCP programming jobs with device-side whole-block
  resume
- [x] Embedded Web UI and REST API
- [x] Device diagnostics with health analysis, task/queue telemetry, reports,
  history reset, and SD benchmark control
- [x] Browser file creation, upload, deletion, preview, and SD formatting
- [x] USB RNDIS, Wi-Fi, DNS, and mDNS connectivity
- [x] MCP23017 driver, shared I/O ownership, and dual CAN termination control
- [x] Internal and SD-card storage services
- [x] Streaming OTA firmware updates from browser or `/sdcard/updates`, with
  automatic rollback
- [x] Startup and manual backend firmware availability checks
- [x] Dual-button gesture service and GUI-safe action dispatcher
- [x] Audible button feedback
- [x] Graceful shutdown, restart, and AXP313A software power-off

### Planned

- [ ] BLF import and export
- [ ] OEM SecurityAccess provider integration without storing secrets in public firmware
- [ ] Device registration and authentication
- [ ] Remote backend integration

## License

Copyright (C) 2026 Yurii Ridkovets.

Spectra is licensed under the GNU General Public License v3.0 only (`GPL-3.0-only`).

See [LICENSE](LICENSE) for the complete license terms.
