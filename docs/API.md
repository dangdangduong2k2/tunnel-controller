# API chung cho ba giao thức

Modbus TCP và RTU dùng **cùng một bảng thanh ghi**, cùng Function Code và OP; khác phần đóng frame. MQTT dùng JSON và topic nhưng gọi cùng chức năng. Không phải mọi lệnh trong toàn bộ SDK ZK đều được mở ra: chỉ 19 OP tại [COMMANDS](COMMANDS.md), theo phạm vi đã chốt.

## Đọc và điều khiển

Đọc trạng thái không cần claim. Khi điều khiển: claim → xác nhận owner → gửi heartbeat khoảng 500 ms → gửi lệnh → theo dõi trạng thái/kết quả → release khi xong. Claim đưa máy về STOP/HOLD và khóa điều khiển thường trên DWIN; không tự Reset. Reset là lệnh chủ động có thể chạy homing.

Owner: 0 không có; 1 RTU; 2/3/4 ba client TCP; 5 PC legacy; 6 MQTT. Các client cùng root MQTT còn được phân biệt bằng `client` 16 ký tự hex. Một client không thể lấy quyền khi client khác đang giữ.

Lease hết sau 3000 ms không được gia hạn: dừng, cần chủ động claim/kiểm tra lại; heartbeat muộn không tự khôi phục chuyển động. Riêng giao thức PC cũ có cơ chế status heartbeat của owner để tương thích; không áp dụng cơ chế đó cho Modbus/MQTT.

STOP được phép từ bên không sở hữu. Trên Modbus ghi HR258=1 dừng và thu hồi owner; HR257=1 Emergency. Trên MQTT vẫn phải có envelope hợp lệ, token mới và dùng `action:"stop"`/`"emergency"`. STOP/lỗi giữ lực giữ; Emergency nhả EN theo cấu hình hiện tại. Phần mềm chốt dừng, không tự chạy lại khi mạng trở lại.

## Ba tầng kết quả

| Tầng | Ý nghĩa | Cần kiểm tra |
|---|---|---|
| Transport | Nhận frame hoặc broker PUBACK | Chưa chứng minh máy đã nhận/thực hiện |
| Chấp nhận lệnh | Modbus không exception + HR280=0; MQTT code/exception/result đều 0 | Chỉ bắt đầu/tiếp nhận yêu cầu |
| Hoàn thành | State machine hoặc reader reply đã cập nhật đúng sequence | Kiểm tra fault, reader status và payload |

Reader: vào maintenance (OP9=1), gửi OP20..29. Theo dõi IR20/21/22/23; IR20=1 đang chờ, 2 có reply, 3 lỗi transport. IR21 phải khớp sequence đã gửi. IR32.. chứa `[command, reader_status, payload...]`; mỗi byte dùng một thanh ghi. `reader_transport_status=0` không có nghĩa `reader_status` trong payload bằng 0. Thoát maintenance OP9=0 và release khi xong.

## Result ứng dụng (HR280 / MQTT result)

| Mã | Ý nghĩa |
|---:|---|
| 0 | Đã chấp nhận |
| 1 | Tham số không hợp lệ |
| 2 | Busy/trạng thái không cho phép hoặc reader bridge chưa nhận yêu cầu |
| 3 | Điều kiện liên động chưa đạt |
| 4 | Reader chưa sẵn sàng khi vào maintenance |
| 7 | Chức năng không hỗ trợ |
| 8 | Lưu mức tốc độ thất bại |

Không dùng mã này thay cho Modbus exception hoặc MQTT `code`. Lệnh được nhận với result khác 0 vẫn tiêu thụ sequence; đọc kết quả rồi dùng sequence mới cho một yêu cầu chủ động tiếp theo.

## Khi mất phản hồi

Không tự gửi lại Reset/chạy băng tải chỉ vì timeout. SDK ném `UnknownOutcome` khi không biết lệnh đã được thực hiện chưa; ứng dụng phải đọc trạng thái, đánh giá máy và ra quyết định mới. Không tự reconnect/claim/heartbeat trong nền. Retry network có thể biến một lệnh cũ thành tác động không mong muốn nếu bỏ qua cơ chế id/sequence.

Chi tiết: [Modbus](MODBUS.md), [MQTT](MQTT.md), [reader và antenna](READER_API.md), [trạng thái](STATES.md), [SDK](../api/README.md).
