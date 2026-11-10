# API Reference - đầy đủ chức năng

Import: `from tunnel_sdk import TunnelDevice, ProtocolError, UnknownOutcome, ReaderError`.
Tất cả ví dụ `device` là một instance đã kết nối qua TCP, RTU hoặc MQTT.
Các ví dụ lệnh độc lập, không chạy cả tài liệu như một script liên tiếp.

## Kết nối, đọc và quyền điều khiển

| Lời gọi | Tham số / trả về / tác động |
|---|---|
| `TunnelDevice.tcp(host, port=502, unit=1, timeout=1.0)` | Tạo cấu hình TCP, chưa IO |
| `TunnelDevice.rtu(serial_port, baud=19200, parity="E", stop_bits=1, unit=1, timeout=1.0)` | Tạo cấu hình RTU; parity N/E/O;8data bits |
| `TunnelDevice.mqtt(host, port=1883, username="", password="", root="tunnel/01/v1", timeout=1.0)` | Tạo cấu hình MQTT; credentials/root của broker |
| `device.connect()` | Mở transport; trả self; không claim |
| `device.is_connected()` | bool trạng thái cục bộ, không gửi ping |
| `device.close()` | Đóng transport; không gửi lệnh máy; gọi nhiều lần được |
| `device.get_status()` | Dictionary snapshot mới; xem STATUS_AND_ERRORS.md |
| `device.get_epc_count()` | int số EPC duy nhất từ một lần đọc mới |
| `device.get_functions()` | Dictionary19OP và tham số; không IO |
| `device.claim()` | ACK; lần đầu STOP/HOLD, không Reset |
| `device.has_control()` | bool đúng client này đang giữ quyền; chỉ đọc |
| `device.heartbeat()` | ACK; chủ động mỗi khoảng500ms; lease3000ms |
| `device.release()` | ACK; owner dừng/HOLD và nhả quyền |
| `device.stop()` | ACK; mọi nguồn có thể dừng/HOLD, giữ EN và nhả quyền |
| `device.emergency()` | ACK; dừng, nhả EN hai cửa, có thể mất lực giữ; kiểm tra owner riêng |
| `device.send_command(method, **params)` | Tên/params của bảng bên dưới; ACK; MQTT dùng numeric OP tương thích1.11 |
| `device.execute(op, args=())` | API số:19OP,0..6uint16, tự thêm0 cho đủ6; ACK |
| `device.get_reader_result(sequence, expected_command=None)` | Đọc một lần; None nếu pending/chưa khớp; reply dict khi thành công; ReaderError khi lỗi |

`with TunnelDevice.tcp(host) as device:` mở/đóng transport. Close không tự release.
Timeout tính bằng giây, phải dương hữu hạn; Unit1..247. Constructor không truy cập bo.
MQTT tự lấy state mới cho token của từng lệnh, nhưng không tự gửi heartbeat định kỳ.

## Kết quả lệnh

```python
ack = device.set_speed(level=3)  # Sau claim, khi máy cho phép.
if not ack["accepted"]:
    raise ProtocolError(f"Rejected: {ack}")
# accepted=True chưa phải cơ cấu/reader đã hoàn tất.
state = device.get_status()
print(state["speed_level"])
```

ACK: `accepted:bool`, `result:int`, OP có `sequence:uint16`.
MQTT thêm `code,exception,session,epoch,client,id,api_version`.
Lệnh reader luôn trả ACK trước; dữ liệu lấy bằng get_reader_result, không nằm trong ACK.
Tham số số nguyên, không dùng bool thay0/1. Các lệnh máy cần owner; reader còn cần maintenance.

## Danh mục nhanh

| OP | Hàm Python | Method chung |
|---:|---|---|
| 1 | `reset()` | `reset` |
| 2 | `set_gate(close=1)` | `gate` |
| 3 | `set_belt(run=1)` | `belt` |
| 4 | `set_scan(run=1)` | `scan` |
| 5 | `set_mode(mode=1)` | `mode` |
| 6 | `confirm()` | `confirm` |
| 7 | `rescan()` | `rescan` |
| 8 | `set_speed(level=3)` | `speed` |
| 9 | `set_maintenance(enabled=1)` | `maintenance` |
| 20 | `get_reader_info()` | `reader.info` |
| 21 | `get_antenna_power()` | `antenna.power.get` |
| 22 | `set_antenna_power(20, 20, 20, 20, ram_only=1)` | `antenna.power.set` |
| 23 | `set_antenna_mask(mask=5, ram_only=1)` | `antenna.mask.set` |
| 24 | `get_rf_profile()` | `reader.profile.get` |
| 25 | `set_rf_profile(encoded=128)` | `reader.profile.set` |
| 26 | `get_gen2x()` | `reader.gen2x.get` |
| 27 | `set_gen2x(value=4123)` | `reader.gen2x.set` |
| 28 | `get_ex10()` | `reader.ex10.get` |
| 29 | `measure_return_loss(frequency_khz=915000, antenna=0)` | `antenna.return_loss` |

