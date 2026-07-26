# Spectra

<div align="center">

![ESP-IDF Build](https://github.com/RedYu/spectra/actions/workflows/build.yml/badge.svg)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.x-E7352C?logo=espressif)
![LVGL](https://img.shields.io/badge/LVGL-v9-00AEEF)
![Platform](https://img.shields.io/badge/Platform-ESP32--S3-blue)
![License](https://img.shields.io/badge/License-MIT-green)

**Modern Automotive CAN Analyzer** <br />
Modular ESP32-S3 diagnostic platform built with ESP-IDF, FreeRTOS and LVGL 9.

</div>

---

## Overview

Spectra is an embedded automotive diagnostic platform based on the **ESP32-S3**.

The project combines a modern touch GUI (LVGL 9), modular software architecture and CAN communication capabilities. 
The long-term goal is to create a professional handheld device for real-time monitoring, logging and analysis of vehicle networks.

---

## Features

### Implemented
- ESP-IDF 6.x + FreeRTOS
- LVGL 9 graphical interface
- Layered software architecture (Drivers → Services → Models → GUI)
- ILI9488 480×320 display driver (SPI + DMA)
- GT911 capacitive touch driver
- SPIFFS storage service
- Settings service with JSON configuration
- System model (uptime, heap, firmware info)
- Splash screen + Main screen + Settings screen
- GitHub Actions CI

### Planned
- Screen Manager with navigation history
- CAN / TWAI interface
- CAN Monitor & Logger
- DBC Parser
- UDS / OBD-II support
- XCP Protocol
- Live Dashboard
- SD Card support
- Wi-Fi / USB networking
- OTA updates
- Data Recording
- SD Card Support
- Firmware Manager

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
| Flash         | 16 MB                  |

---

## Project Architecture

```text
spectra/
├── main/
│   └── main.c
├── components/
│   ├── board/           # Board configuration & pins
│   ├── drivers/         # Display, Touch, (CAN)
│   ├── services/        # Storage, Settings, System, GUI
│   ├── models/          # Shared application state
│   ├── gui/             # Screens, widgets, assets
│   └── lvgl_port/       # LVGL porting layer
├── partitions.csv
└── sdkconfig.defaults
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
- **Screen Manager** handles screen creation, navigation history and lifecycle

## Screen Manager
The GUI uses a centralized Screen Manager with the following features:

* Screen registration via descriptors
* Lazy screen creation
* Navigation history stack
* Animated transitions

Example usage:

```c
// Show a screen
screen_manager_show(SCREEN_ID_SETTINGS, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250);

// Go back
screen_manager_back(LV_SCR_LOAD_ANIM_MOVE_RIGHT, 250);
```

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
    "schema_version": 1,

    "device": {
        "target": "spectra",
		"name": "Modern Automotive CAN Analyzer"
    },
	
	"display": {
		"brightness": 80
	}
}
```

## Getting Started
###Requirements

* ESP-IDF v5.3+ / v6.x
* ESP32-S3 board with ILI9488 + GT911

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
- [x] Display & Touch drivers
- [x] LVGL 9 integration
- [x] Storage & Settings services
- [x] System model
- [x] Screen Manager
- [ ] Theme / Style system

### GUI

- [x] Splash screen
- [x] Main screen
- [ ] Settings application
- [ ] Vehicle information
- [ ] Dashboard
- [ ] CAN Monitor

### Communication

- [ ] CAN / TWAI driver
- [ ] CAN Logger
- [ ] DBC Parser
- [ ] XCP
- [ ] UDS / OBD-II

### Connectivity

- [ ] USB Networking
- [ ] Web UI
- [ ] OTA
- [ ] Wi-Fi

## License

This project is licensed under the MIT License.

See the LICENSE file for details.
