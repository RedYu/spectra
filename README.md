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

The primary channel uses the ESP32-S3 TWAI controller for Classical CAN. The secondary channel uses an external MCP2518FD controller and supports both Classical CAN and CAN FD. Traffic from both interfaces is normalized into a shared frame model and distributed through a central CAN router to monitoring, WebSocket streaming, and future logging and diagnostic services.

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
- Passive buzzer with configurable volume and asynchronous signals
- Internal SPIFFS and removable SD-card storage
- Selected task stacks and buffers allocated in PSRAM
- Centralized application task-priority configuration

### CAN communication

- Primary Classical CAN interface using ESP32-S3 TWAI
- Secondary Classical CAN and CAN FD interface using MCP2518FD over SPI
- TCAN1042HGV transceivers for both physical CAN channels
- Runtime enable, disable, bitrate, and listen-only configuration
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

### Storage and configuration

- JSON-based settings model and service
- Primary and Secondary CAN settings in the GUI and Web API
- Wi-Fi, display, sound, logging, and UI settings
- Settings persistence before controlled restart and shutdown
- UART and optional SD-card file logging
- Configurable per-tag ESP-IDF log levels
- Periodic SD-card log flushing
- Core-dump export from flash to SD card
- Graceful service shutdown and SD-card unmounting
- Browser-based internal-storage and SD-card file manager

### Connectivity

- USB RNDIS network interface
- Wi-Fi SoftAP and Station modes
- Wi-Fi network scanning while Station reconnection is active
- Wi-Fi credentials stored separately in NVS
- DHCP and local DNS services
- Per-device mDNS hostname
- Embedded Web UI and REST API
- Internet connectivity checks through the Spectra backend
- Human-readable Wi-Fi disconnection reasons

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
| Battery monitoring | ADC voltage measurement through a resistor divider |
| Audible feedback | Passive PWM-controlled buzzer |
| Internal storage | SPIFFS |
| Removable storage | SD card over SPI |
| USB connectivity | USB RNDIS network interface |
| Flash | 16 MB |

## Architecture

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
             /     |      \
            v      v       v
      CAN Monitor  WebSocket  Future consumers
                              Logger / UDS / XCP
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
│   ├── lvgl_port/              # LVGL display and input integration
│   ├── models/                 # Thread-safe application state
│   └── services/               # Application services and Web APIs
├── spiffs_data/                # Embedded Web UI and default configuration
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

The development CAN stream page is available at:

```text
http://spectra.device/can_test
```

CAN events are sent in a compact versioned binary protocol using little-endian multibyte fields. JSON control commands configure subscriptions and pause or resume streaming without reconnecting.

### REST API

| Method | Endpoint | Description |
| --- | --- | --- |
| `GET` | `/api/system` | Read system, CPU, memory, storage, and reset information |
| `POST` | `/api/system/restart` | Request a graceful device restart |
| `GET` | `/api/network` | Read Wi-Fi, USB RNDIS, DNS, and mDNS information |
| `POST` | `/api/network/wifi/scan` | Start a Wi-Fi network scan |
| `GET` | `/api/power` | Read PMIC and battery information |
| `GET` | `/api/settings` | Read current settings |
| `PUT` | `/api/settings` | Apply device and CAN settings |
| `POST` | `/api/settings/save` | Save settings to internal storage |
| `POST` | `/api/settings/reload` | Reload settings from internal storage |
| `DELETE` | `/api/settings/wifi/sta/credentials` | Remove stored Station credentials |
| `GET` | `/api/files` | List internal-storage or SD-card entries |
| `GET` | `/api/files/download` | Download a file |

## Storage and configuration

| Storage | Mount point | Purpose |
| --- | --- | --- |
| SPIFFS | `/storage` | Web resources, settings, and internal application data |
| SD card | `/sdcard` | Logs, recordings, core dumps, and user files |

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
- [x] Binary CAN event WebSocket stream
- [x] Embedded Web UI and REST API
- [x] USB RNDIS, Wi-Fi, DNS, and mDNS connectivity
- [x] Internal and SD-card storage services
- [x] Graceful shutdown and restart

### Planned

- [ ] High-throughput CAN logger with PSRAM buffering
- [ ] ASC and CSV export
- [ ] BLF support
- [ ] DBC parsing
- [ ] UDS and OBD-II diagnostics
- [ ] XCP support
- [ ] CAN traffic replay
- [ ] Live signal dashboards
- [ ] OTA firmware updates
- [ ] Device registration and authentication
- [ ] Remote backend integration

## License

Copyright (C) 2026 Yurii Ridkovets.

Spectra is licensed under the GNU General Public License v3.0 only (`GPL-3.0-only`).

See [LICENSE](LICENSE) for the complete license terms.
