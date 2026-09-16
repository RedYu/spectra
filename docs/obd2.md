# OBD-II diagnostics

Spectra exposes a browser OBD-II console at `/obd2`. It reuses the existing
ISO-TP service and UDS Web API transport; no additional protocol task, queue,
or HTTP API endpoint is allocated. The page occupies the former legacy
`/can_test` redirect handler slot.

The page currently supports:

- Mode `01` live powertrain data with one-shot and periodic polling;
- common standardized PIDs for engine load, temperatures, RPM, speed, MAF,
  throttle, runtime, fuel level, and module voltage;
- supported-PID and monitor-status decoding;
- stored, pending, and permanent DTC reads through modes `03`, `07`, and `0A`;
- confirmed diagnostic-information clearing through Mode `04`;
- VIN, calibration ID, and CVN requests through Mode `09`;
- decoded and color-coded raw CAN tracing.

Functional requests default to CAN ID `0x7DF`, while physical requests default
to `0x7E0`. The receive ID selects one ECU, normally `0x7E8` through `0x7EF`.
This matters for functional requests: multiple ECUs may answer `0x7DF`, but one
Spectra ISO-TP channel intentionally follows only the configured response ID.

OBD-II responses use the same complete-message buffers and timeout processing
as manual UDS requests. PID formulas and DTC formatting are evaluated in the
browser, so adding presentation support does not reserve more device RAM.
