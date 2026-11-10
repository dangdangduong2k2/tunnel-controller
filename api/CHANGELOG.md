# Changelog

## 0.1.4

- MQTT default request timeout is 5 s; console heartbeat interval is 1 s.
- Reuse correlated state for at most 1 s on firmware 1.12.3+, with a separate
  GET flight-time bound. Explicit status still reads; no command retries.
- Firmware 1.12.3 uses a 5 s token and 10 s MQTT lease; TCP/RTU retain 3 s.

## 0.1.3

- Standalone scan example now requires firmware 1.12.2; 1.12.1 still blocked
  reader START at the ESP USB layer after claim. Wire operations are unchanged.
- Preserve the original scan failure if cleanup also loses its connection.

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
