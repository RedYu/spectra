# ISO-TP

Spectra implements ISO-TP in layers. The protocol codec in `components/isotp`
validates and encodes Protocol Control Information without owning tasks,
queues, CAN interfaces, or payload buffers.

The codec and session layers support normal, extended, and mixed addressing
for Classical CAN and CAN FD:

- Single Frame, including the CAN FD escape length;
- First Frame with 12-bit and 32-bit message lengths;
- Consecutive Frame and its four-bit sequence number;
- Flow Control statuses, Block Size, and STmin;
- STmin conversion for milliseconds and 100–900 microseconds.
- configurable transmit padding;
- Flow Control overflow responses when a message exceeds the RX buffer.
- functional transmissions restricted to a Single Frame, as required by
  ISO-TP.

Malformed received headers return `ESP_ERR_INVALID_RESPONSE`. Invalid local
arguments return `ESP_ERR_INVALID_ARG`; a message that does not fit the selected
frame type returns `ESP_ERR_INVALID_SIZE`.

## Session state machine

`isotp_session` adds an allocation-free, transport-independent state machine.
It supports:

- Single Frame reception and transmission;
- multi-frame reassembly and segmentation;
- Consecutive Frame sequence-number validation;
- Flow Control CTS, WAIT, overflow, Block Size, and STmin;
- Flow Control and Consecutive Frame deadlines;
- explicit confirmation after each generated CAN frame is transmitted.

The application owns the complete-message receive and transmit buffers. The
state machine returns actions instead of calling `can_router` or FreeRTOS, so
its protocol behavior can be tested independently. A later transport-service
layer will associate sessions with CAN bus and addressing information, allocate
bounded message storage, subscribe to `can_router`, and translate router TX
confirmations into `isotp_session_frame_transmitted()` calls.

UDS will use those ISO-TP sessions. XCP on CAN will use `can_router` directly
rather than ISO-TP.

## CAN router integration

`isotp_service` binds the state machines to `can_router`. It provides four
independent normal-addressing channels. Each channel selects a CAN bus, RX and
TX identifier, Classical CAN or CAN FD framing, and an event callback.

The service copies complete outgoing messages into the caller-provided TX
buffer. Reassembled messages are placed in the caller-provided RX buffer. Those
buffers may be allocated in internal RAM or PSRAM according to the latency and
capacity required by the application. No complete-message buffer is allocated
implicitly by the service.

Router callbacks only copy router events into a bounded service queue.
Protocol processing and calls to `can_router_transmit()` run in the dedicated
ISO-TP task. Successful router TX confirmations advance the state machine;
failed and aborted confirmations terminate the current transfer.

The transport implementation supports normal, extended, and mixed addressing
and one half-duplex operation per channel. Extended and mixed addressing use a
configured address byte before the PCI. CAN identifier width remains an
independent channel setting.
