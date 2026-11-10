# Changelog

## 0.1.2

- Added `example/scan_tags.py`: independent RF scan without homing or motor commands,
  firmware 1.12.1 version check, explicit claim/heartbeat/STOP/release.
- Documented the updated OP4 behavior; no register addresses or wire OPs changed.


## 0.1.1

- One persistent console covers all 19 operations over TCP, RTU and MQTT,
  including reader completion/readback and explicit session management.
- `BusyError` identifies Modbus exception 6 and MQTT ownership/busy responses.
  It remains a `ProtocolError` for existing callers.
- `require_accepted(ack)` optionally raises `BusyError` for application result 2;
  command methods keep their ACK dictionary return contract.
- Added session BUSY/no-replay, ownership loss and readback regression checks.
- Firmware wire protocol and register map are unchanged.

## 0.1.0

- SDK version is independent of firmware version.
- Installable Python package with optional MQTT/RTU dependencies.
- `TunnelDevice` lifecycle and 19 named operation methods for all transports.
- MQTT numeric OP encoding supports firmware 1.11.0 and 1.12.0.
- Validated reader result retrieval, complete API reference and runnable examples.
- Existing `Modbus`, `Mqtt`, `from_settings` and `tunnel_api` imports remain available.
