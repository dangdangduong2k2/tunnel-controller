# 1.17.2 — ESP32 cập nhật DWIN và xóa thông báo tạm

Firmware register **1172**. Giữ sửa antenna, tốc độ và Scan của 1.17.0–1.17.1.

## Nạp qua MQTT / OTA

1. Tắt nguồn công suất motor/servo; giữ nguồn DC ESP32/DWIN ổn định, màn nối P1. Không đấu USB–TTL hoặc USB J5 cùng UART màn (CH340 TX và DWIN TX chung RX44).
2. Tải `TunnelESP32_v1_17_2_app.bin` lên trang OTA hiện có, chọn bo, Update. MQTT truyền lệnh; bo tải firmware qua HTTPS như trước.
3. ESP32 khởi động lại, chờ OTA xác nhận app mới hợp lệ, rồi tự kiểm tra và cập nhật DWIN qua P1 (115200/8N1). Không cần file màn riêng hoặc USB–TTL.
4. Trong lúc ghi, màn tạm về trang boot trống. Giữ nguồn, chờ màn tự khởi động lại về Home. Thành công có thông báo `DWIN Scan buttons updated and verified.`. Máy vẫn dừng, không tự chạy cơ khí.
5. Thử START → STOP → STOP: không bật scan lại. START → START không xóa EPC. Thông báo tạm phải mất cả chữ/nền sau khoảng 3,5 giây.

OTA `success` xác nhận firmware ESP32; kết quả DWIN được theo dõi riêng. Nếu màn chưa nối nên không phản hồi, nối đúng rồi khởi động lại ESP32 để thử lại. Không lặp ghi flash trong cùng lần boot.

## Phạm vi và bảo vệ

Chỉ hỗ trợ DMG10600C070_03WTC, 1024×600, GUI 0x61/OS 0x21 với đúng gói `Tunnel_HMI_DWIN_Package_v2` đã bàn giao. Kiểm tra kernel, độ phân giải, nạp RAM khi boot, CRC toàn slot13 và 12 slot còn lại. Không ghi gói không khớp.

Nhúng một block 32 KiB, slot13/block0. Chỉ hai byte khác gói v2 cũ: offset341 1→2 (START), offset373 1→3 (STOP). Ảnh/font khác giữ nguyên. Màn đã sửa 1.17.1 được kiểm tra rồi bỏ qua ghi.

Quy trình: fingerprint → trang boot → tải block vào RAM VP8000 → đọc lại so từng byte → lưu journal NVS → ghi block104 → CRC slot13 → restart DWIN/kiểm tra Home → xóa journal. Journal ở namespace `dwin-upd`, khóa `pending`, tag CRC32 font13 đích.

Mất nguồn/ACK sau lệnh ghi: giữ khóa điều khiển; boot sau phục hồi chỉ khi journal đúng target và 12 slot khác vẫn khớp. Nếu flash đã đúng nhưng chưa restart màn, restart/xác minh rồi mới xóa journal. ESP OTA phải xác nhận app trước khi ghi màn để bộ phục hồi còn boot được khi mất nguồn. Không tắt watchdog; chờ UART vẫn phục vụ watchdog.

Trong khi chờ/cập nhật hoặc chưa xác minh flash: chặn lệnh điều khiển MQTT/Modbus/HMI, OTA mới, lưu cấu hình/lịch sử. Trạng thái mạng vẫn đọc được; STOP/Emergency giữ quyền dừng. Bản này bật chế độ màn hai nút ngay từ boot: mã cũ `VP1020=1` chỉ dừng, không bật scan. Nếu màn chưa cập nhật, nút cũ sẽ không khởi động scan thay vì tiếp tục lỗi STOP bật lại. Mã mới 2=START, 3=STOP. API MQTT/Modbus không đổi.

## Thông báo bị hằn

Code cũ hết hạn chỉ tắt `toast_show`, còn text vẫn chứa thông báo. Text và nền là widget riêng. Bản mới xóa text VP2940, tắt nền rồi vẽ lại đúng trang sau khi flush biến. Thay thông báo dài bằng ngắn cũng repaint để bỏ nét ngoài vị trí chữ mới. Giữ bản nháp/editor; không tạo lệnh máy. Không đổi ảnh/font DWIN.

## Chẩn đoán

**Cập nhật kiểm chứng 2026-11-25:** người dùng đã báo màn thực tế hiện `DWIN update not complete. Check board status.` sau nạp. Chưa xác định nguyên nhân; cần đọc `dwin_update`, `dwin_stage`, `dwin_hold` từ bo. Đây là thông báo kiểm tra trạng thái bo, không phải kết luận sai baud. Chưa xác nhận cập nhật DWIN thành công trên phần cứng với bản này.

`GET /api/status` của admin (sau đăng nhập như hiện có) thêm `dwin_update`, `dwin_stage`, `dwin_hold`. Status có `waiting_ota_health`, `updating`, `updated`, `already_current`, `no_reply`, `wrong_panel`, `wrong_layout`, `io_error`, `aborted`, `journal_error`, `ram_mismatch`, `flash_mismatch`. `dwin_hold=true` nghĩa lệnh điều khiển còn khóa.

`no_reply`: kiểm tra DC/P1/TX/RX/GND và xung đột J5/TTL, rồi reboot. `wrong_layout`/`wrong_panel`: không ép ghi, xác định gói màn. Lỗi có journal: giữ motor tắt, khôi phục nguồn/giao tiếp rồi reboot để phục hồi. Nếu tiếp tục lỗi cần kiểm tra màn trực tiếp. Không hạ ESP32 trong khi cập nhật màn chưa hoàn tất; firmware trước 1.17.1 không hiểu mã 2/3 của màn mới.

## Kiểm chứng

Native engine và adapter ESP production với UART/NVS/OTA giả: đúng ảnh/already-current, sai kernel/layout, không màn, RAM lỗi, journal lỗi, mất ACK/flash CRC sai, timeout, Emergency, millis wrap, phục hồi và không retry cùng boot; chờ OTA VALID, khóa máy, watchdog. Generator xác minh đúng hai byte đổi, 12 slot giữ nguyên. Full native regression qua; sau sửa toast chạy lại HMI với kiểm tra xóa text, dài→ngắn, UART đầy và giữ editor.

**Chưa nạp/thử 1.17.2 trên phần cứng thật trong lượt này.** File là app ESP32-S3, không phải merged image và không ghi tại offset0 khi dùng USB.

## File build cuối

App 1054512 byte; SHA-256 `69e20f59342d77a130969246b70acec2b75bce5856b47e4c1cac3a1985485c9d`. Build ESP32 core3.3.8 đạt; đối chiếu source HmiUi/DwinPatch/adapter và block nhúng trong build khớp source cuối (ngoài chỉ thị #line do Arduino thêm). Chưa nạp thiết bị.
