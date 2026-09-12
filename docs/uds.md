# Unified Diagnostic Services

Spectra implements UDS as an application protocol above ISO-TP. The initial
implementation is a diagnostic client and does not yet act as an ECU server.

## Layers

`uds_protocol` provides allocation-free request encoding and response parsing.
It validates the positive-response SID (`request SID + 0x40`) and the standard
three-byte negative response (`0x7F`, request SID, NRC).

`uds_client` owns the state of one outstanding diagnostic request. It uses an
existing `isotp_service` channel and provides:

- raw SID and parameter transmission;
- P2 timing after ISO-TP transmission completes;
- P2* timing after NRC `0x78 Response Pending`;
- positive, negative, timeout, protocol-error, and transport-error events;
- human-readable names for commonly used negative response codes.

The ReadDTCInformation implementation supports reporting the DTC count by
status mask (`0x19 0x01`), reporting DTC records by status mask (`0x19 0x02`),
and reporting supported DTCs (`0x19 0x0A`). Positive responses expose the
status-availability mask, DTC format identifier, 24-bit DTC values, and status
bytes through allocation-free views of the ISO-TP response buffer. The status
byte can also be decoded into the eight ISO 14229 DTC status flags.

ClearDiagnosticInformation (`0x14`) accepts a 24-bit group-of-DTC value. The
web diagnostics page asks for confirmation before submitting this destructive
request.

WriteDataByIdentifier (`0x2E`) accepts one 16-bit DID and between 1 and 256
data bytes. The client validates the payload before transmission and the web
diagnostics page asks for confirmation before writing to the ECU.

RoutineControl (`0x31`) supports StartRoutine (`0x01`), StopRoutine (`0x02`),
and RequestRoutineResults (`0x03`). Requests contain a 16-bit routine identifier
and an optional routine-control option record of up to 256 bytes. The web page
asks for confirmation before starting or stopping a routine and supports the
suppress-positive-response bit. Positive response `0x71` is decoded into its
operation, routine identifier, and optional routine status record.

SecurityAccess (`0x27`) supports requesting a seed for odd security levels from
`0x01` through `0x7D` and sending the corresponding key with the following even
subfunction. Both seed-request records and keys are limited to 256 bytes. The
protocol deliberately does not contain an OEM seed-to-key algorithm: application
code receives the positive `0x67` response through the existing UDS client event
callback, calculates the key, and submits it with
`uds_client_security_access_send_key()`. The web page provides equivalent manual
Request Seed and Send Key operations and decodes the `0x67` response.

Complete-message buffers are still configured through `isotp_service`, so an
application can place them in PSRAM. The UDS client itself does not allocate
payload memory.

## Current limitations

- one outstanding request per client;
- physical addressing only through the selected ISO-TP channel;
- no periodic Tester Present scheduler;
- no built-in OEM security-access algorithms;
- no typed DID value, routine-result, download, or transfer decoders yet;
- no authentication or role-based protection for destructive UDS requests.

The next layer should add an optional asynchronous seed-to-key adapter before
adding download and transfer services.
