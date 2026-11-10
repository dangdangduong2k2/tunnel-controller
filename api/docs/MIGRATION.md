# Migration

Các import `Modbus`, `Mqtt`, `from_settings`, `tunnel_api` vẫn hỗ trợ.
Cài package thay cho sửa PYTHONPATH; constructor thấp cấp vẫn kết nối ngay.

| Trước | API mới |
|---|---|
| `Modbus(host=ip)` | `TunnelDevice.tcp(ip)` rồi `connect()` |
| `Modbus(serial_port=port)` | `TunnelDevice.rtu(port)` rồi `connect()` |
| `Mqtt(host, root=root)` | `TunnelDevice.mqtt(host, root=root)` rồi `connect()` |
| `status()` | `get_status()` |
| `call("speed", level=3)` | `set_speed(3)` hoặc `send_command("speed", level=3)` |

`get_status()` giữ key cũ, thêm `machine_state`, `epc_count`, `speed_level`,
`scan_result`. API thấp cấp `Mqtt.call` dùng named wire API cần firmware1.12;
`TunnelDevice` và `Mqtt.execute/command` dùng numeric wire, hỗ trợ1.11.
SDK0.1.0 không yêu cầu nâng/nạp firmware.
