## Summary

Briefly describe what this pull request changes and why the change is needed.

## Related Issues

Closes #

<!-- Remove this section when there is no related issue. -->

## Type of Change

- [ ] Bug fix
- [ ] New feature
- [ ] Refactoring without a behavior change
- [ ] Performance improvement
- [ ] Documentation update
- [ ] Build, dependency, or configuration change
- [ ] CI change
- [ ] Breaking change

## Affected Areas

- [ ] Board support or shared buses
- [ ] Hardware drivers
- [ ] Display, touch, or LVGL port
- [ ] GUI screens, widgets, themes, or styles
- [ ] Models or application services
- [ ] Internal storage or SD card
- [ ] Logging or crash dumps
- [ ] Power management, battery, or buzzer
- [ ] Wi-Fi, USB RNDIS, DNS, or mDNS
- [ ] Web UI or REST API
- [ ] Classical CAN/TWAI
- [ ] CAN FD/MCP2518FD
- [ ] Automotive protocols
- [ ] OTA or firmware management
- [ ] Backend connectivity
- [ ] Build system or CI
- [ ] Documentation only

## Implementation Notes

Describe important design decisions, resource ownership, synchronization, compatibility considerations, or limitations.

## Resource Impact

Describe measured or expected changes.

- Internal RAM:
- PSRAM:
- Flash:
- Task stack:
- CPU load:
- Network or CAN throughput:

## Testing

Describe exactly how the change was verified. Include commands, hardware actions, and expected results.

### Test Environment

- Spectra commit:
- ESP-IDF version:
- ESP-IDF commit:
- Applied ESP-IDF patches or cherry-picked commits:
- Target board or hardware revision:
- Flash and PSRAM size:
- Relevant peripherals:
- Relevant configuration changes:

### Verification

- [ ] Project builds successfully
- [ ] No new compiler warnings were introduced
- [ ] Tested on ESP32-S3 hardware
- [ ] Relevant error and cleanup paths were tested
- [ ] A clean build was performed when appropriate

### Subsystem Tests

Check only the items relevant to this pull request.

- [ ] Display output tested
- [ ] Touch input tested
- [ ] GUI navigation tested
- [ ] Internal storage tested
- [ ] SD-card mount, access, and unmount tested
- [ ] USB RNDIS connection tested
- [ ] DHCP and DNS resolution tested
- [ ] Web UI and affected API endpoints tested
- [ ] Logging tested
- [ ] CAN functionality tested
- [ ] Not applicable — documentation or non-runtime change
- [ ] Light and Dark GUI themes tested
- [ ] Graceful restart tested
- [ ] SD-card removal while the web server is running tested
- [ ] Power-management and battery reporting tested
- [ ] Buzzer output and GUI feedback tested
- [ ] Wi-Fi SoftAP and Station modes tested
- [ ] Internet connectivity state tested
- [ ] Classical CAN receive and transmit tested
- [ ] CAN runtime reconfiguration tested
- [ ] CAN FD receive and transmit tested

## Screenshots or Logs

Add screenshots for GUI/Web UI changes and concise serial logs for firmware behavior. Remove sensitive information before attaching logs.

## Breaking Changes

Describe changes to public APIs, configuration, partition tables, stored data formats, USB descriptors, or expected hardware behavior.

Write `None` when this pull request has no breaking changes.

## Checklist

- [ ] The change is focused on one feature, fix, or refactoring task
- [ ] The code follows the existing project style
- [ ] Identifiers, comments, logs, and documentation are written in English
- [ ] Public APIs and return codes are documented
- [ ] Function arguments and external input are validated
- [ ] Return values are handled explicitly
- [ ] Memory, locks, files, tasks, and hardware handles are released on all relevant paths
- [ ] Shared mutable state is synchronized correctly
- [ ] No credentials, private keys, local paths, or build artifacts are included
- [ ] Documentation and `CHANGELOG.md` were updated when necessary
- [ ] Commits use clear Conventional Commit messages
- [ ] New tasks have measured stack headroom
- [ ] ISR callbacks perform only ISR-safe operations
- [ ] Changes to persistent settings preserve backward compatibility
- [ ] ESP-IDF patches or local framework modifications are documented
