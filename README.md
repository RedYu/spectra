# Spectra

<div align="center">

![ESP-IDF Build](https://github.com/RedYu/spectra/actions/workflows/build.yml/badge.svg)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.x-E7352C?logo=espressif)
![LVGL](https://img.shields.io/badge/LVGL-v9-00AEEF)
![Platform](https://img.shields.io/badge/Platform-ESP32--S3-blue)
![License](https://img.shields.io/badge/License-MIT-green)

Modern Automotive CAN Analyzer <br />
Modular ESP32-S3 automotive diagnostic platform built with ESP-IDF, FreeRTOS and LVGL.

</div>

---

## Overview

Spectra is an embedded automotive diagnostic platform based on the ESP32-S3.

The project combines a modern LVGL graphical interface, CAN communication, and a modular software architecture built on ESP-IDF and FreeRTOS.

The long-term goal is to create a professional handheld automotive diagnostic device capable of monitoring, logging, analyzing, and visualizing vehicle communication in real time.

---

## Features

- ESP-IDF 6.x
- FreeRTOS
- LVGL 9
- Layered software architecture
- Storage Service
- Settings Service
- Configuration Model
- SPIFFS support
- ILI9488 display driver
- GT911 touch driver
- CAN / TWAI interface
- GitHub Actions CI

## Planned Features

* CAN Monitor
* CAN Logger
* DBC Parser
* XCP Protocol
* UDS Diagnostics
* OBD-II Support
* Live Dashboard
* Data Recording
* SD Card Support
* Wi-Fi
* USB Networking
* OTA Updates
* Firmware Manager

---

## Hardware

| Component     | Description            |
| ------------- | ---------------------- |
| MCU           | ESP32-S3               |
| Display       | ILI9488 480×320 SPI    |
| Touch         | GT911                  |
| Communication | CAN / TWAI             |
| Storage       | SD Card (planned)      |
| Networking    | USB / Wi-Fi / Ethernet |

---

## Project Architecture

```text
spectra/
├── main/
│   ├── app_main.c
│   ├── app_config.h
│   └── app_events.h
│
└── components/
    ├── board/
    │   ├── board.c
    │   └── board_config.h
    │
    ├── drivers/
    │   ├── display_driver
    │   ├── touch_driver
    │   └── can_driver
    │
    ├── services/
    │   ├── storage_service
    │   ├── settings_service
    │   ├── gui_service
    │   └── can_service
    │
    ├── models/
    │   ├── settings_model
    │   ├── system_model
    │   └── can_model
    │
    ├── gui/
    │   ├── screens
    │   ├── widgets
    │   └── assets
    │
    └── lvgl_port/
```

## Software Architecture

The application follows a layered architecture.

```
Application
      │
      ▼
 GUI (LVGL)
      │
      ▼
 Services
      │
      ▼
 Models
      │
      ▼
 Drivers
      │
      ▼
 Hardware
```

Responsibilities:

- **Drivers** provide hardware abstraction.
- **Services** implement application logic.
- **Models** store shared application state.
- **GUI** only reads data from models.

## Software Stack

* ESP-IDF
* FreeRTOS
* LVGL 9
* esp_lcd
* SPI DMA
* CMake

---

## Storage

The project uses two independent storage layers.

| Storage | Purpose |
|----------|---------|
| NVS | Persistent system settings |
| SPIFFS | Configuration files and application resources |

Configuration is loaded from:

```
/storage/device_config.json
```

## Configuration

Application settings are stored in JSON format.

Example:

```json
{
    "display": {
        "brightness": 80
    },

    "can": {
        "bitrate": 500000
    }
}
```

## Development

Build

```bash
idf.py build
```

Flash

```bash
idf.py flash
```

Monitor

```bash
idf.py monitor
```

Clean

```bash
idf.py fullclean
```

## Roadmap

### Core

- [x] Modular architecture
- [x] Display driver
- [x] Touch driver
- [x] LVGL integration
- [x] Storage service
- [x] Settings service
- [x] Configuration manager

### GUI

- [ ] Dashboard
- [ ] Settings application
- [ ] CAN monitor
- [ ] Vehicle information

### Communication

- [ ] CAN Logger
- [ ] DBC Parser
- [ ] XCP
- [ ] UDS
- [ ] OBD-II

### Connectivity

- [ ] USB Networking
- [ ] Web UI
- [ ] OTA
- [ ] Wi-Fi

## License

This project is licensed under the MIT License.

See the LICENSE file for details.
