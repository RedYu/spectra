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
bytes through allocation-free views of the ISO-TP response buffer.

Complete-message buffers are still configured through `isotp_service`, so an
application can place them in PSRAM. The UDS client itself does not allocate
payload memory.

## Current limitations

- one outstanding request per client;
- physical addressing only through the selected ISO-TP channel;
- no periodic Tester Present scheduler;
- no security-access algorithm integration;
- no typed DID, DTC, routine, download, or transfer decoders yet;
- no UDS Web API yet.

The next layer should add typed request builders for Diagnostic Session Control,
Tester Present, ECU Reset, and Read Data By Identifier before exposing UDS in
the web interface.
