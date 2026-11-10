# Tunnel IoT Python SDK

Python SDK cho bo Tunnel qua **Modbus TCP**, **Modbus RTU / RS485** và **MQTT**.
Cùng một API để đọc EPC, điều khiển cửa/băng tải và cài đặt reader/antenna.

## Features

### Base APIs

- `TunnelDevice.tcp(...)`, `.rtu(...)`, `.mqtt(...)`
- `connect()`, `is_connected()`, `close()` và context manager `with`
- `get_status()`, `get_epc_count()`, `get_functions()`
- `claim()`, `has_control()`, `heartbeat()`, `release()`, `stop()`, `emergency()`
- `send_command(method, **params)`, `execute(op, args)`, `get_reader_result(sequence)`

### Device control

`reset`, `set_gate`, `set_belt`, `set_scan`, `set_mode`, `confirm`, `rescan`,
`set_speed`, `set_maintenance`.

### Reader and antenna

`get_reader_info`, `get_antenna_power`, `set_antenna_power`, `set_antenna_mask`,
`get_rf_profile`, `set_rf_profile`, `get_gen2x`, `set_gen2x`, `get_ex10`,
`measure_return_loss`.

**Đủ 19 OP đang được firmware cung cấp.** [API Reference](docs/API_REFERENCE.md)
có cách gọi từng hàm, tham số, khoảng giá trị, tác động và cách lấy kết quả.

## Prerequisite

- Python 3.10 trở lên. SDK 0.1.0 có version riêng với firmware.
- Modbus: map của firmware 1.10.2 trở lên. MQTT: firmware 1.11.0 trở lên.
- IP/COM hoặc broker/root theo cấu hình bo. TCP mặc định 502; RTU ID1,19200,8E1.
- MQTT dùng broker của hệ thống, không cần tài khoản Tuya. Firmware chưa hỗ trợ TLS.

## Installation

Giải nén SDK, mở terminal tại thư mục có `pyproject.toml`:

```console
python -m pip install ".[all]"
```

Chỉ TCP: `python -m pip install .`; RTU: `".[rtu]"`; MQTT: `".[mqtt]"`.
Trong checkout firmware dùng `python -m pip install "./api[all]"` từ gốc repo.
Gói chưa publish lên PyPI; không cài theo tên từ PyPI.

## Usage

```python
from tunnel_sdk import TunnelDevice

# Thay bằng IP thực tế trên DWIN.
with TunnelDevice.tcp("192.168.1.211") as device:
    state = device.get_status()
    print(state["epc_count"], state["total_reads"], state["ip"])
```

Đổi transport, giữ nguyên các lời gọi phía sau:

```python
device = TunnelDevice.rtu("COM7", baud=19200, parity="E", unit=1)
device = TunnelDevice.mqtt("broker.example.com", root="factory/tunnel01/v1")
```

Constructor mới chưa kết nối. Dùng `connect()` hoặc `with device:`.
`get_status()` đọc mới, không claim, không gia hạn lease và không reset bộ đếm.

## Sample code

- [TCP](example/read_tcp.py), [RTU](example/read_rtu.py), [MQTT](example/read_mqtt.py): chỉ đọc.
- [Reader power](example/reader_power.py): claim, maintenance, heartbeat, đọc reply, cleanup.
- [Cách chạy](example/README.md).

## API reference

- [Toàn bộ hàm và cách gọi](docs/API_REFERENCE.md)
- [Trạng thái và lỗi](docs/STATUS_AND_ERRORS.md)
- [Bắt đầu và cấu hình](docs/GETTING_STARTED.md)
- [User Guide ba giao thức](docs/Tunnel_User_Guide_VI.pdf)
- [Migration từ SDK cũ](docs/MIGRATION.md)

Claim đưa máy về STOP/HOLD. Ứng dụng chủ động heartbeat khoảng 500 ms; lease 3000 ms.
`accepted=True` là nhận lệnh, chưa phải hoàn tất. Reader cần reply đúng sequence;
cơ khí cần theo dõi trạng thái. Không tự chạy lại sau `UnknownOutcome`.
SDK không tự claim, heartbeat, reconnect hoặc retry; `close()` không gửi lệnh máy.

## Development

```console
python -m pip install -e ".[all]"
python -m unittest discover -s tests -v
python -m pip install build
python -m build
```

Test mặc định dùng thiết bị giả lập, không mở COM hay điều khiển bo thật.
Xem [tests](tests/README.md) và [Changelog](CHANGELOG.md).

## Issue feedback

Gửi SDK/firmware version, transport, code tối thiểu và exception/ACK/state qua
kênh bàn giao hoặc issue repository private. Xóa password khỏi log.

## License

Phân phối private theo thỏa thuận với chủ sở hữu. Xem [LICENSE](LICENSE).
Thư viện bên thứ ba giữ giấy phép riêng.
