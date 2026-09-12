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

`uds_security_access` adds an optional non-blocking seed-to-key state machine.
It owns a UDS client, requests the seed, copies it into an application-provided
buffer, and defers the OEM algorithm until `uds_security_access_poll()` runs.
After the algorithm returns a key, the adapter submits it and waits for the ECU
confirmation. Seed and key storage is supplied by the application, so the
adapter does not reserve fixed payload arrays or allocate heap memory. These
buffers must remain valid and must not overlap for the lifetime of the adapter.
The original UDS client event callback is preserved and receives both stages of
the exchange.

The programming transport includes RequestDownload (`0x34`), TransferData
(`0x36`), and RequestTransferExit (`0x37`). RequestDownload supports one- to
eight-byte memory addresses and sizes and parses the ECU maximum block length
from response `0x74`. TransferData preserves the caller-provided block sequence
counter and supports up to 512 data bytes per request. TransferExit accepts an
optional parameter record of up to 256 bytes. The web diagnostics page exposes
all three primitives for controlled manual testing, using hexadecimal strings
for 64-bit addresses and sizes to avoid JavaScript number precision loss.

`uds_download` builds a non-blocking automatic transfer state machine on these
primitives. It negotiates the ECU maximum block length, caps payload blocks to
the configured buffer and client limit, starts sequence numbering at one,
validates every echoed block counter, wraps the eight-bit counter naturally,
and sends TransferExit after the requested memory size is acknowledged. Input
comes from an application callback, so the same state machine can read a RAM or
PSRAM image, an SD-card file, or another streaming source. The reader and OEM
code execute only from `uds_download_poll()`, not from the ISO-TP callback. The
adapter allocates neither heap memory nor a task and provides progress and
cancellation APIs.

The UDS Web API connects this state machine to an SD-card file. Starting an
automatic download reuses the configured diagnostic transport, closes the
manual client to prevent duplicate ISO-TP channels, and creates a temporary
worker task. The worker continues programming when the browser is not polling;
HTTP requests only read progress or request cancellation. The 512-byte transfer
buffer is allocated in PSRAM and the SD file is closed on completion,
cancellation, or error.

Automatic Web downloads accept only regular `.bin`, `.hex`, `.srec`, or `.mot`
files directly inside `/firmwares`. Empty files and images larger than 64 MiB
are rejected before a diagnostic request is sent. TransferData retries the same
payload with the same block sequence counter up to three times after a response
timeout, `busyRepeatRequest` (`0x21`), or `wrongBlockSequenceCounter` (`0x73`).
The complete automatic transfer is limited to ten minutes. Progress reports the
acknowledged block count, total retry count, current block retry, and last NRC.

Each automatic programming attempt creates a text journal in
`/logs/firmware`. Its filename uses local synchronized time when available and
falls back to monotonic boot time. The journal records the firmware path and
size, target address, data format, retry events, transferred bytes, block and
retry counters, final NRC, result, duration, and the final outcome. The journal
is flushed when opened and synchronized to the SD card before it is closed.

Complete-message buffers are still configured through `isotp_service`, so an
application can place them in PSRAM. The UDS client itself does not allocate
payload memory.

## Current limitations

- one outstanding request per client;
- physical addressing only through the selected ISO-TP channel;
- no periodic Tester Present scheduler;
- no built-in OEM security-access algorithms;
- no typed DID value or routine-result decoders yet;
- no persistent resume after reset or interrupted ECU programming;
- no authentication or role-based protection for destructive UDS requests.