## Cách gọi từng chức năng

### OP1: reset

```python
ack = device.reset()
# Hai cách tương đương:
ack = device.send_command("reset")
ack = device.execute(1, [])
```

Chọn **một** cách gọi cho một yêu cầu. Reset lỗi/STOP và khởi tạo lại chu trình; có thể bắt đầu homing/chuyển động.

Không có tham số.

Có thể homing hai cửa. Chờ fault=0 và stop_flags bit1 homed trước lệnh tiếp.


### OP2: gate

```python
ack = device.set_gate(close=1)
# Hai cách tương đương:
ack = device.send_command("gate", close=1)
ack = device.execute(2, [1])
```

Chọn **một** cách gọi cho một yêu cầu. Điều khiển cửa: 1 đóng/homing, 0 mở; còn phụ thuộc trạng thái máy.

Tham số wire: `close`: int 0..1.

1=đóng/homing, 0=mở. Cần trạng thái manual/STOPPED phù hợp; không đảo nghĩa 0/1.


### OP3: belt

```python
ack = device.set_belt(run=1)
# Hai cách tương đương:
ack = device.send_command("belt", run=1)
ack = device.execute(3, [1])
```

Chọn **một** cách gọi cho một yêu cầu. Băng tải: 0 dừng, 1 chạy thuận trong chế độ điều khiển tay.

Tham số wire: `run`: int 0..1.

0=dừng riêng băng tải, 1=chạy thuận khi liên động cho phép; xem activity_flags bit2. STOP toàn máy dùng stop().


### OP4: scan

```python
ack = device.set_scan(run=1)
# Hai cách tương đương:
ack = device.send_command("scan", run=1)
ack = device.execute(4, [1])
```

Chọn **một** cách gọi cho một yêu cầu. Inventory RFID thủ công: 0 dừng, 1 bắt đầu.

Tham số wire: `run`: int 0..1.

0=dừng,1=bắt đầu. Từ firmware1.12.2, Scan thủ công độc lập, không cần homing/cửa đóng. Claim → set_scan(1) → đọc epc_count/total_reads → set_scan(0) → release; duy trì heartbeat500ms. Không vào maintenance, không gọi Reset. Mọi motor phải đứng yên; chặn khi fault/Emergency hoặc đang ở chu trình AUTO/SEMI. Hết scan giữ STOPPED, không tự chạy AUTO. Firmware1.12.0 trở về trước vẫn yêu cầu cửa đóng. Ví dụ đầy đủ: [scan_tags.py](../example/scan_tags.py).


### OP5: mode

```python
ack = device.set_mode(mode=1)
# Hai cách tương đương:
ack = device.send_command("mode", mode=1)
ack = device.execute(5, [1])
```

Chọn **một** cách gọi cho một yêu cầu. Chọn chế độ: 0 tự động, 1 bán tự động.

Tham số wire: `mode`: int 0..1.

0=AUTO,1=SEMI. Chỉ ở state0,7 hoặc14 đã homing; không maintenance/emergency; có thể mở cửa.


### OP6: confirm

```python
ack = device.confirm()
# Hai cách tương đương:
ack = device.send_command("confirm")
ack = device.execute(6, [])
```

Chọn **một** cách gọi cho một yêu cầu. Xác nhận kết quả trong bước chờ xác nhận bán tự động.

Không có tham số.

Chỉ khi state=9 (SEMI chờ xác nhận). Mở cửa và tiếp tục chu trình.


### OP7: rescan

```python
ack = device.rescan()
# Hai cách tương đương:
ack = device.send_command("rescan")
ack = device.execute(7, [])
```

Chọn **một** cách gọi cho một yêu cầu. Yêu cầu quét lại trong bước chờ xác nhận.

Không có tham số.

Chỉ khi state=9. Theo dõi tiến trình và kết quả quét lại.


### OP8: speed

```python
ack = device.set_speed(level=3)
# Hai cách tương đương:
ack = device.send_command("speed", level=3)
ack = device.execute(8, [3])
```

Chọn **một** cách gọi cho một yêu cầu. Chọn mức tốc độ băng tải 1..5, lưu theo chính sách SpeedStore.

Tham số wire: `level`: int 1..5.

Máy rảnh/dừng, reader ready, không motion/scan/pending. Lưu NVS; xem speed_level.


### OP9: maintenance

```python
ack = device.set_maintenance(enabled=1)
# Hai cách tương đương:
ack = device.send_command("maintenance", enabled=1)
ack = device.execute(9, [1])
```

