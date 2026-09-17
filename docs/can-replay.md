# CAN traffic replay

Spectra can replay its binary SCL recordings and ASC files produced by the
device. The replay service streams records from `/logs/can` and never loads a
complete recording into RAM.

Each selected RX frame or completed TX frame is submitted through the central
CAN router. The service waits for its final hardware transmission confirmation
before advancing, preserving order and preventing an unbounded pending-TX
queue. CAN FD frames cannot be remapped to the Primary TWAI interface.

The scheduler uses the first selected record as time zero and applies the
configured rational speed multiplier, or sends at the maximum rate allowed by
the CAN interface. Pause time is removed from the replay timeline. A recording
can run once, a fixed number of times, or continuously until Stop. Filters
select a time range, source buses, RX/TX direction, remote frames, and an
inclusive identifier range. Frames can retain their recorded bus or be mapped
to one output bus.

Late-frame behavior is configurable:

- `WAIT` preserves every selected frame and reports accumulated lag;
- `DROP_LATE` drops frames beyond the configured lag threshold;
- `STOP_ON_LAG` terminates replay when that threshold is exceeded.

`GET /api/can/replay` returns state, file progress, replay time, elapsed time,
current and maximum lag, repeat number, records read, selected, submitted,
completed, failed, dropped and skipped frames, the pending transaction, and
the latest ESP-IDF result.

`POST /api/can/replay` accepts `start`, `pause`, `resume`, and `stop`. Start is
restricted to `.scl` or `.asc` files inside `/logs/can`. The Web UI lists
compatible recordings and also accepts a manual path. It requires explicit
confirmation because replayed traffic can operate vehicle systems. Replay
should only be used on a controlled bus with the correct bitrate, mode,
termination, and power conditions.

Scheduling precision is bounded by the FreeRTOS tick, controller availability,
CAN arbitration, SD latency, and other real-time activity. Reported timing lag
must therefore be considered when validating a replay.
