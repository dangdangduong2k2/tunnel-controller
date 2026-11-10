# SDK Python và bộ bàn giao API

Chạy `python -m pip install -r api/requirements.txt`. Import `tunnel_sdk` khi thư mục `api` có trên PYTHONPATH; `tunnel_api.py` vẫn là compatibility shim/CLI. Không cần SDK riêng cho từng hãng PLC: dùng bảng [Modbus](../docs/MODBUS.md) hoặc contract [MQTT](../docs/MQTT.md).

## Đọc trạng thái

```python
from tunnel_sdk import Modbus, Mqtt

device = Modbus(host="192.168.1.211")  # thay IP thực tế
# device = Modbus(serial_port="COM7", baud=19200, parity="E", unit=1)
# device = Mqtt("broker.example.com", root="factory/tunnel01/v1")
try:
    state = device.status()
    print(state["unique_epcs"], state["total_reads"], state["ip"])
finally:
    device.close()
```

TCP dùng socket giữ kết nối; RTU dùng pyserial. MQTT constructor chờ subscribe xong, `status()` mới gửi `/get`; không tự poll. Status SDK giải mã `registers` để ba transport có cùng kiểu dữ liệu. Tên `unique_epcs` trong SDK tương ứng `epc_count` trong JSON wire mới.

## Điều khiển có chủ đích

Các hàm chung: `claim`, `heartbeat`, `release`, `stop`, `emergency`, `status`, `close`, `execute(op,args)`, `command(name,args)`, `call(method,**params)`. [Danh sách method/params](../docs/COMMANDS.md).

```python
# Chỉ gọi khi vận hành cho phép; claim đưa máy về STOP/HOLD.
ack = device.claim()
if not ack["accepted"]:
    raise RuntimeError(ack)
# Heartbeat mỗi khoảng500ms do ứng dụng chủ động lập lịch.
ack = device.call("maintenance", enabled=1)
if not ack["accepted"]:
    raise RuntimeError(ack)
ack = device.call("antenna.power.get")
# Chờ reader_state/reader_sequence/reply, kiểm tra cả transport và status reader.
# Khi xong: maintenance(enabled=0), release; không suy ra hoàn tất từ ACK nhận lệnh.
```

Ví dụ đầy đủ có heartbeat/timeout/reader error/cleanup tại [mqtt_read_power.py](mqtt_read_power.py). Không chạy script điều khiển chỉ để xem giao diện; [mqtt_demo.py](mqtt_demo.py) chỉ đọc state theo số mẫu. `close()` không âm thầm gửi một lệnh máy; MQTT owner hết lease nếu ứng dụng không release được.

`ProtocolError`: lỗi phản hồi/exception/quyền điều khiển. `UnknownOutcome`: timeout/mất ACK sau khi có thể đã gửi lệnh; không tự retry chuyển động. SDK không tự reconnect/claim, không tạo thread heartbeat tự động. Xem [quy tắc chung](../docs/API.md).

## Cấu hình mẫu

- [TCP](settings.tcp.example.json), [RTU](settings.rtu.example.json), [MQTT](mqtt_config.example.json).
- Copy file mẫu thành file riêng, điền IP/COM/root/account thực tế. `mqtt_username` là tên trong **SDK config**; web firmware dùng key `mqtt_user`.
- CLI: `python api/tunnel_api.py --settings api/settings.tcp.example.json` (chỉ đọc).
- MQTT demo: `python api/mqtt_demo.py --config api/mqtt_config.example.json --samples 3`.

Thư mục [tunnel_sdk](tunnel_sdk/README.md) mô tả từng module. [examples](examples/README.md) chứa frame JSON mẫu; token/session/id phải lấy từ bo thật. [reference](reference/README.md) chỉ là tài liệu1.11 đối chiếu, không thay thế tài liệu hiện hành.
