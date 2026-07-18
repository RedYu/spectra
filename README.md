# Spectra

<div align="center">

![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.x-E7352C?logo=espressif)
![LVGL](https://img.shields.io/badge/LVGL-v9-00AEEF)
![Platform](https://img.shields.io/badge/Platform-ESP32--S3-blue)
![License](https://img.shields.io/badge/License-MIT-green)

Modern Automotive CAN Analyzer

</div>

---

## Overview

Spectra is an embedded automotive diagnostic platform based on the ESP32-S3.

The project combines a modern LVGL graphical interface, CAN communication, and a modular software architecture built on ESP-IDF and FreeRTOS.

The long-term goal is to create a professional handheld automotive diagnostic device capable of monitoring, logging, analyzing, and visualizing vehicle communication in real time.

---

## Features

* ESP-IDF framework
* LVGL 9 graphical interface
* ILI9488 display driver
* GT911 capacitive touch support
* FreeRTOS multitasking
* Modular architecture
* CAN / TWAI communication
* Splash screen
* Settings interface
* Designed for future Ethernet and USB networking

---

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

## Project Structure

```text
drivers/
    Display driver
    Touch driver

gui/
    LVGL port
    Screens
    Widgets

services/
    GUI service
    CAN service
    Storage service
    Network service

main/
    Application entry point

assets/
    Images
    Icons
    Fonts
```

---

## Software Stack

* ESP-IDF
* FreeRTOS
* LVGL 9
* esp_lcd
* SPI DMA
* CMake

---

## Build

```bash
idf.py build
```

Flash:

```bash
idf.py flash
```

Monitor:

```bash
idf.py monitor
```

---

## Roadmap

* [x] Display Driver
* [x] Touch Driver
* [x] LVGL Integration
* [x] Splash Screen
* [ ] Main Dashboard
* [ ] Settings
* [ ] CAN Monitor
* [ ] CAN Logger
* [ ] XCP Support
* [ ] UDS Support
* [ ] USB Networking
* [ ] OTA

---

## License

This project is licensed under the MIT License.

See the LICENSE file for details.
