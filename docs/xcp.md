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
- `program_job_start` starts an autonomous SD-backed programming task after
  closing the interactive Web XCP session. The request contains the transport,
  programming policy, `/firmwares` path, and an explicit confirmation;
- `program_job_resume`, `program_job_cancel`, and
  `program_job_discard_resume` control the device-side job and journal;
- `execute` sends the hexadecimal CTO bytes from `command`;
- `disconnect`, `cancel`, and `close` control the current Web session.

GET returns the session state, connection state, latest ESP-IDF result and
XCP error, the negotiated maximum CTO/DTO sizes, decoded discovery fields,
identification text, the most recent raw response, and autonomous programming
state, progress, block counters, current address, result, and resume
availability. Identifiers and other
numeric configuration fields are JSON numbers; CTO bytes use a space-separated
hexadecimal string such as `F5 00 04 00`.

Block operations are intentionally bounded by one XCP command's 8-bit element
count. Larger files must be divided into independently checked address ranges.
The master does not contain OEM seed/key algorithms and does not guess ECU
erase, page, event-channel, checksum, or programming-format parameters. Those
values must come from an ECU profile or be entered explicitly.

## A2L-aware DAQ decoding

The XCP page can load an A2L file locally. The file is never uploaded to the
device. The browser extracts addressable `MEASUREMENT` objects, their data
types, byte order, `ECU_ADDRESS`, linear `COMPU_METHOD` conversion and physical
unit. A measurement can then be assigned to a DTO PID and a byte offset after
the PID. Incoming DAQ DTOs are decoded into raw and physical values.

The supported measurement types are `UBYTE`, `SBYTE`, `UWORD`, `SWORD`,
`ULONG`, `SLONG`, `A_UINT64`, `A_INT64`, `FLOAT32_IEEE` and
`FLOAT64_IEEE`. Non-linear conversion tables and formulas remain visible only
as raw values until their conversion method is implemented.

## ECU profiles and programming resume

The Web client can save named XCP ECU profiles. A profile contains the CAN
transport configuration, address extension, DAQ event channel and the compact
DAQ decoding map derived from A2L. Profiles are stored in browser local
storage; the original A2L file is not copied.

After successful `PROGRAM_START`, `PROGRAM_CLEAR`, `PROGRAM_BLOCK` and
`PROGRAM_VERIFY` operations, the browser stores a programming checkpoint with
the profile name, stage, next MTA address and confirmed byte count. The
checkpoint survives page reloads and browser restarts. Restoring it fills the
next MTA and profile, but deliberately does not reconnect, unlock, erase or
transmit anything. The operator must verify the ECU state before continuing.
`PROGRAM_RESET` clears the checkpoint.

The browser checkpoint protects short interactive programming sequences. For
complete firmware images, Spectra also provides an autonomous device-side XCP
programming job. It streams BIN, Intel HEX, Motorola S-record, or BHX input
from `/sdcard/firmwares`, validates the complete image before opening the XCP
session, and never loads the complete file into RAM.

The autonomous job owns a dedicated XCP session and performs PROGRAM_START,
optional PROGRAM_FORMAT, optional range erase, per-block SET_MTA and PROGRAM,
optional PROGRAM_VERIFY, and optional PROGRAM_RESET. The current implementation
requires byte address granularity. OEM seed-to-key calculation remains external;
an ECU with protected PGM resources must be unlocked by a future configured
security provider.

After every ECU-confirmed image block, the service synchronizes this journal:

```text
/sdcard/logs/firmware/xcp-resume.json
```

The journal contains the transport and programming profile, source path, file
size and modification time, parsed image size, confirmed byte/block counts,
and next address. Resume reopens and validates the original image, starts a new
XCP programming session, skips already confirmed whole image blocks, and
continues at the recorded address. It never repeats erase. A changed file,
layout mismatch, or address mismatch rejects resume. Successful completion or
an explicit discard removes the journal; cancellation and failures retain it.

The Web client starts, monitors, cancels, resumes, or discards a device job
through the existing `/api/xcp` endpoint. Programming continues independently
of the HTTP request and browser connection.
