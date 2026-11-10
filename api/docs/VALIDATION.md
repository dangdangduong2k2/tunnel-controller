# SDK 0.1.0 validation - 11/11/2026

- Built sdist and wheel from sdist successfully.
- Installed wheel into a clean Python 3.12 environment; no PYTHONPATH setup.
- 7 SDK test methods including 57 operation/transport combinations (19 x 3).
- 5 existing SDK failure tests passed.
- 15 native-core TCP/RTU integration assertions passed. TCP uses localhost sockets;
  RTU serial is simulated, including framing, CRC and fragmented replies.
- MQTT tested with localhost broker, Paho and native production core. Public API
  emits numeric OPs; reader example completed and released control.
- Markdown Python examples parsed; helper signatures and local document links checked.
- All 4 example --help commands passed using the installed wheel.
- Ruff and generated-contract checks passed; no firmware changes required.

No physical machine, COM port or remote broker was used during these checks.
The workflow includes Python 3.10-3.13 on Windows/Linux, but that remote matrix
has not been run here. The local verification used Windows/Python 3.12.
