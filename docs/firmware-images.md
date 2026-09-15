# Firmware image readers

Spectra provides allocation-free streaming readers in the `uds` component for
raw binary, Intel HEX, Motorola S-record, and BHX firmware images. The parser is
independent of the SD-card and Web layers: the caller supplies a random-access
read callback and the parser returns normalized address/data blocks.

The implementation does not load a complete image into RAM. Text formats use
a 256-byte read-ahead buffer and a bounded line buffer. A complete inspection
reports the decoded byte count, inclusive address range, block and contiguous
segment counts, and an optional execution entry address.

## Raw binary

A BIN image is a contiguous sequence of bytes. Its start address must be
provided by the caller. The reader emits bounded consecutive blocks beginning
at that address.

## Intel HEX

The reader supports:

- data records (`00`);
- end-of-file records (`01`);
- extended segment address records (`02`);
- start segment address records (`03`);
- extended linear address records (`04`);
- start linear address records (`05`).

Every record length and two's-complement checksum is validated. Data records
must be ordered and non-overlapping, cannot wrap a 16-bit record address, and
a valid EOF record is required. Non-whitespace data after EOF is rejected.

## Motorola S-record

The reader supports S0 headers, S1/S2/S3 data, S5/S6 record counts, and
S7/S8/S9 termination records. It validates record length, one's-complement
checksum, consistent data-address width, optional declared record count,
ordered non-overlapping data, the matching termination type, and trailing
content.

## BHX

BHX is parsed as a big-endian multi-section container. A 12-byte `GHDR`
contains version `1` and the total number of firmware data bytes. It is
followed by one or more `SHDR` records. Each section header contains version
`1`, a 32-bit load address, a 32-bit data size, and the marker `0xC0DECAFE`.

The reader emits every section at its embedded load address. It validates all
signatures, versions, section and total sizes, address overflow, ordered
non-overlapping sections, truncation, and unexpected trailing data. Section
headers are not included in the reported firmware data size.

Recognized extensions are `.bin`, `.hex`, `.ihex`, `.srec`, `.s19`, `.s28`,
`.s37`, `.mot`, and `.bhx`, case-insensitively.

## UDS programming integration

The Web UDS programming path validates the complete image before changing the
ECU session. BIN uses the address entered by the user. Intel HEX, S-record,
and BHX use their embedded addresses. Adjacent blocks are combined into segments;
each discontinuous segment receives its own RequestDownload, TransferData
sequence, and RequestTransferExit. Programming session entry, SecurityAccess,
erase, verification, reset, and default-session restoration still run once for
the complete image.

The progress API reports both total image progress and the current segment.
Parser state and the segment index are allocated in PSRAM and released when the
programming file is closed.
