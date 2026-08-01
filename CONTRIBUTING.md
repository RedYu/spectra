# Contributing to Spectra

Thank you for your interest in Spectra. Contributions that improve reliability, hardware support, documentation, testing, and maintainability are welcome.

## Before You Start

- Check existing issues and pull requests to avoid duplicate work.
- Open an issue before making a large architectural change.
- Keep each contribution focused on one feature, fix, or refactoring task.
- Do not include unrelated formatting or generated-file changes.

## Development Environment

Spectra currently targets:

- ESP32-S3
- ESP-IDF 6.x
- FreeRTOS
- LVGL 9
- CMake

Configure and build the project with:

```bash
idf.py set-target esp32s3
idf.py build
```

Before submitting a change, perform a clean build when practical:

```bash
idf.py fullclean
idf.py build
```

## Code Style

- Follow the existing project style and ESP-IDF conventions.
- Use C for firmware components unless another language is required.
- Use English for identifiers, comments, documentation, and log messages.
- Keep functions focused on a single responsibility.
- Prefer descriptive names over abbreviations.
- Use fixed-width integer types when the size matters.
- Add the `U` suffix to unsigned integer constants where appropriate.
- Use parentheses around macro values.
- Declare variables in the narrowest practical scope.
- Mark values `const` when they are not modified.
- Validate public-function arguments.
- Return `esp_err_t` when an operation can fail.
- Handle return values explicitly; do not silently ignore errors.
- Use `(void)` when intentionally discarding a return value.
- Keep headers self-contained and include everything required by their public declarations.
- Protect shared mutable state with the appropriate FreeRTOS synchronization primitive or C atomic type.
- Do not call blocking APIs from interrupt callbacks.
- Release allocated memory, locks, handles, and files on every error path.

Comments should explain intent, constraints, ownership, or non-obvious behavior. Avoid comments that merely repeat the code.

## Public APIs

Public headers should:

- use `#pragma once`;
- provide `extern "C"` guards when the API may be used from C++;
- document public functions with Doxygen comments;
- describe parameter direction with `@param[in]`, `@param[out]`, or `@param[in,out]`;
- document meaningful return codes;
- avoid exposing internal state or implementation-specific handles unless required.

Changes to an existing public API should update all callers and relevant documentation in the same pull request.

## Architecture

Keep dependencies aligned with the project layers:

```text
Application
    ↓
GUI and web interfaces
    ↓
Services
    ↓
Models
    ↓
Drivers
    ↓
Hardware
```

- **Drivers** contain hardware-specific operations.
- **Services** coordinate application logic and resource ownership.
- **Models** provide synchronized application state.
- **GUI and web components** present data and invoke service APIs.

Avoid accessing driver internals directly from GUI code or modifying model state without its public API.

## Commit Messages

Use the Conventional Commits format:

```text
<type>(optional-scope): <short description>
```

Common types:

- `feat`: add a user-visible feature
- `fix`: correct faulty behavior
- `refactor`: restructure code without changing behavior
- `docs`: update documentation
- `test`: add or update tests
- `build`: change the build system or dependencies
- `ci`: change continuous-integration configuration
- `chore`: perform maintenance that does not fit another type

Examples:

```text
feat(web): add storage file downloads
fix(lvgl): release SPI bus after DMA flush
refactor(storage): centralize path validation
docs: update USB network setup
build: add TinyUSB component override
```

Use the imperative mood, keep the subject concise, and do not end it with a period.

Avoid vague messages such as:

```text
Fix
Update
Changes
Test
```

## Pull Requests

Each pull request should include:

- a clear summary of the change;
- the reason for the change;
- relevant hardware and configuration details;
- verification steps and results;
- screenshots for visible GUI or web-interface changes;
- related issue numbers, when applicable.

Before opening a pull request:

- confirm that the project builds without new warnings;
- test affected functionality on hardware when possible;
- review error and cleanup paths;
- update documentation and configuration files when required;
- ensure that no credentials, private keys, local paths, build artifacts, or device-specific secrets are committed.

## Reporting Bugs

A useful bug report includes:

- the ESP-IDF version and Git commit;
- the target hardware and relevant peripherals;
- steps to reproduce the problem;
- expected and actual behavior;
- serial logs and error messages;
- relevant configuration changes;
- whether the issue is reproducible after a clean build.

## License

By contributing to Spectra, you agree that your contribution will be licensed under the project's MIT License.
