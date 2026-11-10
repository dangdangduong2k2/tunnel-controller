# Ví dụ Python và JSON

Cài SDK trước; chạy tại thư mục gốc của bộ SDK:

```console
python example/read_tcp.py --host 192.168.1.211
python example/read_rtu.py --port COM7 --baud 19200 --parity E
python example/read_mqtt.py --host broker.example.com --root factory/tunnel01/v1
```

Ba script trên chỉ đọc. MQTT credentials lấy từ `TUNNEL_MQTT_USERNAME` và
`TUNNEL_MQTT_PASSWORD`. Thay địa chỉ, port và COM bằng cấu hình thật.

Khi máy rảnh, chủ động thực hiện phiên claim/maintenance/đọc công suất:

```console
python example/reader_power.py --transport tcp --host 192.168.1.211
python example/reader_power.py --transport rtu --serial-port COM7
python example/reader_power.py --transport mqtt --host broker.example.com --root factory/tunnel01/v1
```

Chọn một lệnh phù hợp. Claim làm máy STOP/HOLD. Script duy trì heartbeat, kiểm tra
reply, thoát maintenance/release khi có thể. Không ghi công suất RF.
`--help` không kết nối thiết bị. [Toàn bộ API](../docs/API_REFERENCE.md).

## Scan tag không cần homing (firmware 1.12.2+)

```console
python example/scan_tags.py --transport tcp --host 192.168.102.11 --seconds 8
python example/scan_tags.py --transport rtu --serial-port COM7 --baud 9600 --seconds 8
python example/scan_tags.py --transport mqtt --host broker.emqx.io --root tunnel/test/board-7c93a1/v1 --seconds 8
```

Chọn đúng giao thức và cấu hình thực tế; COM của USB-RS485 khác COM nạp ESP32.
Script claim, gửi Scan ON, giữ heartbeat, in số EPC/lượt đọc, Scan OFF, chờ dừng RF
rồi release. Không Reset, không maintenance, không lệnh motor. Dùng `scan 1` /
`scan 0` trong console cũng tương đương. Firmware chặn chuyển động/cài đặt trong
khi scan; Scan bị từ chối nếu motor đang chạy. AUTO/SEMI vẫn giữ liên động cửa.

Broker công cộng có thể quá chậm cho điều khiển: code3 là token quá1500ms, khác
BUSY. Không nới watchdog hoặc retry mù; dùng TCP nội bộ hoặc broker LAN phù hợp.
Firmware cũ chưa hỗ trợ nên script từ chối trước claim.

## Console đủ 19 lệnh cho cả ba giao thức

[device_console.py](device_console.py) giữ **một kết nối và một phiên claim** qua
nhiều lệnh. Chọn đúng một cách kết nối:

```console
python example/device_console.py --transport tcp --host 192.168.1.211
python example/device_console.py --transport rtu --serial-port COM7 --unit 1 --baud 19200 --parity E
python example/device_console.py --transport mqtt --host broker.example.com --root factory/tunnel01/v1
```

TCP mặc định port502, MQTT1883; đổi bằng `--port`. RTU mặc định8E1.
MQTT lấy tài khoản từ hai biến môi trường như các ví dụ trên. Thay IP/COM/broker
bằng cấu hình thật. Mở console **chưa claim và chưa chạy máy**.

Gõ `help` để xem cú pháp. Gõ `claim` khi chủ động muốn giữ quyền: thao tác này
STOP/HOLD. Console gửi heartbeat500ms sau claim, kể cả khi chờ nhập bàn phím hoặc
chờ reader. Đây là logic của ví dụ, SDK không tự heartbeat. Gõ `quit` hoặc Ctrl+C
để kết thúc: ví dụ ngừng heartbeat, kiểm tra quyền rồi release nếu vẫn sở hữu.
Release làm STOP/HOLD. Nếu release thất bại, báo lỗi; không giấu bằng thông báo OK.

