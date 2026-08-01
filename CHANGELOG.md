# Changelog

All notable changes to Spectra will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Main and settings screens.
- Centralized screen manager with lazy screen creation, lifecycle callbacks, and navigation history.
- Reusable toolbar, modal dialog, and SD-card management widgets.
- Thread-safe settings and system models.
- System service with uptime, heap, and interval-based CPU-usage metrics.
- Internal SPIFFS storage service.
- SD-card driver and storage service with shared SPI-bus synchronization.
- UART logging service with optional SD-card file output.
- JSON-based settings loading, validation, application, and persistence.
- USB RNDIS network interface.
- DHCP server for the `172.16.10.x` subnet.
- Local DNS server with `spectra.device` name resolution.
- Embedded HTTP server and JSON API for system information and settings.
- Browser-based file listing for internal storage and SD cards.
- File downloads with streaming SD-card reads and UTF-8 filenames.
- Microsoft OS 1.0 descriptors for RNDIS compatibility.

### Changed

- Reworked application startup into ordered service initialization with GUI progress reporting.
- Improved splash-screen behavior so navigation waits for startup completion.
- Improved display DMA flush synchronization on the shared SPI bus.
- Added explicit error handling and bounded lock timeouts across services and models.
- Updated GUI animation configuration to use atomic state.
- Updated project documentation to describe the current architecture, USB networking, Web UI, and contribution workflow.

### Fixed

- Prevented repeated SPI-bus release for a single LVGL display flush.
- Prevented stale CPU-usage samples after restarting the system service.
- Added cooperative system-service shutdown to avoid task-memory leaks.
- Improved cleanup of storage, logging, GUI, and screen resources on failure paths.
- Corrected path validation and error mapping for internal-storage and SD-card operations.

## [0.1.0] - 2026-07

### Added

- Initial ESP-IDF project for ESP32-S3.
- FreeRTOS-based application foundation.
- LVGL 9 integration.
- ILI9488 480×320 display driver with SPI DMA transfers.
- GT911 capacitive touch driver.
- Display backlight PWM driver.
- Initial splash screen.
- Initial layered firmware architecture for drivers, services, models, and GUI components.
- GitHub Actions build workflow.

[Unreleased]: https://github.com/RedYu/spectra/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/RedYu/spectra/releases/tag/v0.1.0