Chọn **một** cách gọi cho một yêu cầu. 0 thoát, 1 vào bảo trì để cấu hình reader; dừng chuyển động.

Tham số wire: `enabled`: int 0..1.

1=vào,0=thoát. Máy dừng, reader ready, không scan/motion/pending; xem activity_flags bit4.


### OP20: reader.info

```python
ack = device.get_reader_info()
# Hai cách tương đương:
ack = device.send_command("reader.info")
ack = device.execute(20, [])
```

Chọn **một** cách gọi cho một yêu cầu. Đọc thông tin reader qua lệnh ZK 0x21.

Không có tham số.

Reply command0x21. Khi data có ít nhất9byte, mask=data[8] (IR42). Định dạng theo reader đang tích hợp.

Điều kiện: claim + maintenance; reader ready và không pending. Kết quả: ACK → chờ reply đúng sequence.

### OP21: antenna.power.get

```python
ack = device.get_antenna_power()
# Hai cách tương đương:
ack = device.send_command("antenna.power.get")
ack = device.execute(21, [])
```

Chọn **một** cách gọi cho một yêu cầu. Đọc công suất bốn cổng antenna, ZK 0x94.

Không có tham số.

Reply command0x94: data[0:4] là ANT1..4 dBm. Phải có đủ4byte dữ liệu.

Điều kiện: claim + maintenance; reader ready và không pending. Kết quả: ACK → chờ reply đúng sequence.

### OP22: antenna.power.set

```python
ack = device.set_antenna_power(20, 20, 20, 20, ram_only=1)
# Hai cách tương đương:
ack = device.send_command("antenna.power.set", ant1_dbm=20, ant2_dbm=20, ant3_dbm=20, ant4_dbm=20, ram_only=1)
ack = device.execute(22, [20, 20, 20, 20, 1])
```

Chọn **một** cách gọi cho một yêu cầu. Đặt công suất bốn cổng; ram_only=1 chỉ RAM, 0 yêu cầu lưu reader.

Tham số wire: `ant1_dbm`: int 0..30; `ant2_dbm`: int 0..30; `ant3_dbm`: int 0..30; `ant4_dbm`: int 0..30; `ram_only`: int 0..1.

ram_only bắt buộc:1=RAM,0=yêu cầu lưu reader. Chờ reply, sau đó get_antenna_power đọc lại. Không ghi lặp mỗi vòng polling.

Điều kiện: claim + maintenance; reader ready và không pending. Kết quả: ACK → chờ reply đúng sequence.

### OP23: antenna.mask.set

```python
ack = device.set_antenna_mask(mask=5, ram_only=1)
# Hai cách tương đương:
ack = device.send_command("antenna.mask.set", mask=5, ram_only=1)
ack = device.execute(23, [5, 1])
```

Chọn **một** cách gọi cho một yêu cầu. Bit 0..3 bật antenna 1..4; ram_only=1 RAM, 0 lưu reader.

Tham số wire: `mask`: int 1..15; `ram_only`: int 0..1.

Mask1..15, bit0..3=ANT1..4;5=ANT1+ANT3. ram_only:1=RAM,0=lưu. get_reader_info đọc lại mask.

Điều kiện: claim + maintenance; reader ready và không pending. Kết quả: ACK → chờ reply đúng sequence.

### OP24: reader.profile.get

```python
ack = device.get_rf_profile()
# Hai cách tương đương:
ack = device.send_command("reader.profile.get")
ack = device.execute(24, [])
```

Chọn **một** cách gọi cho một yêu cầu. Đọc profile reader, ZK 0x7F một byte.

Không có tham số.

Reply command0x7F; profile legacy=data[0], cần ít nhất1byte.

Điều kiện: claim + maintenance; reader ready và không pending. Kết quả: ACK → chờ reply đúng sequence.

### OP25: reader.profile.set

```python
ack = device.set_rf_profile(encoded=128)
# Hai cách tương đương:
ack = device.send_command("reader.profile.set", encoded=128)
ack = device.execute(25, [128])
```

Chọn **một** cách gọi cho một yêu cầu. Gửi byte Set profile ZK đã mã hóa; bridge kiểm tra thêm mask/giá trị hợp lệ.

Tham số wire: `encoded`: int 128..255.

encoded phải có bit7=1. encoded &63 thuộc0,1,2,3,5,7,11,12,13,15,50..53. Bit6 chuyển nguyên. Dùng get_rf_profile đọc lại.

Điều kiện: claim + maintenance; reader ready và không pending. Kết quả: ACK → chờ reply đúng sequence.

### OP26: reader.gen2x.get

