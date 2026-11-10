# SDK 0.1.1 validation - 11/11/2026

- Built sdist and wheel from sdist; installed wheel into a new Python 3.12 venv.
  All 16 SDK tests passed against that installed wheel with isolated Python and
  no PYTHONPATH. All 5 example --help commands passed without device connections.
- Sdist includes console, tests and handoff documents. Markdown Python snippets
  parsed successfully. Ruff check passed.

- 16 SDK test methods including 57 operation/transport combinations (19 x 3),
  all 19 console commands, BUSY decoding on all transports, persistent ownership,
  pending-reader heartbeat, timeout/no replay and readback mismatch.
- 5 existing SDK failure tests passed.
- 17 native-core TCP/RTU integration assertions passed. TCP uses localhost sockets;
  RTU serial is simulated, including framing, CRC and fragmented replies.
- MQTT tested with localhost broker, Paho and native production core. Public API
  emits numeric OPs; new console read reader data and released control. A second
  MQTT client received BusyError/code5 while the first retained ownership.
- Four rebuilt native suites passed: integration (real Tunnel/reader),
  modbus_integration, modbus_core and mqtt_api. Inventory rejects maintenance,
  speed/mode changes and reader config without stopping the active scan. Pending
  reader rejects the next OP without queueing it. Reads, heartbeat and STOP remain
  available. MQTT preserves result2 and deduplicates the refused request.
- Project structure, Python syntax, local Markdown links and generated contracts passed.
- Firmware source and wire contract are unchanged; this release exposes and tests
  the BUSY behavior already implemented by the board firmware.

No physical machine, COM port or remote broker was used during these checks.
The workflow includes Python 3.10-3.13 on Windows/Linux, but that remote matrix
has not been run here. The local verification used Windows/Python 3.12.
