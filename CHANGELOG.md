# Changelog

## 1.17.2 — 2026-11-25

- ESP32 tự cập nhật block nút DWIN qua P1 sau OTA health, xác minh và journal phục hồi; khóa điều khiển trong lúc cập nhật/ảnh chưa xác minh.
- Mã Scan cũ value1 chỉ dừng trong chế độ màn hai nút, không bật nếu màn chưa cập nhật; 2/3 là START/STOP.
- Xóa text và repaint cùng trang khi toast hết hạn hoặc thay dài bằng ngắn; giữ editor/draft.
- Admin status thêm `dwin_update`, `dwin_stage`, `dwin_hold`. Xem [release notes](docs/RELEASE_1_17_2.md).

## 1.17.1 — 2026-11-25

- Sửa hai nút Scan START/STOP của HMI v2 cùng gửi VP 0x1020, value 1 (toggle). Gói DWIN mới gửi value 2 cho START, value 3 cho STOP; firmware xử lý lệnh tuyệt đối. Bấm STOP nhiều lần không bật lại; bấm START khi đang scan không tạo phiên mới/xóa EPC.
- Giữ value 1 cho màn cũ một nút toggle. Vì vậy cần nạp cả ESP32 và file touch DWIN mới để sửa trên giao diện hai nút. Gói màn chỉ đổi hai byte của `font13.bin`; ảnh, font, VP và tọa độ giữ nguyên.
- Thêm regression qua parser UART/HMI, control và reader thật với I/O giả; kiểm tra độc lập command trong binary DWIN. Xem [release notes](docs/RELEASE_1_17_1.md).

## 1.17.0 — 2026-11-25

- HMI Antennas đọc cấu hình thực, sửa bật/tắt từng cổng và công suất 0–30 dBm, chọn lưu vào reader hoặc chỉ RAM. Ghi bằng 0x2F/0x3F rồi xác minh qua 0x94/0x21; không báo thành công chỉ từ ACK và không tự lặp lệnh ghi.
- Giao dịch antenna giữ độc quyền reader, chặn điều khiển khác trong lúc bận; STOP vẫn được nhận. Cấu hình không cần Home khi máy đứng yên ở trạng thái cho phép. Không thay đổi các trang Reader chưa triển khai.
- Mức tốc độ 1–5 đổi thành khoảng 95/111/133/160/200 kHz; mức 5 mới bằng mức 3 cũ. Giữ số mức trong NVS; không tự đổi 3 thành 5 khi nâng cấp.
- Native regression gồm engine antenna, adapter ESP32, thao tác HMI và xuất xung LEDC. Không nạp hoặc thử cơ khí tự động. Xem [release notes](docs/RELEASE_1_17_0.md).

## 1.15.1

- HMI: tách quyền sửa cài đặt khỏi nhãn Ready/Emergency. Cho sửa khi không có chuyển động, scan, giao dịch reader hoặc FOTA và máy ở Startup wait, Stopped, Manual, Wait sensor, Wait rearm hoặc Fault. Không cần Home và không xóa chốt dừng.
- Vẫn chặn mọi trạng thái giữa chu trình, chờ Confirm và phiên điều khiển từ xa; kiểm tra lại quyền ở Apply/Confirm và giữ nguyên đường fence trước khi ghi NVS.
- Tắt HMI_LINK_DIAG để bộ đếm UART không ghi đè toast của giao diện.
- Regression: bao phủ toàn bộ trạng thái, chỉnh Unit khi Stopped, thay đổi quyền ngay trước Confirm và giữ nguyên nhãn Stopped. Chưa kiểm chứng bản này trên bo cho tới khi có biên bản nạp riêng.

## 1.15.0

- DWIN HMI v2: nhận sự kiện giao diện trên VP0x1090 qua hàng đợi riêng (`Hmi_PopUiEvent`); nút máy, ưu tiên Emergency/Reset và chu trình không đổi.
- Module `HmiUi` (thuần logic, test native): trang, trình sửa số/IP/chữ/mật khẩu/danh sách, bản nháp và hộp thoại "Discard changes?", toast, canh phải/giữa bằng bảng độ rộng font, gửi không chặn và tự gửi lại khi UART đầy. Thay `HmiNetworkPublisher` (file và test cũ còn giữ, không còn được gọi).
- Ethernet/Modbus/MQTT sửa trên màn: kiểm tra bằng `cfg::validate`, hỏi "Apply and restart?", lưu qua đúng đường của web (fence → NVS → restart) bằng `EspConfigSubmitLocal`; không đổi mật khẩu admin. Từ chối khi máy không rảnh hoặc đang có điều khiển từ xa.
- Trang Machine: hiện và đổi mức tốc độ băng tải qua đường lệnh0x19 sẵn có.
- Lịch sử: 500 kết quả đã chốt gần nhất (số thứ tự, giờ, Auto/Semi Auto, số EPC), NVS namespace `tunnel-hist` gồm 25 khối×20 bản ghi có CRC. `Tunnel_SetFinalizeHook` gọi đúng một lần cho mỗi kết quả.
- Giờ: SNTP sau khi Ethernet có IP, UTC+7; chưa đồng bộ thì bản ghi lưu giờ0.
- `tools/generate_hmi.py` sinh `HmiLayout.h` và `contracts/hmi_fields.json` từ gói màn.
- Antennas và Reader là giai đoạn2: hiện `--`, mọi thao tác báo "Not available in this firmware yet.".
- Ghép lên nền 1.13.1 (FOTA): trong lúc cập nhật firmware màn từ chối sửa/Apply/đổi tốc độ như khi máy bận và lịch sử hoãn ghi flash; cập nhật không được cấp khi một khối lịch sử đang ghi. Bản HMI v2 trước đó (1.14.0, nền 1.12.3) không được phát hành.
- Chi tiết và giới hạn: [HMI v2](docs/HMI_V2.md).

## 1.12.0

- Chuẩn hóa layout application/control/reader/HMI/protocol/platform/config/common; tách EspRuntime và modular Python SDK.
- Thêm contract sinh version/catalog/documentation; web source duy nhất, không duy trì hai bản HTML khác nhau.
- MQTT thêm method/params và state có tên, tương thích numeric1.11; giữ trả lời theo `/get`.
- Giữ toàn bộ hành vi chu trình/STOP/arbitration/NVS; format C/C++/Python, cập nhật test harness hiện hành.
- Có README cho từng thành phần, API chung ba giao thức, build/test/nạp/rollback và giới hạn kiểm chứng.

## Mốc 1.11.0

Thêm MQTT đọc theo yêu cầu `/get`→`/state`, correlation client/id; giữ Modbus TCP/RTU và DWIN network fields. Đây là mốc regression của refactor1.12.0.