```python
ack = device.get_gen2x()
# Hai cách tương đương:
ack = device.send_command("reader.gen2x.get")
ack = device.execute(26, [])
```

Chọn **một** cách gọi cho một yêu cầu. Đọc cấu hình Gen2X, ZK 0x7F ba byte.

Không có tham số.

Reply command0x7F; profile=data[0]*256+data[1], cần ít nhất2byte.

Điều kiện: claim + maintenance; reader ready và không pending. Kết quả: ACK → chờ reply đúng sequence.

### OP27: reader.gen2x.set

```python
ack = device.set_gen2x(value=4123)
# Hai cách tương đương:
ack = device.send_command("reader.gen2x.set", value=4123)
ack = device.execute(27, [4123])
```

Chọn **một** cách gọi cho một yêu cầu. Gửi từ cấu hình Gen2X 16-bit; bridge kiểm tra thêm bitfield.

Tham số wire: `value`: int 0..65535.

Giá trị phải thuộc allowlist bên dưới và được reader hỗ trợ. Dùng get_gen2x đọc lại.

Điều kiện: claim + maintenance; reader ready và không pending. Kết quả: ACK → chờ reply đúng sequence.

### OP28: reader.ex10.get

```python
ack = device.get_ex10()
# Hai cách tương đương:
ack = device.send_command("reader.ex10.get")
ack = device.execute(28, [])
```

Chọn **một** cách gọi cho một yêu cầu. Đọc thông tin Ex10, ZK 0xEC.

Không có tham số.

Reply command0xEC; giữ payload nguyên theo model reader.

Điều kiện: claim + maintenance; reader ready và không pending. Kết quả: ACK → chờ reply đúng sequence.

### OP29: antenna.return_loss

```python
ack = device.measure_return_loss(frequency_khz=915000, antenna=0)
# Hai cách tương đương:
ack = device.send_command("antenna.return_loss", frequency_hi=13, frequency_lo=63032, antenna=0)
ack = device.execute(29, [13, 63032, 0])
```

Chọn **một** cách gọi cho một yêu cầu. Đo return loss; tần số 32-bit theo đơn vị/mã hóa ZK, antenna đánh số 0..3.

Tham số wire: `frequency_hi`: int 0..65535; `frequency_lo`: int 0..65535; `antenna`: int 0..3.

SDK tự chia tần số kHz thành HI/LO. 840000..960000, chia hết cho100 hoặc125; antenna0..3=ANT1..4. Reply0x91, data[0] là giá trị thô, cần ít nhất1byte.

Điều kiện: claim + maintenance; reader ready và không pending. Kết quả: ACK → chờ reply đúng sequence.

## Lấy kết quả reader

Ví dụ đủ các lệnh, có heartbeat, timeout, readback và BUSY:
[console dùng chung ba giao thức](../example/README.md).
`require_accepted(ack)` chuyển `result=2` thành `BusyError`; xem
[BUSY và vòng đời phiên](STATUS_AND_ERRORS.md#busy-khi-chưa-thể-nhận-lệnh-trong-phiên-hiện-tại).

```python
# Sau claim và set_maintenance(1); ứng dụng vẫn lập lịch heartbeat.
ack = device.get_antenna_power()
if not ack["accepted"]:
    raise ProtocolError(str(ack))
reply = device.get_reader_result(ack["sequence"], expected_command=0x94)
if reply is not None:
    if len(reply["data"]) != 4:
        raise ProtocolError("Expected four power values")
    print(reply["data"])  # ANT1, ANT2, ANT3, ANT4 (dBm)
```

Nếu None, chờ/poll tiếp với deadline và heartbeat. Xem example/reader_power.py cho
vòng chờ đầy đủ và cleanup. Reply dict gồm `sequence,command,status,data,raw_reply`;
status=0 là reader OK. `data` bỏ hai byte command/status; `raw_reply` giữ toàn bộ.
SDK kiểm tra reader_state, transport_status, sequence và reader status, cùng command
nếu đã truyền expected_command. Ứng dụng kiểm tra chiều dài payload theo chức năng.

## Allowlist Gen2X

1,3,5,7,11,12,13,15,102,103,104,120,123,124,125,126,141,146,147,148,185,
202,203,205,222,223,224,225,226,241,244,285,302,323,324,325,326,342,343,344,
345,382,4123,4124,4141,4146,4148,4185,5123,5124,5141,5146,5148,5185.

Firmware/reader còn kiểm tra điều kiện và profile thực tế; khoảng uint16 ở API
không có nghĩa mọi số đều được hỗ trợ. Xem [trạng thái/lỗi](STATUS_AND_ERRORS.md)
và User Guide. Phạm vi là toàn bộ chức năng firmware đã mở, không phải toàn bộ SDK ZK.
