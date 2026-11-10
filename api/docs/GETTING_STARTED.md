# Bắt đầu

## Kết nối

Bo Ethernet vào LAN có DHCP; máy tính cùng LAN. Xem IP tại DWIN → Kết nối.
RTU dùng USB-RS485; theo bo đã đối chiếu, J11 chân 1=B, 2=GND, 3=A.
Cổng USB nạp ESP32 không phải RTU. Dùng thông số thực tế đã lưu trên bo.
MQTT: bo và ứng dụng cùng broker/root, cấu hình bo qua `http://<IP>/`.

Mặc định TCP502/Unit1, RTU19200/8E1/ID1, MQTT1883/root `tunnel/01/v1`.
Khi đã vào fallback192.168.4.1, firmware1.11/1.12 hiện cần reset với cáp LAN có
DHCP đã cắm để thử DHCP lại. Xem User Guide đính kèm.

## Tạo kết nối

```python
from tunnel_sdk import TunnelDevice

tcp = TunnelDevice.tcp("192.168.1.211", port=502, unit=1, timeout=1.0)
rtu = TunnelDevice.rtu("COM7", baud=19200, parity="E", stop_bits=1, unit=1)
mqtt = TunnelDevice.mqtt("broker.example.com", port=1883,
                        username="user", password="secret",
                        root="factory/tunnel01/v1", timeout=1.0)
```

Chọn một instance; không cần kết nối cả ba. Thay giá trị mẫu bằng cấu hình thật.
Đặt credentials trong biến môi trường/file riêng không commit. Mật khẩu web khác
MQTT password. `is_connected()` là trạng thái cục bộ, không phải ping thiết bị.

## Phiên điều khiển

1. `connect()` và `get_status()`; kiểm tra điều kiện máy.
2. `claim()`; kiểm tra `accepted` và `has_control()`.
3. Lập lịch `heartbeat()` mỗi khoảng500ms, lease3000ms.
4. Gửi lệnh, kiểm tra ACK và theo dõi trạng thái để biết hoàn tất.
5. Reader: `set_maintenance(1)` trước lệnh; chờ reply đúng sequence.
6. Hết reader pending: `set_maintenance(0)`, `release()`, `close()`.

Đọc không gia hạn lease. Claim STOP/HOLD và khóa điều khiển thường trên DWIN.
Reset có thể homing hai cửa. Không có thread heartbeat tự động. Các lời gọi trên
một instance được tuần tự hóa; chọn timeout/polling để không trì hoãn heartbeat
quá lease. Không dùng nhiều master trên cùng bus RS485.

`close()` không tự gửi release. MQTT owner hết lease nếu không release;
firmware xử lý Modbus TCP disconnect để dừng/nhả quyền.

## MQTT phía SDK

SDK chờ SUBACK `/state`, `/ack`; `get_status()` gửi `/get`. Trước mỗi lệnh,
SDK lấy state mới để điền session/epoch/seen_ms và ghép ACK theo
session/epoch/client/id. Broker cấp quyền publish `/get`, `/cmd` và subscribe
`/state`, `/ack`; `/availability` dành cho monitor ngoài SDK.
API `TunnelDevice` chuyển tên Python thành OP/args, dùng được từ firmware1.11.
Không cần tài khoản cloud riêng của SDK. Một owner chung cho ba giao thức.
