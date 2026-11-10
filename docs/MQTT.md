# MQTT API 1.12.0

MQTT 3.1.1 qua TCP, port mặc định1883. Bo là client kết nối broker; ứng dụng cũng kết nối broker. `root` phải riêng cho từng bo, ví dụ `factory/tunnel01/v1`; mặc định `tunnel/01/v1`. Đổi broker/root/tài khoản trên web, không cần build lại. Bản này chưa có TLS/WebSocket. Xem [cấu hình](CONFIGURATION.md).

## Topic

Hướng PUBLISH/SUBSCRIBE tính từ ứng dụng của người tích hợp:

| Topic | Ứng dụng | Bo | QoS | Retain | Nội dung |
|---|---|---|---:|---|---|
| `<root>/get` | Publish | Subscribe | 1 | false | Yêu cầu snapshot, không cần claim |
| `<root>/state` | Subscribe | Publish | 1 | false | Snapshot trả lời một `/get` hợp lệ |
| `<root>/cmd` | Publish | Subscribe | 1 | false | Claim/heartbeat/lệnh máy/reader |
| `<root>/ack` | Subscribe | Publish | 1 | false | Kết quả chấp nhận `/cmd` |
| `<root>/availability` | Subscribe | Publish/LWT | 1 | true | `{"online":true}` hoặc `{"online":false}` |

Subscribe **không** gửi yêu cầu đọc tới bo. Chờ SUBACK của `/state` và `/ack` trước khi gửi. Bo không tự phát state mỗi200ms, sau claim hay sau reconnect. Ứng dụng muốn cập nhật500ms thì gửi `/get` mỗi500ms. Availability có thể là retained và chỉ báo kết nối MQTT, không chứng minh máy đang an toàn/sẵn sàng.

## Đọc trạng thái / EPC

Publish `/get`, đúng hai trường:

```json
{"client":"1111111111111111","id":1}
```

`client`: 16 ký tự hex chữ thường, riêng mỗi tiến trình. `id`: số nguyên1..4294967295; tăng theo lần đọc. Trong `/state`, chỉ nhận gói có `request_client` và `request_id` khớp yêu cầu đang chờ. State của ứng dụng khác cũng có thể xuất hiện trên cùng topic.

State gồm:

| Trường | Kiểu / ý nghĩa |
|---|---|
| firmware | Chuỗi `Tunnel-3Protocol-1.12.0` |
| api_version | 1; client bỏ qua field bổ sung không dùng |
| request_client, request_id | Phản chiếu yêu cầu đọc |
| session | 16hex; đổi khi MQTT reconnect/phiên mới |
| epoch | uint32; đổi khi fence/STOP/claim tạo ranh giới điều khiển |
| ms | uint32 thời điểm bo tạo snapshot; token cho lệnh kế tiếp |
| owner_client | Client MQTT đang sở hữu, hoặc chuỗi rỗng |
| registers | 96 số uint16, tương ứng IR0..95; giữ tương thích1.11 |
| machine_state | uint16; nghĩa tại [STATES](STATES.md) |
| fault | uint16;0 không lỗi |
| mode | 0 Auto,1 Semi |
| speed_level | 1..5 |
| epc_count | Số EPC duy nhất trong phiên scan |
| total_reads | uint32 tổng lượt đọc |
| scan_elapsed_ms | uint32 thời gian scan |
| reader_state | 0 idle,1 pending,2 reply,3 transport error |
| owner, lease_ms | Owner giao thức và thời gian còn lại |
| ip | IPv4 dạng chuỗi |

Các trường tên mới được tạo từ cùng snapshot với `registers`. Đọc không reset số EPC/lượt đọc, không claim và không gia hạn lease.

## Điều khiển và tính mới của lệnh

Lấy `/state` mới ngay trước mỗi lệnh. Lấy `session`, `epoch`, `ms` từ đó; không lấy thời gian máy tính gán vào `seen_ms`. Tuổi token tối đa1500ms. `id` lệnh tăng riêng với `id` đọc; không dùng lại cho nội dung khác.