| Lệnh console | Hàm SDK / OP | Ý nghĩa và điều kiện |
|---|---|---|
| `reset` | `reset()` / 1 | Reset/homing; có thể làm cửa chuyển động. Chỉ khi chủ động yêu cầu. |
| `gate 1` / `gate 0` | `set_gate(close)` / 2 | 1 đóng/homing, 0 mở; cần điều kiện cơ khí hợp lệ. |
| `belt 1` / `belt 0` | `set_belt(run)` / 3 | Chạy/dừng băng tải; không vượt interlock. |
| `scan 1` / `scan 0` | `set_scan(run)` / 4 | Bắt đầu/dừng RF độc lập; FW1.12.2 không cần home, mọi motor phải dừng. |
| `mode 0` / `mode 1` | `set_mode(mode)` / 5 | AUTO/SEMI; chỉ trạng thái cho phép, có thể mở cửa. |
| `confirm` | `confirm()` / 6 | Xác nhận kết quả tại state9 trong cùng phiên. |
| `rescan` | `rescan()` / 7 | Quét lại tại state9 trong cùng phiên. |
| `speed 3` | `set_speed(level)` / 8 | Tốc độ1..5, lưu trên bo; máy/reader phải rảnh. |
| `maintenance 1` / `maintenance 0` | `set_maintenance(enabled)` / 9 | Vào/thoát cấu hình reader; không vào khi đang chạy/scan. |
| `reader.info` | `get_reader_info()` / 20 | Chờ reply0x21, xuất mask antenna và raw payload. |
| `antenna.power.get` | `get_antenna_power()` / 21 | Chờ reply0x94, xuất dBm ANT1..4. |
| `antenna.power.set 20 21 22 23 1` | `set_antenna_power(..., ram_only=1)` / 22 | Đặt bốn công suất0..30dBm; chờ Set rồi Get kiểm tra. |
| `antenna.mask.set 5 1` | `set_antenna_mask(5, ram_only=1)` / 23 | Mask1..15; 5=ANT1+ANT3. Chờ Set rồi đọc info kiểm tra. |
| `reader.profile.get` | `get_rf_profile()` / 24 | Đọc profile legacy. |
| `reader.profile.set 0x8D` | `set_rf_profile(0x8D)` / 25 | Profile13, bit7 Set; chờ Set rồi Get so sánh6bit profile. |
| `reader.gen2x.get` | `get_gen2x()` / 26 | Đọc ID Gen2X16bit. |
| `reader.gen2x.set 4123` | `set_gen2x(4123)` / 27 | Set ID thuộc allowlist; chờ Set rồi Get kiểm tra. |
| `reader.ex10.get` | `get_ex10()` / 28 | Chờ reply0xEC, giữ raw payload theo model reader. |
| `antenna.return_loss 915000 0` | `measure_return_loss(915000, 0)` / 29 | kHz, antenna0..3=ANT1..4; chờ0x91, xuất giá trị thô. |

OP20..29 cần `claim` và `maintenance 1`, reader ready, không inventory/pending.
`ram_only=1` chỉ RAM, `0` ghi bền; luôn phải nhập rõ tham số này. Profile phải
được model reader hỗ trợ; xem allowlist ở [API Reference](../docs/API_REFERENCE.md).
Không chạy tất cả lệnh trong bảng như một chuỗi tự động: mỗi lệnh có điều kiện riêng.

| Lệnh khác | Tác dụng |
|---|---|
| `status` | Snapshot mới: máy, inputs, IP, fault, owner, reader, số EPC và lượt đọc. |
| `epc` | Đọc mới số EPC duy nhất và tổng lượt đọc trong cùng snapshot. |
| `functions` | Catalog19OP/tham số của SDK. |
| `connected` | Trạng thái kết nối cục bộ; không chứng minh bo đang phản hồi. |
| `owner` | Kiểm tra chính client này có giữ quyền không. |
| `claim` | Giữ quyền và STOP/HOLD; bắt đầu heartbeat của console. |
| `heartbeat` | Gia hạn ngay; từ chối nếu không còn claim cục bộ/quyền thực tế. |
| `release` | Trả quyền, STOP/HOLD; tắt gia hạn cho phiên đó. |
| `stop` | STOP/HOLD toàn máy, không cần claim; trả quyền. |
| `emergency` | Emergency, nhả EN; không cần claim. Có thể còn owner; console ngừng gia hạn. |
| `quit` | Kết thúc, release nếu còn sở hữu, đóng transport. |

### Đọc và cấu hình reader

Trong console, ví dụ một phiên đọc thông số đầy đủ:

```text
claim
maintenance 1
reader.info
antenna.power.get
reader.profile.get
reader.gen2x.get
reader.ex10.get
antenna.return_loss 915000 0
maintenance 0
release
quit
```

Để thử ghi, chọn đúng lệnh Set trong bảng, thực hiện trong cùng maintenance.
Output chứa `ack`, `reply`, `decoded`; Set có thêm `readback` và `verified=true`
sau khi đối chiếu. Không coi ACK là đã ghi thành công. Readback ngay chỉ xác nhận
giá trị hiện tại; kiểm chứng lưu bền cần khởi động lại có chủ đích rồi Get lại.

