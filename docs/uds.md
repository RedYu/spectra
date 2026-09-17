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

Additional typed diagnostic requests include ReadMemoryByAddress (`0x23`)
with one- to eight-byte address and size fields, CommunicationControl (`0x28`),
InputOutputControlByIdentifier (`0x2F`) with an optional control-state record,
and ControlDTCSetting (`0x85`) with an optional option record. They are exposed
by the manual Web diagnostics page and remain subject to the active ECU session
and SecurityAccess permissions.

ReadScalingDataByIdentifier (`0x24`) uses the same 16-bit DID selection as
ReadDataByIdentifier. RequestUpload (`0x35`) uses the standard data-format and
address-and-length format fields. WriteMemoryByAddress (`0x3D`) validates that
the declared memory size exactly matches the supplied data and limits one
manual request to 512 bytes. All three have typed allocation-free request
builders, UDS client entry points, and manual Web controls.

The service-ID model also names Authentication (`0x29`),
ReadDataByPeriodicIdentifier (`0x2A`), DynamicallyDefineDataIdentifier (`0x2C`),
RequestFileTransfer (`0x38`), AccessTimingParameter (`0x83`),
SecuredDataTransmission (`0x84`), ResponseOnEvent (`0x86`), and LinkControl
(`0x87`). Their OEM- and subfunction-specific parameter records can be tested
through the bounded raw UDS request control while dedicated semantic editors
are added incrementally.

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

The programming pipeline can enter a configured diagnostic session, perform
SecurityAccess with the selected provider, run OEM erase and verification
routines, transfer the image, reset the ECU, and restore the default session.
Protected erase, download, and verify operations can trigger SecurityAccess
after NRC `0x33` when credentials are configured. On a terminal failure the
pipeline preserves the original result while making a best-effort transition
back to the default session.

Erase and verification routines support long-running ECU operations. A
positive StartRoutine response starts delayed RequestRoutineResults (`0x31
0x03`) polling instead of being treated as final completion. The polling
interval and maximum number of result requests are configurable. NRC `0x78`
uses the UDS client P2* timeout, while NRC `0x21` delays and repeats the result
request without restarting the routine. An optional application callback can
interpret the OEM routine status record as pending, complete, or failed. The
programming web page provides a simple status-byte policy with configurable
offset, pending value, and success value for erase and verification routines.

The UDS Web API connects this state machine to an SD-card file. Starting an
automatic download reuses the configured diagnostic transport, closes the
manual client to prevent duplicate ISO-TP channels, and creates a temporary
worker task. The worker continues programming when the browser is not polling;
HTTP requests only read progress or request cancellation. The 512-byte transfer
buffer is allocated in PSRAM and the SD file is closed on completion,
cancellation, or error.

Automatic Web downloads accept BIN, Intel HEX, Motorola S-record, and BHX images
directly inside `/firmwares`. BIN uses the configured base address. Text-image
formats are completely validated before the diagnostic session changes and use
their embedded addresses. Adjacent data records are combined; every address
gap creates another RequestDownload, TransferData, and RequestTransferExit
sequence without repeating session entry, SecurityAccess, erase, or final
verification. Empty files and images larger than 64 MiB are rejected before a
diagnostic request is sent. TransferData retries the same
payload with the same block sequence counter up to three times after a response
timeout, `busyRepeatRequest` (`0x21`), or `wrongBlockSequenceCounter` (`0x73`).
For other programming stages, NRC `0x21` schedules a bounded delayed retry,
SecurityAccess NRC `0x37` waits longer before retrying the same seed/key stage,
and NRC `0x33` during erase, RequestDownload, or verification restarts the
configured SecurityAccess exchange before resuming that stage. Structural,
range, programming-failure, and unsupported-service NRCs remain terminal.
The complete automatic transfer is limited to ten minutes. Progress reports the
acknowledged block count, total retry count, current block/action retry,
selected NRC action, and last NRC.

Each automatic programming attempt creates a text journal in
`/logs/firmware`. Its filename uses local synchronized time when available and
falls back to monotonic boot time. The journal records the firmware path and
size, target address, data format, retry events, transferred bytes, block and
retry counters, final NRC, result, duration, and the final outcome. The journal
is flushed when opened and synchronized to the SD card before it is closed.

Automatic programming also maintains an atomic resume checkpoint at
`/logs/firmware/uds-resume.json`. Only a segment that has received a positive
RequestTransferExit response is recorded as complete. After a reset or power
loss, the Programming page offers Resume and Discard actions. Resume validates
the original firmware path, file size, and parsed segment count, then uses the
currently applied ECU configuration to re-enter the programming session and
SecurityAccess. It
skips the erase routine and starts at the first incomplete segment. The current
segment is retransmitted from its beginning when interruption occurred before
RequestTransferExit; partial in-segment resume is intentionally not assumed.
The checkpoint is removed after successful completion or explicit
cancellation, and retained after an error or unexpected reset. Security keys
are never written to the checkpoint.

The dedicated `/uds_programming` page separates automatic ECU programming
from manual ISO-TP and UDS diagnostics. It lists supported images directly
from `/firmwares`, configures the diagnostic CAN channel, locks mutable fields
while an operation owns the channel, and displays the current stage, percent,
throughput estimate, ETA, elapsed time, negotiated block size, sequence
counter, acknowledged blocks, retries, last NRC, and journal path. Manual
RequestDownload, TransferData, and RequestTransferExit controls remain on the
ISO-TP diagnostics page for protocol testing.

