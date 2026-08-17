# Spectra

<div align="center">

![ESP-IDF Build](https://github.com/RedYu/spectra/actions/workflows/build.yml/badge.svg)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0.2%20%2B%20VFS%20fix-E7352C?logo=espressif)
![LVGL](https://img.shields.io/badge/LVGL-v9-00AEEF)
![Platform](https://img.shields.io/badge/Platform-ESP32--S3-blue)
![License](https://img.shields.io/badge/License-MIT-green)

**Modern Automotive CAN Analyzer**  
A modular ESP32-S3 diagnostic platform built with ESP-IDF, FreeRTOS, and LVGL 9.

</div>

---

## Overview

Spectra is a handheld automotive diagnostic platform based on the ESP32-S3. It combines a touch interface, Classical CAN communication, removable and internal storage, USB and Wi-Fi networking, power monitoring, and a layered firmware architecture.

The project is designed for real-time monitoring, recording, and analysis of automotive networks. The current firmware provides the platform services and the first primary CAN interface; higher-level diagnostic protocols and CAN FD support remain under development.

## Features

### Implemented

- ESP-IDF 6.x and FreeRTOS
- LVGL 9 graphical interface
- ILI9488 480x320 display over SPI with DMA
- GT911 capacitive touch controller over a shared I²C bus
- Centralized screen manager with lazy creation and navigation history
- Splash, main, and settings screens
- Shared GUI theme and reusable LVGL style system
- Selectable Light and Dark GUI themes
- Audible GUI feedback through a passive buzzer
- Internal SPIFFS and removable SD-card storage
- Shared SPI-bus synchronization
- JSON-based settings model and service
- Thread-safe system model and runtime metrics
- UART and optional SD-card file logging
- Configurable per-tag ESP-IDF log levels
- Periodic SD-card log flushing
- Core-dump export from flash to SD card
- Graceful device restart with service shutdown and SD-card unmounting
- USB RNDIS, Wi-Fi SoftAP, and Wi-Fi Station networking
- Wi-Fi network scanning and credential storage in NVS
- DHCP, local DNS, and mDNS discovery
- Internet connectivity checks through the Spectra backend
- AXP313A power-management driver and service
- Battery-voltage ADC driver and battery service
- Primary Classical CAN interface using ESP32-S3 TWAI
- Runtime CAN enable, bitrate, and Listen-only configuration
- Embedded Web UI and REST API
- Browser-based internal-storage and SD-card file manager
- GitHub Actions build workflow

### Planned

- MCP2518FD secondary CAN FD interface
- Live CAN monitor and logger
- DBC parsing
- UDS and OBD-II support
- XCP support
- Live dashboards
- Bluetooth connectivity
- OTA firmware updates
- Device registration and authentication
- Remote backend integration

## Hardware

| Component | Description |
| --- | --- |
| MCU | ESP32-S3, 240 MHz |
| Display | ILI9488, 480x320, SPI |
| Touch | GT911, shared I2C bus |
| Primary CAN | ESP32-S3 TWAI with TCAN1042HGV transceiver |
| Secondary CAN FD | MCP2518FD with TCAN1042HGV, planned |
| Power management | AXP313A PMIC |
| Battery monitoring | ADC voltage measurement through a resistor divider |
| Audible feedback | Passive low-level-trigger buzzer |
| Internal storage | SPIFFS |
| Removable storage | SD card over SPI |
| USB connectivity | USB RNDIS network interface |
| Flash | 16 MB |

## Project Architecture

```text
spectra/
├── main/
│   └── main.c
├── components/
│   ├── board/                 # Board initialization and shared buses
│   ├── drivers/               # Hardware-specific drivers
│   ├── services/              # Application logic and resource ownership
│   ├── models/                # Thread-safe application state
│   ├── gui/                   # Screens, widgets, themes, styles, and assets
│   └── lvgl_port/             # LVGL display and input integration
├── spiffs_data/               # Embedded Web UI and default configuration
├── partitions.csv
└── sdkconfig.defaults
```

The application follows a layered architecture:

```text
Application startup
        │
        ▼
GUI and Web API
        │
        ▼
Application services
        │
        ▼
Shared models
        │
        ▼
Hardware drivers
        │
        ▼
Hardware
```

- **Drivers** provide hardware-specific operations.
- **Services** coordinate application logic and own long-lived resources.
- **Models** hold synchronized application state.
- **GUI** presents model data and sends user actions to services.
- **Web API** exposes device state and settings to the embedded Web UI.
- **Screen Manager** controls screen creation, navigation, and lifecycle.
- **GUI Theme** defines Light and Dark palettes, fonts, spacing, radii, and component-specific visual tokens.
- **GUI Styles** provides reusable LVGL styles for screens, toolbars, dialogs, cards, inputs, and buttons.

## Screen Manager

The centralized screen manager provides:

- descriptor-based screen registration;
- lazy screen creation;
- cached screen instances;
- navigation history;
- optional animated transitions;
- screen lifecycle callbacks.

Example:

```c
screen_manager_show(
    SCREEN_ID_SETTINGS,
    LV_SCR_LOAD_ANIM_MOVE_LEFT,
    250U
);

screen_manager_back(
    LV_SCR_LOAD_ANIM_MOVE_RIGHT,
    250U
);
```

## Storage and Configuration

| Storage | Mount point | Purpose |
| --- | --- | --- |
| SPIFFS | `/storage` | Web resources, settings, and internal application data |
| SD card | `/sdcard` | Logs, recordings, core dumps, and user files |

Storage operations validate paths and serialize access. SD-card operations also coordinate access to the shared SPI bus. During a graceful restart, file logging is drained and closed before the SD card is unmounted.

The current settings file is stored at:

```text
/storage/device_config.json
```

Example configuration:

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
      "warning": "wifi,wifi_init",
      "info": "",
      "debug": "",
      "disabled": ""
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
      "password": "spectra123"
    },
    "wifi_sta": {
      "enabled": false,
      "ssid": "",
      "credential_id": ""
    },
    "usb_rndis": {
      "enabled": true
    }
  },
  "can": {
    "primary": {
      "enabled": false,
      "bitrate": 500000,
      "listen_only": true
    }
  }
}
```

The GUI supports `light` and `dark` themes. Theme changes are applied after restarting the device.

Wi-Fi Station passwords are never stored in the JSON configuration. Credentials are stored separately in NVS, while the configuration contains only the selected SSID and a non-secret credential identifier.

## Primary CAN

The primary Classical CAN interface uses the ESP32-S3 TWAI controller and an external TCAN1042HGV transceiver.

Supported nominal bitrates:

- 50 kbit/s
- 100 kbit/s
- 125 kbit/s
- 250 kbit/s
- 500 kbit/s
- 800 kbit/s
- 1 Mbit/s

The interface can be enabled, disabled, or reconfigured at runtime without restarting the device. Listen-only mode allows passive bus monitoring without transmitting frames or acknowledgements.

The secondary MCP2518FD interface is reserved for future CAN FD support.

## Network and Web Interface

Spectra exposes USB RNDIS and Wi-Fi network interfaces. When connected through USB RNDIS or the device SoftAP, the local DNS service resolves `spectra.device` to the appropriate device address.

| Setting | Value |
| --- | --- |
| USB device address | `172.16.10.1` |
| USB client subnet | `172.16.10.x` |
| Local DNS name | `spectra.device` |

When connected through a local Wi-Fi network, the device can also be accessed using its unique mDNS hostname:

```text
http://spectra-XXXXXX.local/
```

Common local addresses:

```text
http://spectra.device/
http://172.16.10.1/
```

The file manager is available at:

```text
http://spectra.device/files
```

### REST API

| Method | Endpoint | Description |
| --- | --- | --- |
| `GET` | `/api/system` | Read system, CPU, memory, storage, and reset information |
| `POST` | `/api/system/restart` | Request a graceful device restart |
| `GET` | `/api/network` | Read Wi-Fi, USB RNDIS, DNS, and mDNS information |
| `POST` | `/api/network/wifi/scan` | Start a Wi-Fi network scan |
| `GET` | `/api/power` | Read PMIC and battery information |
| `GET` | `/api/settings` | Read current settings |
| `PUT` | `/api/settings` | Apply settings, including Primary CAN configuration |
| `POST` | `/api/settings/save` | Save settings to internal storage |
| `POST` | `/api/settings/reload` | Reload settings from internal storage |
| `DELETE` | `/api/settings/wifi/sta/credentials` | Remove stored Station credentials |
| `GET` | `/api/files` | List internal-storage or SD-card entries |
| `GET` | `/api/files/download` | Download a file |

## Software Stack

- ESP-IDF 6.x
- FreeRTOS
- LVGL 9
- `esp_lcd`
- ESP-IDF TWAI driver
- TinyUSB
- ESP-IDF HTTP server and HTTP client
- cJSON
- SPIFFS
- FAT filesystem and SDSPI
- mDNS and lwIP
- CMake

## Getting Started

### Requirements

- ESP-IDF `v6.0.2`
- Required upstream VFS fix:
  `4a5d1af3de15fa291169652a9e8068f660715000`
- Python and tools installed by ESP-IDF
- ESP32-S3 target hardware
- ILI9488 display and GT911 touch controller for the complete GUI

> [!IMPORTANT]
> The original ESP-IDF `v6.0.2` release contains a null-pointer
> dereference in `esp_vfs_select()`. It can crash the HTTP server when
> the SD-card FAT filesystem is unregistered.
>
> Spectra currently requires the corresponding upstream VFS fix to be
> applied on top of ESP-IDF `v6.0.2`.

### Prepare ESP-IDF

Check out ESP-IDF `v6.0.2` and apply the required upstream VFS fix:

```bash
cd /path/to/esp-idf

git switch --detach v6.0.2
git switch -c v6.0.2-vfs-fix

git fetch --no-recurse-submodules origin \
    4a5d1af3de15fa291169652a9e8068f660715000

git cherry-pick FETCH_HEAD
```

### Configure

```bash
idf.py set-target esp32s3
idf.py menuconfig
```

### Build

```bash
idf.py build
```

### Flash and monitor

```bash
idf.py flash monitor
```

To exit the serial monitor, press `Ctrl+]`.

### Clean

```bash
idf.py fullclean
```

## Roadmap

### Platform

- [x] Layered firmware architecture
- [x] Display and touch drivers
- [x] LVGL 9 integration
- [x] Internal and SD-card storage
- [x] Settings and system services
- [x] Logging service
- [x] Graceful restart service
- [x] Screen manager
- [x] Shared GUI theme and style system
- [x] AXP313A power management
- [x] Battery-voltage monitoring
- [x] Passive buzzer service and GUI feedback

### User Interface

- [x] Splash screen
- [x] Main screen
- [x] Settings screen
- [x] Light and Dark themes
- [x] SD-card management dialog
- [x] Browser-based file manager
- [ ] Vehicle information screen
- [ ] Live dashboard
- [ ] Live CAN monitor

### Automotive Communication

- [x] Primary Classical CAN/TWAI driver
- [x] Primary CAN service
- [x] Runtime CAN configuration
- [ ] MCP2518FD CAN FD driver and service
- [ ] CAN logger
- [ ] DBC parser
- [ ] UDS and OBD-II
- [ ] XCP
- [ ] LIN support

### Connectivity

- [x] USB RNDIS networking
- [x] Wi-Fi SoftAP
- [x] Wi-Fi Station
- [x] Wi-Fi network scanning
- [x] DHCP and local DNS services
- [x] mDNS discovery
- [x] Internet connectivity checks
- [x] Embedded Web UI and REST API
- [ ] Bluetooth
- [ ] OTA updates
- [ ] Device registration
- [ ] Remote backend

## License

This project is licensed under the MIT License. See the [`LICENSE`](LICENSE) file for details.
