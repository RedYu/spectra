# Spectra

<div align="center">

![ESP-IDF Build](https://github.com/RedYu/spectra/actions/workflows/build.yml/badge.svg)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.x-E7352C?logo=espressif)
![LVGL](https://img.shields.io/badge/LVGL-v9-00AEEF)
![Platform](https://img.shields.io/badge/Platform-ESP32--S3-blue)
![License](https://img.shields.io/badge/License-MIT-green)

**Modern Automotive CAN Analyzer**  
A modular ESP32-S3 diagnostic platform built with ESP-IDF, FreeRTOS, and LVGL 9.

</div>

---

## Overview

Spectra is an embedded automotive diagnostic platform based on the ESP32-S3. It combines a touch interface, removable and internal storage, USB networking, and a layered firmware architecture.

The long-term goal is to provide a handheld device for real-time monitoring, recording, and analysis of automotive networks.

## Features

### Implemented

- ESP-IDF 6.x and FreeRTOS
- LVGL 9 graphical interface
- ILI9488 480x320 display over SPI with DMA
- GT911 capacitive touch controller over shared I²C
- Centralized screen manager with lazy creation and navigation history
- Splash, main, and settings screens
- Shared GUI theme and reusable LVGL style system
- Selectable Light and Dark GUI themes
- Internal SPIFFS and removable SD-card storage
- Shared SPI-bus synchronization
- JSON-based settings model and service
- Thread-safe system model and runtime metrics
- UART and optional SD-card file logging
- Periodic SD-card log flushing
- Core-dump export from flash to SD card
- USB RNDIS, Wi-Fi SoftAP, and Wi-Fi Station networking
- Wi-Fi network scanning and credential storage in NVS
- DHCP, local DNS, and mDNS discovery
- Internet connectivity monitoring through the Spectra backend
- AXP313A power-management driver and service
- Embedded Web UI and REST API
- Browser-based internal-storage and SD-card file manager
- GitHub Actions build workflow

### Planned

- CAN/TWAI interfaces
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
| MCU | ESP32-S3 |
| Display | ILI9488, 480x320, SPI |
| Touch | GT911, I²C |
| Automotive communication | CAN/TWAI planned |
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
│   ├── drivers/               # Display, touch, backlight, and SD drivers
│   ├── services/              # System, storage, settings, logging, USB, and web services
│   ├── models/                # Thread-safe application state
│   ├── gui/                   # Screens, widgets, shared themes, styles, and assets
│   └── lvgl_port/             # LVGL display and input integration
├── partitions.csv
└── sdkconfig.defaults
```

- **GUI Theme** defines Light and Dark color palettes, fonts, spacing, radii, and component-specific visual tokens.
- **GUI Styles** provides reusable LVGL styles shared by screens, toolbars, dialogs, cards, inputs, and buttons.

The application follows a layered architecture:

```text
Application startup
        │
        ▼
GUI and web interfaces
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
- **Services** coordinate application logic and resource ownership.
- **Models** hold synchronized application state.
- **GUI** presents model data and sends user actions to services.
- **Screen Manager** controls screen creation, navigation, and lifecycle.

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

## Storage

Spectra uses separate internal and removable storage services.

| Storage | Mount point | Purpose |
| --- | --- | --- |
| SPIFFS | `/storage` | Web resources, settings, and internal application data |
| SD card | `/sdcard` | Logs, recordings, and user files |

Storage operations validate paths and serialize access. SD-card operations also coordinate access to the shared SPI bus.

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
    "sd_enabled": true
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
  }
}
```

The GUI supports `light` and `dark` themes. Theme changes are applied after restarting the device.

Wi-Fi Station passwords are not stored in the JSON configuration. Credentials are stored separately in NVS, while the configuration contains only the selected SSID and a non-secret credential identifier.

## Network and Web Interface

Spectra exposes a USB RNDIS network interface. The device provides network configuration to the connected computer through DHCP.

| Setting | Value |
| --- | --- |
| Device address | `172.16.10.1` |
| Client subnet | `172.16.10.x` |
| Local hostname | `spectra.device` |

When connected through a local Wi-Fi network, the device can also be accessed using its unique mDNS hostname:

```text
http://spectra-XXXXXX.local/
```

After connecting the device, open either address:

```text
http://spectra.device/
http://172.16.10.1/
```

The file browser is available at:

```text
http://spectra.device/files
```

Current HTTP endpoints include:

| Method | Endpoint | Description |
| --- | --- | --- |
| `GET` | `/api/system` | Read system, CPU, memory, and storage information |
| `GET` | `/api/network` | Read Wi-Fi, USB RNDIS, DNS, and mDNS information |
| `GET` | `/api/power` | Read AXP313A power-management information |
| `GET` | `/api/settings` | Read current settings |
| `PUT` | `/api/settings` | Update settings |
| `POST` | `/api/settings/save` | Save settings to internal storage |
| `GET` | `/api/files` | List internal-storage or SD-card entries |
| `GET` | `/api/files/download` | Download a file |

## Software Stack

- ESP-IDF 6.x
- FreeRTOS
- LVGL 9
- `esp_lcd`
- TinyUSB
- ESP-IDF HTTP server
- cJSON
- SPIFFS
- FAT filesystem and SDSPI
- CMake

## Getting Started

### Requirements

- ESP-IDF 6.x
- Python and tools installed by ESP-IDF
- ESP32-S3 target hardware
- ILI9488 display and GT911 touch controller for the complete GUI

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
- [x] Screen manager
- [x] Shared GUI theme and style system

### User Interface

- [x] Splash screen
- [x] Main screen
- [x] Settings screen
- [x] SD-card management dialog
- [x] Browser-based file manager
- [ ] Vehicle information screen
- [ ] Live dashboard
- [ ] CAN monitor

### Automotive Communication

- [ ] CAN/TWAI drivers
- [ ] CAN logger
- [ ] DBC parser
- [ ] UDS and OBD-II
- [ ] XCP

### Connectivity

- [x] USB RNDIS networking
- [x] Wi-Fi SoftAP
- [x] Wi-Fi Station
- [x] Wi-Fi network scanning
- [x] DHCP and local DNS services
- [x] mDNS discovery
- [x] Internet connectivity monitoring
- [x] Embedded Web UI
- [ ] Bluetooth
- [ ] OTA updates
- [ ] Device registration
- [ ] Remote backend

## License

This project is licensed under the MIT License. See the [`LICENSE`](LICENSE) file for details.