Chạy một chuỗi được chỉ định trong cùng phiên (thay phần kết nối bằng RTU/MQTT
nếu cần; dừng ở lỗi đầu tiên và release khi thoát):

```console
python example/device_console.py --transport tcp --host 192.168.1.211 --command claim --command "maintenance 1" --command "antenna.power.set 20 21 22 23 1" --command "maintenance 0" --command release
```

Mọi cú pháp/range của chuỗi được kiểm tra trước khi mở kết nối. Profile allowlist
và điều kiện máy vẫn do firmware/reader quyết định. `--command status --command epc`
chỉ đọc, không claim. Chuỗi chuyển động cần operator theo dõi completion; console
không tự đoán thời gian cửa/băng tải đã chạy xong.

### Chu trình SEMI, Confirm và Rescan

Giữ nguyên console đã claim. Chủ động `reset`/homing nếu cần, kiểm tra `status`
đến khi homing xong, chọn `mode 1` ở trạng thái cho phép. Duy trì cùng console
qua chu trình cảm biến; khi `machine_state=9`, chọn `confirm` hoặc `rescan`.
Không mở script mới để claim giữa chu trình: claim mới STOP/HOLD, có thể mất
điều kiện xác nhận. Sau Rescan tiếp tục đọc `status`/`epc`, đợi state9 lại.

### Khi bo bận, mất quyền hoặc không nhận được kết quả

- **BUSY trong console tương tác:** báo `BUSY`, giữ phiên/heartbeat, không tự
  STOP/release/claim/retry. `status` và `epc` vẫn đọc được. Đợi kết thúc chu trình
  hoặc chủ động dừng bằng lệnh phù hợp; chỉ gửi yêu cầu mới khi đủ điều kiện.
- **Cài đặt khi scan đang chạy:** bo từ chối vào maintenance/cài reader; kết thúc
  scan, đợi reader hết pending, rồi mới vào maintenance. Có thể giữ nguyên claim.
- **Reader pending:** mỗi OP được ghép đúng sequence/command, chờ tối đa3giây.
  Set thành công mới gửi Get đọc lại. Không gửi tiếp Set khi chưa biết kết quả.
- **Mất quyền/heartbeat:** dừng gia hạn và khóa lệnh tiếp theo; không tự claim lại.
  Vẫn cho đọc trạng thái và STOP/Emergency. Chẩn đoán rồi mở phiên mới có chủ đích.
- **UnknownOutcome:** thoát console/chuỗi, thử trả quyền nếu còn sở hữu; không
  tự gửi lại lệnh. Mất ACK không đồng nghĩa lệnh chưa chạy.
- **Readback sai / reader lỗi:** báo lỗi kèm dữ liệu; không tự ghi lại. Reader
  status và transport status khác với BUSY; xem bảng mã.

Wire Modbus dùng exception06 hoặc HR280/result2; MQTT dùng code/exception/result
tương ứng. Chi tiết và cách bắt `BusyError` nằm trong
[trạng thái và lỗi](../docs/STATUS_AND_ERRORS.md#busy-khi-chưa-thể-nhận-lệnh-trong-phiên-hiện-tại).

## JSON MQTT mẫu

| File | Nội dung |
|---|---|
| [get.example.json](get.example.json) | Yêu cầu đọc `/get`, dùng từ firmware 1.11.0. |
| [state.example.json](state.example.json) | Snapshot `/state`; bản 1.12 có thêm các trường tên. |
| [ack.example.json](ack.example.json) | Kết quả nhận lệnh `/ack`. |
| [commands.example.json](commands.example.json) | Payload lệnh numeric OP/args, dùng từ firmware 1.11.0. |
| [named.example.json](named.example.json) | Payload method/params; chỉ dùng với firmware 1.12.0 trở lên. |

Token, epoch và ms trong các file là dữ liệu minh họa. Subscribe state/ack trước,
rồi gửi `/get` để lấy token mới; tăng id cho từng yêu cầu mới, đặt retain=false.
Chỉ publish object payload của lệnh cần dùng, không gửi cả danh sách ví dụ.
Đọc/cài antenna cần claim và maintenance; theo dõi reply đúng sequence, duy trì
heartbeat trong lúc chờ. Thư mục này chứa chung script chạy được và JSON tham khảo.
