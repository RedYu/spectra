<!--
SPDX-License-Identifier: GPL-3.0-only
Copyright (C) 2026 Yurii Ridkovets
-->

# Spectra firmware architecture

This document describes the high-level firmware architecture of Spectra, the
responsibilities of its components, and the main runtime data flows.

## Design goals

The firmware is organized to provide:

- independent Primary and Secondary CAN interfaces;
- one hardware-independent frame and event representation;
- non-blocking delivery to monitoring and external consumers;
- explicit queue, task, and hardware-resource ownership;
- runtime configuration without exposing driver details to the UI;
- graceful startup, reconfiguration, restart, and shutdown;
- measurable overload behavior instead of silent data corruption.

Spectra is a diagnostic and development instrument. It is not a
safety-certified automotive control system.

## System overview

```text
Physical buses
  Primary CAN                         Secondary CAN / CAN FD
       |                                       |
       v                                       v
ESP32-S3 TWAI driver                 MCP2518FD SPI driver
       |                                       |
       v                                       v
Primary CAN service                  Secondary CAN FD service
       |                                       |
       +---------------+-----------------------+
                       |
                       v
                   CAN router
                  /     |      \
                 v      v       v
          CAN monitor  Web CAN  Future consumers
                                Logger / UDS / XCP
                 |        |
                 v        v
              LVGL UI   WebSocket client
```

Both CAN implementations remain hardware-specific below their service
boundaries. Above those boundaries, consumers use `can_frame_t` and
`can_event_t` from the shared CAN component.

## Repository components

```text
spectra/
├── main/                       Application composition and startup
├── components/
│   ├── app/                    Global application configuration
│   ├── app_task_config/        Shared FreeRTOS task priorities
│   ├── board/                  Board buses, pins, and shared resources
│   ├── can/                    Common CAN model and central router
│   ├── can_monitor/            CAN activity aggregation
│   ├── drivers/                Hardware-specific implementations
│   ├── gui/                    LVGL screens, widgets, styles, and assets
│   ├── lvgl_port/              Display/input integration with LVGL
│   ├── models/                 Synchronized application state
│   └── services/               Long-lived application and Web services
├── spiffs_data/                Embedded Web UI and default data
├── docs/                       User and developer documentation
├── partitions.csv              Flash layout
└── sdkconfig.defaults          Project configuration defaults
```

## Architectural layers

### Board layer

The board component defines physical resources and initializes resources shared
by more than one driver. Examples include:

- GPIO assignments;
- SPI and I2C buses;
- the shared GPIO ISR service;
- display, touch, CAN, storage, and power connections.

A peripheral driver must not independently install or destroy a board-wide
resource. The board owns such a resource for the application's lifetime.

### Driver layer

Drivers communicate directly with hardware and expose hardware-oriented APIs.
They are responsible for:

- peripheral initialization and deinitialization;
- register and bus access;
- interrupt handling;
- hardware FIFO management;
- conversion between hardware state and driver-native structures;
- low-level counters and error reporting.

Important CAN drivers are:

- `can_twai_driver` for ESP32-S3 Classical CAN;
- `can_fd_mcp2518fd_driver` for MCP2518FD Classical CAN and CAN FD.

Drivers do not implement GUI, WebSocket, logging, or diagnostic policy.

### Service layer

Services own long-lived runtime behavior. A service may own a task, queue,
semaphore, event group, timer, or driver instance. Services are responsible for:

- lifecycle control;
- concurrency and synchronization;
- runtime configuration;
- queueing and backpressure;
- translating driver callbacks into application events;
- exposing thread-safe APIs to the rest of the application.

Examples include CAN, settings, storage, network, battery, system, GUI,
shutdown, and Web services.

### Model layer

Models hold application state that can be read by multiple presentation or
service components. Access to mutable state must be synchronized. Models must
not directly operate hardware or LVGL objects.

### Presentation layer

The presentation layer contains:

- LVGL screens on the device;
- the embedded browser application;
- REST APIs;
- the CAN WebSocket stream.

Presentation code requests operations through services and reads snapshots from
models or monitoring APIs. It must not call hardware drivers directly.

## CAN data model

`can_frame_t` is the common representation for both buses. It describes:

- bus identity;
- standard or extended identifier;
- Classical CAN or CAN FD format;
- RTR, BRS, and ESI flags;
- DLC and actual payload length;
- up to 64 payload bytes;
- timestamp and timestamp source.

The complete type contract and validation rules are documented in
[can-frame-model.md](can-frame-model.md).

`can_event_t` wraps a frame with lifecycle information:

- RX or TX direction;
- received, queued, completed, failed, or aborted event type;
- global event sequence;
- application transaction ID;
- driver-native TX sequence;
- operation result.