```json
{
  "session":"0123456789abcdef",
  "epoch":1,
  "client":"1111111111111111",
  "id":1,
  "seen_ms":100,
  "action":"claim"
}
```

Các số/token trên chỉ là ví dụ; thay bằng state hiện tại. Sau ACK claim phải `/get` lại vì epoch có thể đổi. Kiểm tra `owner=6` và `owner_client` đúng client. Dùng envelope tương tự cho `heartbeat`, `release`, `stop`, `emergency`. Heartbeat khoảng500ms, lease3000ms; đọc state không thay heartbeat.

## Lệnh theo tên (mới)

Dùng `method` + `params` thay cho `action/op/args`. Không trộn hai kiểu trong một yêu cầu. Ví dụ đọc công suất, sau khi claim và vào maintenance:

```json
{
  "session":"0123456789abcdef",
  "epoch":2,
  "client":"1111111111111111",
  "id":3,
  "seen_ms":250,
  "method":"antenna.power.get",
  "params":{}
}
```

Ví dụ phần lệnh đặt công suất: `"method":"antenna.power.set","params":{"ant1_dbm":20,"ant2_dbm":20,"ant3_dbm":20,"ant4_dbm":20,"ram_only":1}`. Vẫn phải gửi đủ5 trường envelope ở trên. Tất cả tham số là số nguyên; `1/0`, không dùng JSON boolean. Tên/tham số/tác động đầy đủ tại [COMMANDS](COMMANDS.md).

## Kiểu lệnh cũ còn hỗ trợ

Cùng envelope, dùng `"action":"execute","op":21,"args":[0,0,0,0,0,0]`. `args` phải đúng6 số uint16; ô không dùng bằng0. OP/ý nghĩa giống Modbus. Cả hai kiểu được chuẩn hóa thành cùng một lệnh trước kiểm tra replay/quyền sở hữu.

## ACK, reader reply và lỗi

```json
{"api_version":1,"session":"0123456789abcdef","epoch":2,"client":"1111111111111111","id":3,"code":0,"exception":0,"result":0,"sequence":2}
```

Ghép ACK bằng bộ `session/epoch/client/id` của **yêu cầu**, không dùng epoch mới của state. `code=0,exception=0,result=0` là đã nhận. Reader chạy bất đồng bộ: `/get` tiếp theo, ghép `registers[21]` với `sequence`, kiểm tra IR20/22/23/32... như [API chung](API.md).

| code | Nghĩa |
|---:|---|
| 0 | Đã xử lý, vẫn kiểm tra exception/result |
| 1 | Yêu cầu không hợp lệ ở lớp API |
| 2 | Sai session/epoch |
| 3 | Token quá cũ |
| 4 | Retained control bị từ chối |
| 5 | Không có quyền / controller khác |
| 6 | Exception Modbus trong adapter; xem trường exception |
| 7 | Replay/collision id hoặc id không tăng |
| 8 | Bảng client logic đã đủ8 trong epoch |

JSON sai/thiếu/dư/trùng field, sai kiểu, escape, quá sâu hoặc quá dài bị parser bỏ qua, **không đảm bảo có ACK lỗi**. Gói `/get` retained hoặc bị phân mảnh cũng bị bỏ qua. Queue đọc đầy có thể làm mất reply; không làm mất lease của controller. Firmware không cung cấp mọi chức năng JSON-RPC; đây là contract JSON riêng trên giao thức MQTT chuẩn.

## Replay và mất mạng

Lặp đúng yêu cầu gần nhất trả lại kết quả đã lưu, không thực hiện lại/không gia hạn lease. Nội dung khác cùng id bị từ chối. Client giữ tối đa một lệnh chờ ACK. Khi disconnect/session đổi, bỏ token cũ, đọc lại, chủ động quyết định claim; không tự chạy lại motor. Thư viện [SDK](../api/README.md) thực hiện correlation và báo `UnknownOutcome` nếu không nhận ACK, không tự gửi lại.
