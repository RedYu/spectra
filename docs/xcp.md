# XCP foundation

Spectra implements the first protocol-independent layer of XCP on CAN.
XCP traffic uses direct CAN CTO and DTO frames; it does not use ISO-TP.

The `xcp` component currently provides:

- generic CTO command encoding;
- packet classification for RES, ERR, EV, SERV and DAQ DTO packets;
- CONNECT response decoding;
- XCP error-code names;
- encoders for CONNECT, DISCONNECT, GET_STATUS, GET_COMM_MODE_INFO,
  GET_ID, SET_MTA, UPLOAD, SHORT_UPLOAD, DOWNLOAD, DOWNLOAD_NEXT, and SYNCH;
- response decoders for GET_STATUS, GET_COMM_MODE_INFO, and GET_ID;
- the public contract for a four-session stateful XCP master service;
- CAN bus, CRO/DTO identifier, timeout, padding, and callback configuration;
- session state, slave capabilities, command counters, and queue statistics;
- blocking CONNECT, DISCONNECT, CTO execution, and cancellation operations.

The service is implemented by `xcp_service.c`. It owns a bounded command
queue, subscribes to normalized CAN router events, matches transmit
confirmations and DTO responses, enforces one outstanding CTO per session,
and completes blocking callers on RES, ERR, cancellation, or timeout. The
application starts it after the CAN router and stops it before CAN sources
and the router are released during a graceful restart.

## Web API

The device exposes `GET /api/xcp` and `POST /api/xcp`. The Web API owns one
of the four service sessions. POST accepts these actions:

- `configure` opens or replaces the Web session. Required fields are `bus`,
  `command_identifier`, and `response_identifier`. Optional fields are
  `extended`, `can_fd`, `brs`, `transmit_data_length`, `padding_byte`, and
  `timeout_ms`;
- `connect` sends CONNECT and accepts the optional `mode` byte;
- `get_status` reads session and resource-protection state;
- `get_comm_mode_info` reads optional communication capabilities;
- `get_id` reads an identification object and automatically follows a
  transfer-mode-zero response with as many UPLOAD commands as required;
- `set_mta` sets the memory transfer address;
- `upload` reads the requested number of address-granularity elements;
- `write_memory` sets the MTA and writes one bounded DOWNLOAD packet. It
  requires an explicit confirmation flag and an allowed inclusive address
  range; the complete payload must fit inside that range and the negotiated
  `MAX_CTO`;
- `execute` sends the hexadecimal CTO bytes from `command`;
- `disconnect`, `cancel`, and `close` control the current Web session.

GET returns the session state, connection state, latest ESP-IDF result and
XCP error, the negotiated maximum CTO/DTO sizes, decoded discovery fields,
identification text, and the most recent raw response. Identifiers and other
numeric configuration fields are JSON numbers; CTO bytes use a space-separated
hexadecimal string such as `F5 00 04 00`.

Multi-packet DOWNLOAD using DOWNLOAD_NEXT, general block transfer,
DAQ/STIM configuration, seed/key access,
calibration-page control and programming commands are intentionally outside
this first layer. They require a stateful XCP master service and explicit
safety policy before they are exposed through the Web API.
