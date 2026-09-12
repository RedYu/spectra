# XCP foundation

Spectra implements the first protocol-independent layer of XCP on CAN.
XCP traffic uses direct CAN CTO and DTO frames; it does not use ISO-TP.

The `xcp` component currently provides:

- generic CTO command encoding;
- packet classification for RES, ERR, EV, SERV and DAQ DTO packets;
- CONNECT response decoding;
- XCP error-code names;
- encoders for CONNECT, DISCONNECT, GET_STATUS, SYNCH and SHORT_UPLOAD.

Multi-frame block transfers, DAQ/STIM configuration, seed/key access,
calibration-page control and programming commands are intentionally outside
this first layer. They require a stateful XCP master service and explicit
safety policy before they are exposed through the Web API.
