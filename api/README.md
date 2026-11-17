# Tunnel IoT Python SDK

**[User Guide tiếng Việt - VI 1.1, 28 trang](docs/Tunnel_User_Guide_VI.pdf)** (cập nhật 17/11/2026).

Hướng dẫn kết nối Modbus TCP theo DHCP/Static (trang 24), cài đặt HMI, MQTT, OTA và scan độc lập (trang 25-28); kèm bảng thanh ghi và lệnh của ba giao thức. Guide cập nhật theo firmware **1.16.4**; các mục HMI/OTA cần firmware và gói màn tương ứng. Kiểm tra phiên bản thực tế của bo tại IR1 trước khi áp dụng.

Chọn đúng giao thức đang dùng để xem cách kết nối và **toàn bộ hàm của giao thức đó**:

- [1. Modbus RTU / RS485](#modbus-rtu): COM, baud, Slave ID, hàm đọc/ghi và ví dụ RTU.
- [2. Modbus TCP/IP](#modbus-tcp): IP, port, Unit ID, hàm đọc/ghi và ví dụ TCP.
- [3. MQTT](#mqtt): broker, topic, hàm request/response và ví dụ MQTT.
- [4. Kết quả, lỗi và tài liệu](#results).

Cả ba giao thức hỗ trợ đủ19 OP hiện có của firmware. Tên hàm Python giống nhau;
kết nối, cách truyền lệnh và cách quản lý phiên được ghi riêng ở từng mục.
SDK 0.1.4, Python 3.10+. Modbus dùng map từ firmware 1.10.2; MQTT cần firmware 1.11.0+.

Ví dụ chạy đủ **19 lệnh trên cả RTU, TCP/IP và MQTT**: [device_console.py](example/device_console.py).
Xem [bảng lệnh và các tình huống](example/README.md): giữ phiên Confirm/Rescan, cài
reader có readback, BUSY, mất quyền, timeout và kết thúc phiên.

## Cài SDK

Trong repository này, mở terminal tại thư mục `api`:

```console
cd api
python -m pip install ".[all]"
```

Nếu dùng ZIP SDK độc lập, mở tại thư mục chứa `pyproject.toml` và bỏ bước `cd api`.
Các lệnh cài/chạy ví dụ bên dưới đều tính từ thư mục SDK này. Gói chưa publish lên PyPI.

<a id="modbus-rtu"></a>
## 1. Modbus RTU / RS485

Bo là **RTU slave**; PLC/máy tính là master. Máy tính dùng USB-RS485, không dùng
cổng USB nạp ESP32 làm RTU. Trên bo đã đối chiếu: J11 chân1=B,2=GND,3=A.
Mặc định ID1,19200 baud,8E1; dùng thông số thực tế trong DWIN → Kết nối → Modbus RTU.

### Cài đặt và kết nối RTU

```console
python -m pip install ".[rtu]"
```

```python
from tunnel_sdk import TunnelDevice

with TunnelDevice.rtu("COM7", baud=19200, parity="E",
                      stop_bits=1, unit=1, timeout=1.0) as rtu:
    state = rtu.get_status()
    print(state["machine_state"], state["epc_count"], state["total_reads"])
```

Thay COM7 bằng COM của USB-RS485. Data bits=8, parity nhận N/E/O, stop_bits1/2.
Constructor chưa mở cổng; `with` gọi connect/close. Ví dụ này chỉ đọc.

### Các hàm RTU cung cấp

**Kết nối, đọc và quyền điều khiển**

| Hàm gọi | Trả về / tác dụng |
|---|---|
| `rtu.connect()` | Mở kết nối, trả instance; không claim hoặc chạy máy. |
| `rtu.is_connected()` | bool trạng thái transport cục bộ; không phải phép kiểm tra bo đang online. |
| `rtu.close()` | Đóng transport; không tự gửi lệnh máy hoặc release. |
| `rtu.get_status()` | Dictionary trạng thái mới, EPC, lỗi, IP, owner; FC04 đọc IR0..95. |
| `rtu.get_epc_count()` | int số EPC duy nhất từ một lần đọc mới. |
| `rtu.get_functions()` | Danh mục19 OP và tham số của SDK; không truy cập bo. |
| `rtu.claim()` | ACK; lấy quyền và STOP/HOLD máy. |
| `rtu.has_control()` | bool: đúng client này có quyền hay không; không gia hạn lease. |
| `rtu.heartbeat()` | ACK; chủ động gọi khoảng500 ms để giữ quyền. |
| `rtu.release()` | ACK; owner dừng/HOLD và nhả quyền. |
| `rtu.stop()` | ACK; STOP toàn máy, giữ EN và nhả quyền; không cần là owner. |
| `rtu.emergency()` | ACK; dừng và nhả EN hai cửa, có thể mất lực giữ. Kiểm tra owner riêng. |
| `rtu.send_command("speed", level=3)` | Gọi lệnh theo tên; trả ACK. |
| `rtu.execute(8, [3])` | Gọi trực tiếp OP8/P0=3; tự thêm0 cho đủ6 tham số; trả ACK. |
| `rtu.get_reader_result(sequence=1, expected_command=0x94)` | None nếu chưa có reply khớp; dictionary reply nếu thành công; lỗi thì ReaderError. Dùng sequence thực tế từ ACK. |

FC06 ghi HR256:1 lấy/duy trì,0 nhả quyền. Đọc không gia hạn lease. Lease hết sau3000 ms không được duy trì.

**Đủ19 lệnh máy và reader**

Mỗi hàng là ví dụ độc lập; thay giá trị theo nhu cầu. Trước lệnh OP cần claim;
OP20..29 cần thêm maintenance. Mọi hàm ở bảng dưới trả ACK trước, chưa phải kết quả hoàn tất.

| OP | Hàm gọi | Tham số / tác động |
|---:|---|---|
| 1 | `rtu.reset()` | Reset lỗi/STOP và homing; có thể chuyển động hai cửa. |
| 2 | `rtu.set_gate(close=1)` | 1=đóng/homing;0=mở. Cần trạng thái cho phép. |
| 3 | `rtu.set_belt(run=1)` | 1=chạy thuận;0=dừng riêng băng tải. Kiểm tra liên động. |
| 4 | `rtu.set_scan(run=1)` | 1=bắt đầu RFID;0=dừng. Từ FW1.12.2: không cần home, mọi motor phải dừng. |
| 5 | `rtu.set_mode(mode=1)` | 0=AUTO;1=SEMI. Đổi khi máy ở trạng thái phù hợp, đã homing. |
| 6 | `rtu.confirm()` | Xác nhận cho hàng qua; chỉ khi SEMI đang chờ xác nhận (state9). |
| 7 | `rtu.rescan()` | Quét lại; chỉ khi SEMI đang chờ xác nhận (state9). |
| 8 | `rtu.set_speed(level=3)` | Mức1..5; máy rảnh và reader ready; lưu tốc độ vào NVS. |
| 9 | `rtu.set_maintenance(enabled=1)` | 1=vào;0=thoát bảo trì reader; máy dừng và reader ready. |
| 20 | `rtu.get_reader_info()` | Yêu cầu thông tin reader; đọc reply để lấy thông tin/mask antenna. |
| 21 | `rtu.get_antenna_power()` | Yêu cầu đọc công suất ANT1..4; kết quả dBm trong reply. |
| 22 | `rtu.set_antenna_power(20, 20, 20, 20, ram_only=1)` | Bốn công suất0..30 dBm; ram_only=1 chỉ RAM,0 lưu reader. |
| 23 | `rtu.set_antenna_mask(mask=5, ram_only=1)` | Mask1..15;5=ANT1+ANT3;15=cả bốn. ram_only=1 RAM,0 lưu. |
| 24 | `rtu.get_rf_profile()` | Yêu cầu đọc profile legacy. |
| 25 | `rtu.set_rf_profile(encoded=128)` | Byte đã mã hóa; bit7=1 và profile thuộc allowlist. |
| 26 | `rtu.get_gen2x()` | Yêu cầu đọc profile Gen2X16-bit. |
| 27 | `rtu.set_gen2x(value=4123)` | Đặt ID thuộc allowlist Gen2X và được reader hỗ trợ. |
| 28 | `rtu.get_ex10()` | Yêu cầu đọc thông tin phiên bản Ex10. |
| 29 | `rtu.measure_return_loss(frequency_khz=915000, antenna=0)` | 840000..960000 kHz, chia hết cho100 hoặc125; antenna0..3=ANT1..4. |

Sau `get_antenna_power()`, lấy `ack["sequence"]` và gọi
`rtu.get_reader_result(ack["sequence"], expected_command=0x94)` để nhận dữ liệu.
`reply["data"]` chứa bốn công suất dBm khi đủ4 byte; None nghĩa là tiếp tục chờ
với deadline và heartbeat. Set công suất/mask cần Get đọc lại để xác nhận.
Khoảng, allowlist và điều kiện đầy đủ: [API Reference](docs/API_REFERENCE.md).

### Giao thức RTU và chạy thử

| Function Code | Dùng để |
|---|---|
| FC03 | Đọc Holding Register: quyền của client, SEQ và kết quả lệnh. |
| FC04 | Đọc Input Register: trạng thái máy, EPC, reply reader, thông số mạng. |
| FC06 | Ghi HR256=claim/heartbeat/release;HR257=Emergency;HR258=STOP. |
| FC16 (0x10) | Ghi một khối8 word HR272..279: `[SEQ,OP,P0,P1,P2,P3,P4,P5]`. |

Offset bắt đầu từ0. Word big-endian; CRC16 gửi byte thấp trước và được SDK tự tính.
Chỉ một master trên bus. Sai ID/CRC hoặc broadcast ID0 không có phản hồi.
Mở lại COM không xóa SEQ trong bo; SDK đọc HR272 để lấy SEQ tiếp theo.

```console
python example/read_rtu.py --port COM7 --baud 19200 --parity E
```

Khi máy rảnh, muốn thực hiện claim/maintenance để đọc công suất:

```console
python example/reader_power.py --transport rtu --serial-port COM7 --baud 19200 --parity E
```

Script thứ hai làm STOP/HOLD khi claim, có heartbeat/chờ reply/cleanup.
Source: [read_rtu.py](example/read_rtu.py), [reader_power.py](example/reader_power.py).

<a id="modbus-tcp"></a>
## 2. Modbus TCP/IP

Bo là **Modbus TCP server** trên RJ45. PLC/máy tính kết nối trực tiếp đến IP bo.
Xem IP trên DWIN → Kết nối → Ethernet/IP. Mặc định port502, Unit ID1; Unit ID dùng chung
với RTU. Đây là server riêng trên bo, không phải gateway chuyển tiếp RS485.

### Cài đặt và kết nối TCP/IP

```console
python -m pip install .
```

```python
from tunnel_sdk import TunnelDevice

# Thay IP ví dụ bằng IP thực tế của bo.
with TunnelDevice.tcp("192.168.1.211", port=502, unit=1, timeout=1.0) as tcp:
    state = tcp.get_status()
    print(state["machine_state"], state["epc_count"], state["ip"])
```

TCP không cần thư viện ngoài Python. Constructor chưa kết nối;
`with` gọi connect/close. Ví dụ này chỉ đọc, không claim hoặc chạy máy.

### Các hàm TCP cung cấp

**Kết nối, đọc và quyền điều khiển**

| Hàm gọi | Trả về / tác dụng |
|---|---|
| `tcp.connect()` | Mở kết nối, trả instance; không claim hoặc chạy máy. |
| `tcp.is_connected()` | bool trạng thái transport cục bộ; không phải phép kiểm tra bo đang online. |
| `tcp.close()` | Đóng transport; không tự gửi lệnh máy hoặc release. |
| `tcp.get_status()` | Dictionary trạng thái mới, EPC, lỗi, IP, owner; FC04 đọc IR0..95. |
| `tcp.get_epc_count()` | int số EPC duy nhất từ một lần đọc mới. |
| `tcp.get_functions()` | Danh mục19 OP và tham số của SDK; không truy cập bo. |
| `tcp.claim()` | ACK; lấy quyền và STOP/HOLD máy. |
| `tcp.has_control()` | bool: đúng client này có quyền hay không; không gia hạn lease. |
| `tcp.heartbeat()` | ACK; chủ động gọi khoảng500 ms để giữ quyền. |
| `tcp.release()` | ACK; owner dừng/HOLD và nhả quyền. |
| `tcp.stop()` | ACK; STOP toàn máy, giữ EN và nhả quyền; không cần là owner. |
| `tcp.emergency()` | ACK; dừng và nhả EN hai cửa, có thể mất lực giữ. Kiểm tra owner riêng. |
| `tcp.send_command("speed", level=3)` | Gọi lệnh theo tên; trả ACK. |
| `tcp.execute(8, [3])` | Gọi trực tiếp OP8/P0=3; tự thêm0 cho đủ6 tham số; trả ACK. |
| `tcp.get_reader_result(sequence=1, expected_command=0x94)` | None nếu chưa có reply khớp; dictionary reply nếu thành công; lỗi thì ReaderError. Dùng sequence thực tế từ ACK. |

FC06 ghi HR256:1 lấy/duy trì,0 nhả quyền. Đọc không gia hạn lease. Lease hết sau3000 ms không được duy trì.

**Đủ19 lệnh máy và reader**

Mỗi hàng là ví dụ độc lập; thay giá trị theo nhu cầu. Trước lệnh OP cần claim;
OP20..29 cần thêm maintenance. Mọi hàm ở bảng dưới trả ACK trước, chưa phải kết quả hoàn tất.

| OP | Hàm gọi | Tham số / tác động |
|---:|---|---|
| 1 | `tcp.reset()` | Reset lỗi/STOP và homing; có thể chuyển động hai cửa. |
| 2 | `tcp.set_gate(close=1)` | 1=đóng/homing;0=mở. Cần trạng thái cho phép. |
| 3 | `tcp.set_belt(run=1)` | 1=chạy thuận;0=dừng riêng băng tải. Kiểm tra liên động. |
| 4 | `tcp.set_scan(run=1)` | 1=bắt đầu RFID;0=dừng. Từ FW1.12.2: không cần home, mọi motor phải dừng. |
| 5 | `tcp.set_mode(mode=1)` | 0=AUTO;1=SEMI. Đổi khi máy ở trạng thái phù hợp, đã homing. |
| 6 | `tcp.confirm()` | Xác nhận cho hàng qua; chỉ khi SEMI đang chờ xác nhận (state9). |
| 7 | `tcp.rescan()` | Quét lại; chỉ khi SEMI đang chờ xác nhận (state9). |
| 8 | `tcp.set_speed(level=3)` | Mức1..5; máy rảnh và reader ready; lưu tốc độ vào NVS. |
| 9 | `tcp.set_maintenance(enabled=1)` | 1=vào;0=thoát bảo trì reader; máy dừng và reader ready. |
| 20 | `tcp.get_reader_info()` | Yêu cầu thông tin reader; đọc reply để lấy thông tin/mask antenna. |
| 21 | `tcp.get_antenna_power()` | Yêu cầu đọc công suất ANT1..4; kết quả dBm trong reply. |
| 22 | `tcp.set_antenna_power(20, 20, 20, 20, ram_only=1)` | Bốn công suất0..30 dBm; ram_only=1 chỉ RAM,0 lưu reader. |
| 23 | `tcp.set_antenna_mask(mask=5, ram_only=1)` | Mask1..15;5=ANT1+ANT3;15=cả bốn. ram_only=1 RAM,0 lưu. |
| 24 | `tcp.get_rf_profile()` | Yêu cầu đọc profile legacy. |
| 25 | `tcp.set_rf_profile(encoded=128)` | Byte đã mã hóa; bit7=1 và profile thuộc allowlist. |
| 26 | `tcp.get_gen2x()` | Yêu cầu đọc profile Gen2X16-bit. |
| 27 | `tcp.set_gen2x(value=4123)` | Đặt ID thuộc allowlist Gen2X và được reader hỗ trợ. |
| 28 | `tcp.get_ex10()` | Yêu cầu đọc thông tin phiên bản Ex10. |
| 29 | `tcp.measure_return_loss(frequency_khz=915000, antenna=0)` | 840000..960000 kHz, chia hết cho100 hoặc125; antenna0..3=ANT1..4. |

Sau `get_antenna_power()`, lấy `ack["sequence"]` và gọi
`tcp.get_reader_result(ack["sequence"], expected_command=0x94)` để nhận dữ liệu.
`reply["data"]` chứa bốn công suất dBm khi đủ4 byte; None nghĩa là tiếp tục chờ
với deadline và heartbeat. Set công suất/mask cần Get đọc lại để xác nhận.
Khoảng, allowlist và điều kiện đầy đủ: [API Reference](docs/API_REFERENCE.md).

### Giao thức TCP/IP và chạy thử

| Function Code | Dùng để |
|---|---|
| FC03 | Đọc HR256 quyền của socket;HR272 SEQ;HR280 kết quả của socket. |
| FC04 | Đọc IR0..95 cho snapshot trạng thái/EPC/reply reader. |
| FC06 | HR256=1 claim/heartbeat,0 release;HR257=1 Emergency;HR258=1 STOP. |
| FC16 (0x10) | Ghi đủ8 word từ HR272: `[SEQ,OP,P0,P1,P2,P3,P4,P5]`. |

TCP và RTU dùng cùng địa chỉ/OP. TCP có MBAP, **không thêm CRC**.
SDK giữ cùng socket cho claim, heartbeat và lệnh; quyền điều khiển gắn với socket.
Socket mới bắt đầu SEQ1; release/claim trong cùng socket không reset SEQ.
Mất kết nối hoặc lease hết làm máy dừng, không tự chạy lại sau reconnect.

```console
python example/read_tcp.py --host 192.168.1.211 --port 502 --unit 1
```

Khi máy rảnh, thực hiện phiên đọc công suất antenna:

```console
python example/reader_power.py --transport tcp --host 192.168.1.211
```

Script thứ hai claim STOP/HOLD, duy trì heartbeat, kiểm tra reply và release.
Source: [read_tcp.py](example/read_tcp.py), [reader_power.py](example/reader_power.py).

<a id="mqtt"></a>
## 3. MQTT

Bo và ứng dụng đều là **MQTT client** kết nối tới broker. Cấu hình broker/root của bo
qua `http://<IP-bo>/`; mỗi bo một root riêng. Mặc định port1883, root`tunnel/01/v1`.
Firmware dùng MQTT3.1.1 qua TCP, chưa hỗ trợ TLS/WebSocket.

### Cài đặt và kết nối MQTT

```console
python -m pip install ".[mqtt]"
```

```python
import os
from tunnel_sdk import TunnelDevice

with TunnelDevice.mqtt(
    "broker.example.com", port=1883, root="factory/tunnel01/v1",
    username=os.getenv("TUNNEL_MQTT_USERNAME", ""),
    password=os.getenv("TUNNEL_MQTT_PASSWORD", ""), timeout=1.0,
) as mqtt:
    state = mqtt.get_status()
    print(state["machine_state"], state["epc_count"], state["owner_client"])
```

Thay host/root và tài khoản bằng cấu hình broker thực tế. MQTT username/password
khác mật khẩu web bo. SDK chờ subscribe sẵn sàng rồi mới gửi yêu cầu đọc.
Ví dụ này không claim. Không cần tài khoản Tuya hoặc cloud riêng của SDK.

### Các hàm MQTT cung cấp

**Kết nối, đọc và quyền điều khiển**

| Hàm gọi | Trả về / tác dụng |
|---|---|
| `mqtt.connect()` | Mở kết nối, trả instance; không claim hoặc chạy máy. |
| `mqtt.is_connected()` | bool trạng thái transport cục bộ; không phải phép kiểm tra bo đang online. |
| `mqtt.close()` | Đóng transport; không tự gửi lệnh máy hoặc release. |
| `mqtt.get_status()` | Dictionary trạng thái mới, EPC, lỗi, IP, owner; Publish `/get`, nhận `/state`. |
| `mqtt.get_epc_count()` | int số EPC duy nhất từ một lần đọc mới. |
| `mqtt.get_functions()` | Danh mục19 OP và tham số của SDK; không truy cập bo. |
| `mqtt.claim()` | ACK; lấy quyền và STOP/HOLD máy. |
| `mqtt.has_control()` | bool: đúng client này có quyền hay không; không gia hạn lease. |
| `mqtt.heartbeat()` | ACK; chủ động gọi khoảng1000 ms để giữ quyền. |
| `mqtt.release()` | ACK; owner dừng/HOLD và nhả quyền. |
| `mqtt.stop()` | ACK; STOP toàn máy, giữ EN và nhả quyền; không cần là owner. |
| `mqtt.emergency()` | ACK; dừng và nhả EN hai cửa, có thể mất lực giữ. Kiểm tra owner riêng. |
| `mqtt.send_command("speed", level=3)` | Gọi lệnh theo tên; trả ACK. |
| `mqtt.execute(8, [3])` | Gọi trực tiếp OP8/P0=3; tự thêm0 cho đủ6 tham số; trả ACK. |
| `mqtt.get_reader_result(sequence=1, expected_command=0x94)` | None nếu chưa có reply khớp; dictionary reply nếu thành công; lỗi thì ReaderError. Dùng sequence thực tế từ ACK. |

`action=claim/heartbeat/release` trên `/cmd`. Đọc không gia hạn lease. Từ firmware 1.12.3, MQTT hết lease sau 10000 ms không được duy trì (bản cũ: 3000 ms).

**Đủ19 lệnh máy và reader**

Mỗi hàng là ví dụ độc lập; thay giá trị theo nhu cầu. Trước lệnh OP cần claim;
OP20..29 cần thêm maintenance. Mọi hàm ở bảng dưới trả ACK trước, chưa phải kết quả hoàn tất.

| OP | Hàm gọi | Tham số / tác động |
|---:|---|---|
| 1 | `mqtt.reset()` | Reset lỗi/STOP và homing; có thể chuyển động hai cửa. |
| 2 | `mqtt.set_gate(close=1)` | 1=đóng/homing;0=mở. Cần trạng thái cho phép. |
| 3 | `mqtt.set_belt(run=1)` | 1=chạy thuận;0=dừng riêng băng tải. Kiểm tra liên động. |
| 4 | `mqtt.set_scan(run=1)` | 1=bắt đầu RFID;0=dừng. Từ FW1.12.2: không cần home, mọi motor phải dừng. |
| 5 | `mqtt.set_mode(mode=1)` | 0=AUTO;1=SEMI. Đổi khi máy ở trạng thái phù hợp, đã homing. |
| 6 | `mqtt.confirm()` | Xác nhận cho hàng qua; chỉ khi SEMI đang chờ xác nhận (state9). |
| 7 | `mqtt.rescan()` | Quét lại; chỉ khi SEMI đang chờ xác nhận (state9). |
| 8 | `mqtt.set_speed(level=3)` | Mức1..5; máy rảnh và reader ready; lưu tốc độ vào NVS. |
| 9 | `mqtt.set_maintenance(enabled=1)` | 1=vào;0=thoát bảo trì reader; máy dừng và reader ready. |
| 20 | `mqtt.get_reader_info()` | Yêu cầu thông tin reader; đọc reply để lấy thông tin/mask antenna. |
| 21 | `mqtt.get_antenna_power()` | Yêu cầu đọc công suất ANT1..4; kết quả dBm trong reply. |
| 22 | `mqtt.set_antenna_power(20, 20, 20, 20, ram_only=1)` | Bốn công suất0..30 dBm; ram_only=1 chỉ RAM,0 lưu reader. |
| 23 | `mqtt.set_antenna_mask(mask=5, ram_only=1)` | Mask1..15;5=ANT1+ANT3;15=cả bốn. ram_only=1 RAM,0 lưu. |
| 24 | `mqtt.get_rf_profile()` | Yêu cầu đọc profile legacy. |
| 25 | `mqtt.set_rf_profile(encoded=128)` | Byte đã mã hóa; bit7=1 và profile thuộc allowlist. |
| 26 | `mqtt.get_gen2x()` | Yêu cầu đọc profile Gen2X16-bit. |
| 27 | `mqtt.set_gen2x(value=4123)` | Đặt ID thuộc allowlist Gen2X và được reader hỗ trợ. |
| 28 | `mqtt.get_ex10()` | Yêu cầu đọc thông tin phiên bản Ex10. |
| 29 | `mqtt.measure_return_loss(frequency_khz=915000, antenna=0)` | 840000..960000 kHz, chia hết cho100 hoặc125; antenna0..3=ANT1..4. |

Sau `get_antenna_power()`, lấy `ack["sequence"]` và gọi
`mqtt.get_reader_result(ack["sequence"], expected_command=0x94)` để nhận dữ liệu.
`reply["data"]` chứa bốn công suất dBm khi đủ4 byte; None nghĩa là tiếp tục chờ
với deadline và heartbeat. Set công suất/mask cần Get đọc lại để xác nhận.
Khoảng, allowlist và điều kiện đầy đủ: [API Reference](docs/API_REFERENCE.md).

### Topic MQTT

`<root>` là root cấu hình trên bo. Chiều truyền tính từ ứng dụng tích hợp.

| Topic | Ứng dụng | QoS / retain | Chức năng |
|---|---|---|---|
| `<root>/get` | Publish | 1 / false | Hỏi snapshot; `get_status()` và `get_epc_count()` dùng topic này. |
| `<root>/state` | Subscribe | 1 / false | Nhận snapshot trả lời /get; gồm trạng thái và số EPC. |
| `<root>/cmd` | Publish | 1 / false | Claim/heartbeat/release/STOP/Emergency và execute OP. |
| `<root>/ack` | Subscribe | 1 / false | Kết quả tiếp nhận lệnh; SDK ghép theo session/epoch/client/id. |
| `<root>/availability` | Subscribe bằng MQTT client ngoài SDK | 1 / true | Online/offline và Last Will; không thay state mới. |

Subscribe đơn thuần không làm bo trả state. `get_status()` gửi /get mỗi lần gọi;
bo không tự publish mỗi200 ms. Đọc không claim và không gia hạn quyền.

### Lệnh MQTT được SDK gửi như thế nào?

Ví dụ Python `mqtt.set_speed(level=3)` được chuyển thành `action="execute"`,
`op=8`, `args=[3,0,0,0,0,0]` trên /cmd, kèm session/epoch/client/id/seen_ms.
Trước mỗi lệnh SDK lấy state mới để tạo token; không dùng thời gian máy tính.
Sau claim, SDK đọc lại state để xác nhận owner và lấy epoch mới.

`TunnelDevice` luôn dùng OP số trên wire nên hỗ trợ firmware 1.11.0 trở lên.
Đừng nhầm với API thấp cấp cũ `Mqtt.call()` dùng named wire cần firmware 1.12.0.
SDK không tự gửi heartbeat định kỳ; ứng dụng vẫn phải lập lịch: MQTT khoảng 1000 ms; TCP/RTU khoảng 500 ms.

```console
python example/read_mqtt.py --host broker.example.com --root factory/tunnel01/v1
```

Khi máy rảnh, thực hiện phiên đọc công suất antenna:

```console
python example/reader_power.py --transport mqtt --host broker.example.com --root factory/tunnel01/v1
```

Cả hai script đọc credentials từ biến môi trường TUNNEL_MQTT_USERNAME/PASSWORD.
Script thứ hai claim STOP/HOLD, duy trì heartbeat, chờ reply và release khi có thể.
Source: [read_mqtt.py](example/read_mqtt.py), [reader_power.py](example/reader_power.py).

Các [file JSON MQTT mẫu](example/README.md#json-mqtt-mẫu) cũng nằm trong `example/`:
get, state, ack, lệnh numeric và lệnh named (yêu cầu firmware 1.12+).

<a id="results"></a>
## 4. Kết quả, lỗi và tài liệu

| Loại lời gọi | Kết quả |
|---|---|
| Đọc trạng thái | Dictionary; `epc_count`, `total_reads`, `machine_state`, `fault`, `owner`,... |
| Lệnh máy/reader | ACK với `accepted`, `result`; OP có thêm `sequence`. |
| Reader result | None khi chưa khớp/đang chờ; thành công có `command,status,data,raw_reply,sequence`. |

`accepted=True` chỉ xác nhận lệnh được nhận. Reader cần reply đúng sequence;
cơ khí cần theo dõi state/fault/inputs. Chỉ một nguồn TCP/RTU/MQTT giữ quyền cùng lúc.
`UnknownOutcome` có nghĩa chưa biết lệnh đã thực hiện hay chưa; không tự gửi lại lệnh chạy.
SDK không tự claim, heartbeat, reconnect hoặc retry. `close()` không tự gửi release;
chủ động release khi kết thúc phiên.

- [API Reference: tham số, điều kiện và cách gọi tương đương](docs/API_REFERENCE.md)
- [Toàn bộ trường trạng thái và mã lỗi](docs/STATUS_AND_ERRORS.md)
- [Cấu hình và bắt đầu](docs/GETTING_STARTED.md)
- [User Guide ba giao thức / register map](docs/Tunnel_User_Guide_VI.pdf)
- [Chuyển từ API cũ](docs/MIGRATION.md)
- [Kết quả kiểm thử](docs/VALIDATION.md)

## Development

```console
python -m pip install -e ".[all]"
python -m unittest discover -s tests -v
python -m pip install build
python -m build
```

Test SDK dùng transport giả lập, không mở COM hoặc điều khiển bo thật.
Xem [tests](tests/README.md), [Changelog](CHANGELOG.md).

## Issue feedback và license

Gửi SDK/firmware version, transport, code tối thiểu và exception/ACK/state qua issue
repo private hoặc kênh bàn giao; loại password khỏi log. Phân phối private theo
thỏa thuận với chủ sở hữu: [LICENSE](LICENSE). Dependency giữ giấy phép riêng.

[MQTT timing](docs/MQTT_TIMING.md)