Hardware adapters translate between the common representation and each
driver-native frame structure:

```text
can_twai_frame_t             can_fd_mcp2518fd_frame_t
        |                              |
        v                              v
can_twai_frame_adapter       can_mcp2518fd_frame_adapter
        |                              |
        +----------> can_frame_t <-----+
```

This keeps the router and its consumers independent from the selected CAN
controller.

## CAN receive path

```text
CAN wire
   |
   v
Controller RX FIFO
   |
   v
Driver interrupt/task processing
   |
   v
CAN service RX queue and adapter
   |
   v
can_event_t (RX)
   |
   v
CAN router subscriber delivery
   |
   +--> CAN monitor aggregation --> device CAN Monitor screen
   |
   +--> Web CAN queue --> binary batches --> WebSocket client
   |
   +--> future logger / protocol consumers
```

The MCP2518FD receive path uses interrupt-driven processing and batch reads to
reduce SPI transactions and CPU overhead. Processing is bounded so sustained RX
traffic cannot permanently starve TX completion and error handling.

Every queue is a deliberate overload boundary. A full downstream queue must
increment a drop counter; it must not block the hardware receive path
indefinitely.

## CAN transmit path

```text
Application / future UDS or XCP client
   |
   v
can_router_transmit()
   |
   +--> validate frame and target bus
   +--> allocate application transaction ID/context
   +--> publish TX_QUEUED event
   |
   v
Selected CAN service and frame adapter
   |
   v
TWAI or MCP2518FD driver
   |
   v
Controller transmission result
   |
   v
Service confirmation processing
   |
   v
can_router_report_tx_result()
   |
   +--> TX_COMPLETED
   +--> TX_FAILED
   +--> TX_ABORTED
```

The application transaction ID identifies the logical request across layers.
The native sequence identifies the controller/driver transmission when the
hardware supports it. These values are related but are not interchangeable.

## CAN router

The CAN router is the boundary between CAN producers and consumers. It:

- accepts normalized RX and TX lifecycle events;
- assigns global event sequence values;
- routes transmit requests to the selected interface;
- correlates TX results with application transactions;
- distributes events to registered subscribers;
- tracks queue and delivery statistics.

Subscribers must process or enqueue events quickly. Slow work such as JSON
generation, file I/O, browser transmission, or protocol timeouts belongs in a
consumer-owned task, not in the router's dispatch path.

The initial consumers are the CAN monitor and Web CAN stream. Planned consumers
include the logger, UDS, XCP, replay, and signal-decoding services.

## CAN monitor

The CAN monitor subscribes to router events and maintains aggregated state:

- per-bus RX and TX rates;
- frame and byte totals;
- drops and queue occupancy;
- per-identifier counters;
- latest direction, payload, timestamp, and activity time.

The monitor exposes snapshots. Its identifier capacity limits the amount of
aggregated state retained and displayed; it does not limit the number of CAN
frames processed by the drivers or router.

The LVGL CAN Monitor screen reads snapshots periodically. It does not receive
every frame directly and therefore cannot block CAN processing.

## Web CAN stream

The Web CAN stream service is another router subscriber. It owns a bounded
event queue and a worker that groups events into binary WebSocket batches.

```text
CAN router
   |
   v
bounded Web CAN queue
   |
   v
filter and batch worker
   |
   +--> binary event batches
   +--> JSON stream statistics
   v
HTTP server WebSocket connection
```

Client subscription state selects Primary/Secondary and RX/TX events and can
pause delivery without stopping either CAN interface. Queue drops, filtering,
batch sizes, and send failures are observable through stream statistics.

The wire format is defined in
[websocket-can-protocol.md](websocket-can-protocol.md).

The HTTP resources exposed by the firmware are described in
[web-api.md](web-api.md).

## Settings and runtime configuration

The settings model contains persistent user configuration. The settings service
coordinates validation, persistence, and application of settings to dependent
services.

CAN settings include:

- interface enable state;
- nominal bitrate;
- Secondary data-phase bitrate;
- listen-only mode;
- CAN FD enable state;
- BRS enable state.

The settings service must use service or router APIs rather than modifying
driver state directly. Runtime reconfiguration must stop access to an interface
before releasing and rebuilding its driver resources.

Wi-Fi credentials are stored separately in NVS. The JSON configuration stores
only the selected SSID and a non-secret credential identifier.

## Networking and storage

The network stack provides Wi-Fi AP/STA, USB RNDIS, DHCP/DNS, mDNS, HTTP, REST,
and WebSocket access. Board-wide ESP-NETIF and event-loop resources are
initialized before network interfaces and their dependent services.

Storage is split into:

