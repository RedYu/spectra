---
name: Bug report
about: Report a reproducible problem in Spectra
title: "[BUG] "
labels: bug
assignees: ""
---

## Description

Provide a clear and concise description of the problem.

## Steps to Reproduce

1.
2.
3.

## Expected Behavior

Describe what you expected to happen.

## Actual Behavior

Describe what happened instead. Include any visible error messages, resets, freezes, corrupted output, or unexpected device state.

## Reproduction Rate

- [ ] Every time
- [ ] Frequently
- [ ] Intermittently
- [ ] Only once

Approximate frequency or timing:

## Regression

Did this work in an earlier version or commit?

- Last known working version or commit:
- First known affected version or commit:

## Hardware

- ESP32-S3 module:
- Board or hardware revision:
- Flash and PSRAM size:
- Display:
- Touch controller:
- SD card model, capacity, and filesystem:
- USB connection or hub:
- CAN transceiver or other peripherals:
- Power supply:
- Additional hardware:

## Software Environment

- Spectra version or Git commit:
- ESP-IDF version (`idf.py --version`):
- LVGL version:
- Host operating system:
- Compiler/toolchain version:
- Browser and version, if relevant:
- Relevant `sdkconfig` changes:
- Local component overrides, if any:

## Affected Area

- [ ] Startup or initialization
- [ ] Display or LVGL
- [ ] Touch input
- [ ] GUI navigation
- [ ] Internal storage
- [ ] SD card
- [ ] Settings
- [ ] Logging
- [ ] USB RNDIS
- [ ] DHCP or DNS
- [ ] Web UI or HTTP API
- [ ] CAN or automotive communication
- [ ] Build system or CI
- [ ] Other

## Logs

Paste the smallest complete log that shows the problem, including several lines before the first error.

```text
Paste relevant serial, build, browser-console, or network logs here.
```

## Screenshots or Recordings

Attach screenshots, browser developer-tool output, packet captures, or a short recording when they help explain the problem.

## Additional Context

Add any other information that may help reproduce or diagnose the issue.

## Checklist

- [ ] I searched existing issues for duplicates
- [ ] I can reproduce the issue using the steps above
- [ ] I tested a clean build when practical
- [ ] I removed passwords, private keys, serial numbers, MAC addresses, and other sensitive information from logs