`uds_ecu_profile` defines a validated device-independent ECU profile model.
It groups CAN and ISO-TP addressing, CAN FD options, P2/P2* and TesterPresent
timing, programming-session parameters, SecurityAccess level, memory-format
defaults, erase and verification routines, routine-result policy, ECU reset,
and default-session restoration. Profiles initialize with conservative
physical-addressing defaults and reject incompatible settings such as CAN FD
on the Primary TWAI interface, extended identifiers outside 29 bits, invalid
STmin values, even SecurityAccess seed levels, and ambiguous routine status
values. Security keys and seed-to-key implementations are deliberately not
part of a profile; a stored profile may select a provider in a later storage
layer, but secret material must remain outside the profile file.

`uds_profile_service` stores each validated profile as a separate versioned
JSON file in `/config/uds/profiles`. File names are restricted to safe local
characters and the `.json` extension. Writes use a temporary file and publish
the final name only after the JSON has been flushed and synchronized. Invalid,
oversized, or unsupported profile files are rejected when loaded and omitted
from profile listings. Formatting an SD card recreates the profile directory.
The existing `/api/uds` handler exposes profile listing and loading through
GET query parameters, plus validated save and remove actions through POST. It
does not consume additional HTTP URI-handler slots. The UDS Programming page
can load a profile into its editable form, save the current form as a profile,
apply its transport configuration to the UDS channel in one operation, or
delete a selected profile. The browser remembers only the last selected profile
file name and restores that selection when the page is opened again. A one-time
SecurityAccess key is never loaded from or written to a profile or browser
storage.

`uds_did_catalog` provides a separate, caller-owned catalog of DID
definitions. Each entry contains the 16-bit DID, display metadata, fixed data
length, unsigned, signed, IEEE floating-point, ASCII, UTF-8, or raw-byte type,
byte order, scale, offset, and unit. Catalog validation rejects malformed
definitions and duplicate identifiers. Lookup and decoding do not allocate
memory: numeric decoding preserves the raw integer and calculates the physical
value, text is copied into a caller-provided buffer, and raw-byte values refer
to the received response buffer. Invalid text encodings, non-finite floating
point values, and responses whose length differs from the definition are
reported explicitly.

`uds_did_catalog_service` stores each DID catalog as a separate versioned
JSON file in `/config/uds/dids`. A catalog contains a display name, an
optional description, and up to 128 validated DID definitions. File names are
restricted to safe local characters and the `.json` extension. Loading and
JSON decoding write definitions into storage supplied by the caller, so a
catalog that must remain active can be placed in PSRAM. Temporary file-read
and listing buffers are also allocated in PSRAM. Files larger than 64 KiB,
unsupported schema versions, malformed definitions, and duplicate DIDs are
rejected. Catalog listing is paginated and omits invalid files. Formatting an
SD card recreates the catalog directory.

The existing `/api/uds` GET handler also exposes DID catalogs without using an
additional HTTP URI slot. `did_catalogs=1` returns a paginated summary list and
`did_catalog=<file>` returns one validated catalog directly. A POST to the same
`did_catalog=<file>` query stores the raw catalog document, while the
`did_catalog_remove` action removes it. All input is validated before changing
the SD card. Catalog request bodies are bounded to the catalog file limit and
their HTTP receive buffer is allocated in PSRAM. Direct transfer avoids
building an API wrapper and avoids serializing and parsing the complete catalog
a second time. The API logs Internal heap availability before and after catalog
list, load, and save operations for on-device verification.
The ISO-TP diagnostics page provides a catalog editor for adding, changing,
loading, saving, and removing up to 128 DID definitions. Its Use action copies
the selected DID directly into the ReadDataByIdentifier request form. Positive
`0x62` responses are decoded locally in the browser using the catalog currently
shown in the editor. The decoder checks the returned DID and exact data length,
supports signed and unsigned integers, IEEE 32-bit and 64-bit floating point,
ASCII, UTF-8, and raw bytes, applies byte order, scale, offset, and unit, and
keeps the original bytes visible. Unknown DIDs and length mismatches remain
visible without being interpreted.

Stored ECU profiles use the same low-memory transfer pattern. GET and POST
requests with `profile=<file>` exchange the validated profile document
directly, and the request or file buffer resides in PSRAM. This avoids a
second complete cJSON tree and API wrapper during profile load and save.

Complete-message buffers are still configured through `isotp_service`, so an
application can place them in PSRAM. The UDS client itself does not allocate
payload memory.

Automatic suppressed-response Tester Present keeps a confirmed non-default
diagnostic session alive at a configurable interval. A persistent UDS worker
owns client polling, so keep-alive and timeout handling do not depend on an
open browser page. Normal requests take priority; a due keep-alive is deferred
while the client has an outstanding request.

The manual UDS channel can transmit through a functional CAN identifier. ISO-TP
enforces the functional-addressing Single Frame restriction. The configured RX
identifier selects one physical ECU response for the stateful client, and
automatic Tester Present is disabled on a functional channel to avoid periodic
broadcast traffic. Functional multi-responder discovery remains a separate
collector concern and does not replace the active physical programming client.

## Current limitations

- one outstanding request per client;
- one configured ECU response identifier per functional UDS channel;
- no built-in OEM security-access algorithms;
- one ReadDataByIdentifier response is decoded at a time;
- resume is limited to confirmed image-segment boundaries because generic UDS
  does not guarantee partial RequestDownload support inside one segment;
- no authentication or role-based protection for destructive UDS requests.