- internal SPIFFS for configuration and embedded Web content;
- removable SD storage for logs, recordings, exported core dumps, and user
  files.

File users must coordinate with the SD service. A card must not be unmounted
while another task still owns an open file or is performing I/O.

## GUI architecture

Only the GUI task may access LVGL objects. The screen manager owns screen
registration, navigation history, lifecycle callbacks, and cached screen
objects.

```text
Services and models
       |
       | snapshots / commands
       v
GUI service and LVGL task
       |
       v
Screen manager
       |
       +--> Main screen
       +--> Settings screen
       +--> CAN Monitor screen
       +--> reusable widgets and dialogs
```

Screen timers and callbacks are started in `on_show`, stopped in `on_hide`, and
released in `destroy`. Background services continue operating when a screen is
not visible.

## Task priorities and memory

Task priorities are centralized in `app_task_config`. The relative ordering is
intentional:

1. shutdown coordination;
2. MCP2518FD interrupt processing;
3. CAN interface processing;
4. CAN router and monitor;
5. logging, Web CAN, GUI, and storage;
6. network and periodic support services.

Higher priority is reserved for short, latency-sensitive work. It does not
authorize long loops or blocking file/network operations. Tasks must block on
notifications, queues, semaphores, or timers when idle.

Selected low-risk task stacks and large buffers can reside in PSRAM. Internal
RAM remains necessary for DMA buffers, interrupt-safe data, driver objects, and
other allocations requiring internal-memory capabilities. Moving memory to
PSRAM therefore requires checking both capability requirements and task stack
high-water marks.

## Startup dependencies

The exact orchestration is implemented by `app_main`, but startup follows these
dependency rules:

```text
logging and persistent storage
            |
            v
board and shared hardware resources
            |
            v
models, network foundation, and core services
            |
            v
CAN router and CAN consumers
            |
            v
settings load and runtime application
            |
            v
GUI, Web endpoints, and normal operation
```

In particular, the CAN router must be available before settings attempt to
enable a CAN interface through it. A service must not publish events to a
consumer that has not completed initialization.

Startup functions must unwind resources already acquired when a later step
fails.

## Shutdown and restart

Shutdown is coordinated asynchronously so callers such as HTTP handlers can
finish their responses. The shutdown sequence must:

1. reject duplicate shutdown requests;
2. save pending settings;
3. stop new externally initiated work;
4. stop producers before their consumers are destroyed;
5. wait for task-stop acknowledgement;
6. flush and close files;
7. unmount removable storage;
8. stop network and presentation services where required;
9. release peripheral resources;
10. restart the MCU or remove power.

Tasks use cooperative termination and an explicit event group or semaphore to
acknowledge that they no longer access shared resources. Deleting a task while
it still owns a file, lock, timer, or hardware handle is not a valid normal
shutdown path.

## Error handling and observability

Subsystem boundaries return `esp_err_t`; callers must handle return values
explicitly. Runtime failures are exposed through concise logs and statistics.

Important health signals include:

- CAN controller error counters and bus state;
- RX FIFO overflow;
- service queue current, peak, capacity, and drops;
- successful, failed, and aborted transmissions;
- WebSocket filtering, batching, drops, and send failures;
- task stack high-water marks;
- internal/DMA heap free, minimum, and largest block;
- SD synchronization and I/O failures;
- Wi-Fi disconnection reason and connectivity state.

High-frequency errors must be rate-limited or accumulated into counters. Logging
every overflow or frame can itself cause CPU load, timing changes, and further
data loss.

## Extension rules

New CAN consumers should follow these rules:

1. Depend on the common CAN model and router, not hardware-specific drivers.
2. Own a bounded queue when processing can block or take significant time.
3. Define overload behavior and expose drop counters.
4. Correlate transmissions using router transaction IDs.
5. Keep protocol state outside router and driver callbacks.
6. Avoid file, network, JSON, and LVGL work in high-priority CAN paths.
7. Register before required producers start and unregister before teardown.

Expected additions fit into the architecture as follows:

| Component | Role |
|---|---|
| CAN logger | Router subscriber with PSRAM buffering and SD writer |
| UDS service | Request/response protocol using router TX and filtered RX |
| XCP service | Session and measurement protocol over normalized CAN events |
| Replay service | Timed producer of router TX requests |
| DBC decoder | Consumer of frame snapshots/events that produces signals |

## Dependency direction

The intended long-term dependency direction is:

```text
presentation -> services -> domain components -> drivers -> board/platform
```

Callbacks and narrow interfaces may invert runtime control, but lower layers
must not acquire presentation responsibilities. When two components require
each other's concrete headers, introduce a shared interface or move the common
contract into a lower-level component instead of creating a circular
dependency.
