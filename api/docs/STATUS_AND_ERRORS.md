# Trạng thái và lỗi

`get_status()` trả dictionary. Tất cả số đếm là số nguyên; IP là chuỗi;
inputs là dictionary bool; `registers` là mảng96uint16 tương ứng IR0..95.

| Key | Ý nghĩa |
|---|---|
| firmware | 1102=1.10.2;1110=1.11.0;1120=1.12.0 |
| machine_state / state | Mã trạng thái máy |
| fault | 0=không lỗi |
| mode | 0=AUTO,1=SEMI |
| inputs | IN1..IN7: mức GPIO, không tự diễn giải cơ khí |
| activity_flags | Bit0 cửa1,1 cửa2,2 belt,3 scan,4 maintenance,5 EN released |
| stop_reason | Nguyên nhân dừng; bảng User Guide |
| stop_flags | Bit0 latch,1 homed,2 released,3 remote supervision,4 HMI locked |
| speed_level / speed | 1..5 |
| epc_count / unique_epcs | Số EPC duy nhất; không có danh sách từng EPC |
| total_reads | Tổng lượt đọc uint32 |
| scan_elapsed_ms | Thời gian scan uint32 ms |
| reader_flags | Bit0 ready,1 inventory,2 pending; byte cao scan result |
| scan_result | 0 idle,1 đang chạy,2 OK,3 mất transport,4 không reply,5 lỗi reader,6 lỗi giao thức,7 đầy EPC,8 thiếu antenna |
| owner | 0 none,1 RTU,2..4 TCP,5 PC legacy,6 MQTT |
| lease_ms | Thời gian quyền còn lại; đọc không renew |
| last_sequence / last_result | Lệnh gần nhất toàn máy, có thể của nguồn khác |
| reader_state | 0 idle,1 pending,2 reply,3 transport error |
| reader_sequence | Sequence để ghép ACK reader |
| reader_transport_status | 0 OK,4 lost,5 malformed,6 timeout |
| reader_reply | Byte array [command,status,payload...], tối đa64byte |
| ethernet_flags | Bit0 init,1 link,2 có IP |
| ip | IPv4 thực tế |
| rtu_ready | bool |
| uptime_ms | Thời gian bo từ khởi động, uint32 |
| registers | Snapshot thô IR0..95; không gồm IR96..99 |
| map_version | Key tương thích cũ, ứng dụng không cần dùng |
| session / epoch / owner_client | Có thêm khi dùng MQTT |

## ACK

Các hàm lệnh trả `accepted: bool`, `result: int`; OP có thêm `sequence:uint16`.
MQTT còn có `api_version,session,epoch,client,id,code,exception`.
`accepted=True` chưa chứng minh hoàn tất. Reader: `get_reader_result(ack["sequence"])`;
motor: theo dõi machine_state/fault/inputs/activity_flags.

Result:0 accepted;1 tham số;2 busy/trạng thái/bridge;3 interlock;4 reader chưa ready;
7 không hỗ trợ;8 lưu tốc độ lỗi. Result khác0 vẫn có thể tiêu thụ sequence.
SDK tự đọc HR272 trước OP; không tự retry sau mất ACK.

## Exceptions

| Exception | Cách xử lý |
|---|---|
| ValueError | Sửa tên/tham số trước khi gọi lại |
| BusyError (ProtocolError) | Lệnh bị từ chối do trạng thái/quyền/reader; đọc trạng thái, không tự retry/claim/STOP |
| ProtocolError | Kiểm tra response, quyền, token hoặc Modbus exception |
| UnknownOutcome (ProtocolError) | Có thể lệnh đã tới bo; không tự gửi lại lệnh chạy; đọc và đánh giá trạng thái |
| ReaderError (ProtocolError) | Reply đúng sequence nhưng transport/status/payload lỗi |
| OSError / TimeoutError | Lỗi mở transport; kiểm tra IP/COM/broker |

Modbus exception:01 function,02 address,03 value/sequence,04 backend,06 busy/not owner,
TCP0B wrong unit. MQTT code:0 xử lý;1 invalid;2 session/epoch;3 stale;4 retained;
5 owner;6 Modbus exception;7 replay;8 client table full. JSON lỗi có thể không ACK.
Không tự reconnect/claim/run sau lỗi. Ghi nhận lỗi và trạng thái máy trước quyết định mới.

## BUSY khi chưa thể nhận lệnh trong phiên hiện tại

Bo đã gửi phản hồi từ chối; không chỉ chặn ở phần mềm máy tính. Giữ mã wire hiện
có để PLC/HMI cũ tiếp tục dùng được:

| Trường hợp | TCP / RTU | MQTT `/ack` |
|---|---|---|
| Đổi tốc độ khi đang chạy; vào maintenance khi đang scan/chạy; lệnh máy sai trạng thái | FC16 được trả lời, sau đó đọc HR280: `2` | `code=0, exception=0, result=2` |
| Có reader transaction đang chờ, gửi OP mới | Exception `06`, PDU `90 06` | `code=6, exception=6` |
| Nguồn khác đang giữ quyền, gửi claim | Exception `06`, PDU `86 06` | `code=5` |
| Không sở hữu quyền nhưng gửi OP | Exception `06` | `code=5` |

**Result 2 là BUSY/trạng thái/bridge từ chối**, không bảo đảm chỉ cần chờ là thành
công. Ví dụ bridge cũ cũng trả 2 nếu profile ngoài allowlist. Kiểm tra tham số và
state trước khi thử lại. SDK không bịa thêm lý do mà firmware chưa cung cấp.

`BUSY` không phải lỗi mạng, không phải lệnh đang xếp hàng. Lệnh bị từ chối không
được thực thi tự động khi hết bận. Lệnh mới sau khi điều kiện đã phù hợp phải dùng
sequence/id mới; lặp request cũ có thể chỉ trả lại kết quả đã lưu. ACK `code=0`
hoặc FC16 thành công **chưa đủ**: luôn kiểm tra cả `result`.

Phân biệt **chu trình scan/giao dịch reader** với **phiên giữ quyền claim**:
phải kết thúc scan/giao dịch xung đột trước khi cài đặt, nhưng có thể giữ cùng
claim, heartbeat và vào maintenance khi máy đã rảnh. Không bắt buộc release rồi
claim lại; claim mới có thể STOP/HOLD chu trình. Confirm/Rescan ở state9 cần cùng
phiên đang chạy. Đọc state/EPC và heartbeat vẫn dùng được khi BUSY; STOP/Emergency
có đường xử lý riêng, không bị chặn bởi reader pending.

```python
from tunnel_sdk import BusyError, require_accepted

# Trong phiên đã claim; việc lập lịch heartbeat thuộc ứng dụng.
try:
    ack = require_accepted(device.set_speed(3))
except BusyError as exc:
    print("BUSY", exc.result, exc.exception, exc.code)
    print(device.get_status())
    # Báo cho operator/caller. Không tự STOP, release, claim hoặc retry.
```

Không gọi `require_accepted` thì lệnh trả `accepted=False, result=2` như SDK cũ.
Exception06 và MQTT code5 tự raise `BusyError`. `result/exception/code` trên lỗi
là mã nhận được; trường không có dùng `None`. BUSY chỉ là phản hồi theo yêu cầu,
không thêm topic MQTT tự publish.
