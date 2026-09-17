# XCP master

Spectra implements the first protocol-independent layer of XCP on CAN.
XCP traffic uses direct CAN CTO and DTO frames; it does not use ISO-TP.

The `xcp` component provides:

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
- slave block-mode DOWNLOAD and PROGRAM transfers using DOWNLOAD_NEXT and
  PROGRAM_NEXT, with the final response collected by the session service;
- GET_SEED and bounded UNLOCK requests for CAL/PAG, DAQ, STIM, and PGM
  resources. Seed-to-key calculation remains an external/OEM concern;
- GET_CAL_PAGE and SET_CAL_PAGE;
- dynamic DAQ allocation, ODT/entry allocation, DAQ pointer and entry writes,
  list-mode selection, list start/stop, and synchronized start/stop;
- unsolicited DAQ DTO delivery through the existing session callback and raw
  CAN trace;
- STIM DTO transmission on a separately configurable CAN identifier;
- PROGRAM_START, PROGRAM_CLEAR, PROGRAM, PROGRAM_NEXT, PROGRAM_RESET,
  PROGRAM_PREPARE, PROGRAM_FORMAT, and PROGRAM_VERIFY encoders and Web API
  actions.

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
- `download_block` performs a confirmed, address-range-checked slave block
  transfer. It requires CONNECT to advertise slave block mode and rejects a
  transfer larger than the negotiated `MAX_BS` or 255 address-granularity
  elements;
- `get_seed` and `unlock` expose explicit seed/key resource access without
  embedding an OEM key algorithm in the firmware;
- `get_cal_page` and `set_cal_page` inspect or activate a calibration page;
- `daq_free`, `daq_allocate`, `daq_allocate_odt`, `daq_allocate_entry`,
  `daq_set_pointer`, `daq_write`, `daq_set_mode`, `daq_start_stop`, and
  `daq_synchronize` configure and control dynamic DAQ lists;
- `stim` queues one DTO on the configured STIM identifier;
- `program_start`, `program_clear`, `program`, `program_block`,
  `program_prepare`, `program_format`, `program_verify`, and `program_reset`
  expose the standard programming sequence. Destructive actions require an
  explicit confirmation from the Web client;
- `execute` sends the hexadecimal CTO bytes from `command`;
- `disconnect`, `cancel`, and `close` control the current Web session.

GET returns the session state, connection state, latest ESP-IDF result and
XCP error, the negotiated maximum CTO/DTO sizes, decoded discovery fields,
identification text, and the most recent raw response. Identifiers and other
numeric configuration fields are JSON numbers; CTO bytes use a space-separated
hexadecimal string such as `F5 00 04 00`.

Block operations are intentionally bounded by one XCP command's 8-bit element
count. Larger files must be divided into independently checked address ranges.
The master does not contain OEM seed/key algorithms and does not guess ECU
erase, page, event-channel, checksum, or programming-format parameters. Those
values must come from an ECU profile or be entered explicitly. DAQ decoding is
currently raw: the configured ODT layout must be interpreted by the browser or
an A2L-aware layer.
