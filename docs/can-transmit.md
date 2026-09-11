# CAN transmission jobs

The device owns four volatile slots (0–3). Nothing is transmitted at boot or
page load. A job starts only after an explicit `start` request. Closing a browser
does **not** stop a running job. Use only on authorized equipment, initially on a
bench with a receiving/acknowledging CAN node.

Primary supports Classical CAN; Secondary also supports CAN FD/BRS when enabled
in its interface settings. Standard and extended identifiers are supported.
Only Normal operating mode is allowed. RTR and ESI requests are rejected.

## HTTP API

`GET /api/can/transmit` returns `jobs`, an array of four snapshots:
`slot`, `state`, `bus`, `next_id`, `next_dlc`, `count`, `interval_ms`, `attempts`,
`queued`, `completed`, `failed`, `aborted`, `unknown`, `pending`, `last_error`.
`pending` is a router transaction ID, or zero. States: `idle`, `active`,
`stopped`, `complete`, `error`, `unknown`.

`POST /api/can/transmit` requires `Content-Type: application/json`, at most 2048
bytes. This endpoint shares the existing device web server's access model; it
does not introduce authentication. Do not expose it to untrusted networks.

Example body (documentation only; sends three real frames if submitted):

```json
{
  "action": "start", "slot": 0, "bus": 0,
  "id": 291, "extended": false, "fd": false, "brs": false,
  "dlc": 8, "data": [0,0,0,0,0,0,0,0],
  "interval_ms": 100, "count": 3,
  "id_step": 0, "id_end": 291,
  "increment_dlc": false, "dlc_end": 8,
  "data_offset": 0, "data_width": 1, "data_step": 1,
  "data_big_endian": false
}
```

All JSON numbers are decimal. The web editor displays ID and ID step/end in hex.
`bus`: 0 Primary, 1 Secondary. `slot`, `bus`, `id`, `dlc`, `data`, and `count` are
required for start. Optional flags default false, interval defaults to 100 ms,
ID step to zero, end to initial ID, DLC end to initial DLC, counter width and
offset to zero, and counter step to one.

Stop a slot: `{"action":"stop","slot":0}`. Stop all four:
`{"action":"stop_all"}`. Stop only prevents new submissions; this router API
cannot retract an already queued hardware frame or its hardware retransmissions.
The configured controller retransmission policy remains in force.

Responses: 200 command accepted (not a TX confirmation), 400 invalid configuration,
409 slot busy/interface unavailable or service error, 413 oversized/empty body,
415 unsupported content type. GET returns 503 when snapshots are unavailable.
Never automatically retry `start` after an HTTP timeout: first inspect job status.

## Scheduling and counters

- Interval: 10–3,600,000 ms. Best-effort FreeRTOS timing, not a hard real-time generator.
- Count: 1–1,000,000 submission attempts; zero runs until stopped. A defensive
  counter limit also stops an unlimited job at UINT32_MAX attempts.
- One outstanding frame per job, four in total. No catch-up bursts after delays.
- `queued` means accepted by the router, `completed` means controller confirmation.
  `failed` includes rejected submissions and failed confirmations; `aborted` is separate.
- Immediate failure or unsuccessful confirmation stops the job. A missing
  confirmation after five seconds stops it as `unknown`, **not** confirmed or failed.
  A missing event can result from queue loss; a late frame may still transmit.
- Stop retains pending accounting. A slot cannot be overwritten until pending is
  resolved or times out. A new start resets that slot's counters and initial frame.
- Jobs and configuration are not persisted. Lifecycle shutdown quiesces the service
  before CAN/router teardown. The worker/queue are system-lifetime allocations
  (~4 KiB stack plus bounded job and confirmation storage); no task is created per job.

## Increments

Advance after each **accepted submission**, not every scheduler wakeup.
ID adds a positive step and wraps to the initial ID when exceeding `id_end`.
DLC advances by one through `dlc_end`, then wraps. Classical DLC is 0–8;
FD DLC is 0–15 with lengths 0–8,12,16,20,24,32,48,64. Initial DATA must exactly
match initial DLC. Newly exposed payload bytes are zero; truncated bytes are cleared.
DATA is a 1–8-byte unsigned counter at `data_offset`, with chosen endianness and
positive 32-bit step. It wraps modulo its byte width and must fit the shortest
(initial) payload. Width zero disables it. ID, DATA and DLC increments can coexist.

## Verification

Pure sequence/validation Unity tests are in
`components/services/test/test_can_transmit_sequence.c` (tag `[can_tx]`). They do
not transmit frames. Run with the ESP-IDF unit-test application and this component.

Hardware acceptance checklist (not replaced by compilation):

1. Listen-only, stopped bus, bus-off and incompatible FD settings reject Start.
2. On an isolated bench, send one Classical frame on each channel; verify bytes,
   ID and exactly one confirmation on an external analyzer.
3. Send three frames at 100 ms; verify count, ID wrap and both counter byte orders.
4. Check Secondary FD DLC 8→9→10→8 and zero padding on the external analyzer.
5. Stop an unlimited job from a second browser; confirm no new submissions after
   Stop returns, while already queued transmissions may complete.
6. Remove the acknowledging node; verify bounded pending, unknown/error handling
   and no replay burst after reconnection. Repeat under logger/WebSocket load.
