# Migration 1.11.0 → 1.12.0

## Giữ tương thích

Map Modbus/FC/OP/sequence, các VP DWIN, thuật toán cửa/băng tải/inventory, thứ tự STOP fence, lease3000ms, NVS schema1 và namespace tốc độ không đổi. MQTT `/get` mới trả snapshot theo yêu cầu như1.11.0; không bật lại state định kỳ. Envelope numeric `action/execute/op/args` tiếp tục nhận. SDK import `tunnel_api` tiếp tục dùng qua shim.

IR1 đổi1110→1120; chuỗi firmware đổi1.11.0→1.12.0. IR0 tiếp tục giữ giá trị tương thích nội bộ, không tạo thêm chức năng vận hành. Topic root không đổi tự động. Cấu hình thật nằm trong NVS, không phải file mẫu.

## Thay đổi bổ sung

- `method/params` có tên; đầy đủ19OP hiện có, không mở toàn bộ SDK ZK.
- State thêm tên trường; client phải bỏ qua field lạ, vẫn nhận `registers`96phần tử.
- SDK `call(method, **params)`; MQTT gửi kiểu mới, Modbus ánh xạ về mailbox cũ. `command(name,args)` và `execute(op,args)` vẫn dùng được.
- Sketch `src` theo component; `EspRuntime` tách khỏi lifecycle; SDK chia module.
- Contract/version/web header sinh bởi `tools/generate.py`; kiểm tra drift bằng `--check`.
- Test cũ phụ thuộc HTTP gateway không có trong package được thay bằng test SDK hiện tại; test STM32 PWM/flash cũ không áp dụng ESP32 không đưa vào suite. Thêm reader-frequency regression vào suite chung.

## Cho người đang tích hợp

Client numeric cũ không bắt buộc đổi. Client1.11 chỉ nhận đúng tập field JSON cố định cần sửa để cho phép field state bổ sung. Client dùng named API cần1.12; không gửi `method` tới firmware1.11. Không trộn `action/op/args` và `method/params` trong cùng JSON.

OP2 giữ hành vi thật: ARG0=1 đóng/homing,0 mở. Named parameter là `close`; không diễn giải1là mở. Reset vẫn có thể gây homing, không phải chỉ xóa bộ đếm.

`source_layout.json` ghi đường dẫn trước/sau cho các file kế thừa; file mới được giải thích tại README thành phần. Tài liệu Excel1.11 ở [reference](../api/reference/README.md) chỉ giữ để đối chiếu wire cũ; contract hiện hành là Markdown/JSON đi kèm bản này.
