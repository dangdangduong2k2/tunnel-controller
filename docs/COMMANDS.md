# Danh sách lệnh chung

Sinh từ [operations.json](../contracts/operations.json). MQTT và Modbus gọi cùng backend.

Tham số theo thứ tự ARG0..ARG5; các ô còn lại phải bằng 0. MQTT named API yêu cầu đủ các trường trong `params`, số nguyên (không dùng `true/false`). Mọi lệnh cần quyền điều khiển; các lệnh reader còn cần maintenance và reader rảnh. ACK nhận lệnh chưa đồng nghĩa chuyển động/reader đã hoàn tất.

| OP | Method | Tham số bắt buộc, khoảng | Tác động |
|---:|---|---|---|
| 1 | `reset` | `{}` / không có | Reset lỗi/STOP và khởi tạo lại chu trình; có thể bắt đầu homing/chuyển động. |
| 2 | `gate` | `close`: 0..1 | Điều khiển cửa: 1 đóng/homing, 0 mở; còn phụ thuộc trạng thái máy. |
| 3 | `belt` | `run`: 0..1 | Băng tải: 0 dừng, 1 chạy thuận trong chế độ điều khiển tay. |
| 4 | `scan` | `run`: 0..1 | Scan RFID độc lập: 0 dừng, 1 bắt đầu. Từ firmware1.12.1 không cần homing/cửa đóng; yêu cầu mọi motor dừng, không maintenance/Emergency. |
| 5 | `mode` | `mode`: 0..1 | Chọn chế độ: 0 tự động, 1 bán tự động. |
| 6 | `confirm` | `{}` / không có | Xác nhận kết quả trong bước chờ xác nhận bán tự động. |
| 7 | `rescan` | `{}` / không có | Yêu cầu quét lại trong bước chờ xác nhận. |
| 8 | `speed` | `level`: 1..5 | Chọn mức tốc độ băng tải 1..5, lưu theo chính sách SpeedStore. |
| 9 | `maintenance` | `enabled`: 0..1 | 0 thoát, 1 vào bảo trì để cấu hình reader; dừng chuyển động. |
| 20 | `reader.info` | `{}` / không có | Đọc thông tin reader qua lệnh ZK 0x21. |
| 21 | `antenna.power.get` | `{}` / không có | Đọc công suất bốn cổng antenna, ZK 0x94. |
| 22 | `antenna.power.set` | `ant1_dbm`: 0..30; `ant2_dbm`: 0..30; `ant3_dbm`: 0..30; `ant4_dbm`: 0..30; `ram_only`: 0..1 | Đặt công suất bốn cổng; ram_only=1 chỉ RAM, 0 yêu cầu lưu reader. |
| 23 | `antenna.mask.set` | `mask`: 1..15; `ram_only`: 0..1 | Bit 0..3 bật antenna 1..4; ram_only=1 RAM, 0 lưu reader. |
| 24 | `reader.profile.get` | `{}` / không có | Đọc profile reader, ZK 0x7F một byte. |
| 25 | `reader.profile.set` | `encoded`: 128..255 | Gửi byte Set profile ZK đã mã hóa; bridge kiểm tra thêm mask/giá trị hợp lệ. |
| 26 | `reader.gen2x.get` | `{}` / không có | Đọc cấu hình Gen2X, ZK 0x7F ba byte. |
| 27 | `reader.gen2x.set` | `value`: 0..65535 | Gửi từ cấu hình Gen2X 16-bit; bridge kiểm tra thêm bitfield. |
| 28 | `reader.ex10.get` | `{}` / không có | Đọc thông tin Ex10, ZK 0xEC. |
| 29 | `antenna.return_loss` | `frequency_hi`: 0..65535; `frequency_lo`: 0..65535; `antenna`: 0..3 | Đo return loss; tần số 32-bit theo đơn vị/mã hóa ZK, antenna đánh số 0..3. |
