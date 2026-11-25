# Firmware 1.17.0 — antenna HMI và thang tốc độ

Ngày: 2026-11-25. Mã firmware tại Input Register 1: **1170**.

## File nạp

`TunnelESP32_v1_17_0_app.bin` là **app image ESP32-S3**, dùng cho OTA hoặc nạp USB vào phân vùng app đã xác minh của bo Tunnel hiện tại. Không phải ảnh flash tổng và không ghi tại offset 0. Partition/core/FQBN không đổi so với 1.16.4. Build không mở COM và không nạp bo.

Build ngày 2026-11-25 đạt: file **1.016.704 byte**, target `tunnel-esp32s3-v1`, identity **1170**. SHA-256: `80534acb49613219c4ca34be8ddb1aaeb3a718bfe4b7559de73829e4812a9633`. Arduino báo code 1.016.551 byte / 3.342.336 byte và biến toàn cục 50.696 byte / 327.680 byte. `dist/TunnelESP32_v1_17_0_native_tests.txt` lưu kết quả toàn bộ native suite; project/codegen và HMI layout đối chiếu gói màn cũng đạt.

Giữ cấu hình mạng, MQTT, Modbus, lịch sử và số mức tốc độ trong NVS. Không cần xóa cấu hình. OTA giữ trust/key của bản đang dùng trong source; sửa RAM EPC của 1.16.4 được giữ lại. Xem [build và nạp](BUILD_TEST.md).

## Antennas trên HMI

Nguyên nhân: trang Antennas trong firmware cũ chỉ có giao diện và thông báo `Not available in this firmware yet.`, chưa nối với backend reader.

1. Để máy đứng yên, dừng scan và nhả phiên điều khiển MQTT/Modbus. Không cần Home để chỉnh antenna.
2. Mở **Settings → Antennas**, chờ đọc reader. Giá trị chưa đọc được hiện `--`, không cho ghi từ dữ liệu giả định.
3. Chọn On/Off cho ANT1–ANT4 (ít nhất một cổng bật). Chỉnh từng công suất bằng nút +/- hoặc chạm số để nhập **0–30 dBm**.
4. Chọn lưu vào reader nếu muốn giữ qua mất nguồn; chọn RAM nếu chỉ thử trong lần cấp nguồn hiện tại.
5. Bấm **Apply changes → Confirm**. Bo gửi SetPower `0x2F`, SetMask `0x3F`, rồi GetPower `0x94` và GetInfo `0x21`.
6. Chỉ khi đọc lại khớp cả bốn công suất và mask mới hiện `Antenna settings applied and verified.`. Rời trang rồi mở lại sẽ đọc trực tiếp reader lần nữa.

Trong lúc xử lý, firmware chặn các lệnh điều khiển cạnh tranh và không cho OTA/ghi cấu hình/lịch sử bắt đầu. STOP/Emergency vẫn có hiệu lực. Khi bị ngắt, không gửi tiếp các lệnh ghi; giữ reader đến khi lệnh đang bay kết thúc hoặc timeout. Không tự retry lệnh ghi.

Hai lệnh ghi vào reader không tạo thành một giao dịch nguyên tử: nếu một lệnh thành công rồi mất kết nối, cấu hình có thể đã đổi một phần. Firmware không báo Saved; mở lại Antennas để đọc trạng thái thật trước khi thử lại. Readback tức thời xác minh cấu hình hiện hành; việc lưu bền còn cần thử tắt/bật nguồn reader thực tế.

Dùng cùng gói màn **Tunnel_HMI_DWIN_Package_v2**, không đổi VP/event hoặc ảnh màn. Các trang **Reader/profile/return-loss/maintenance** ngoài Antennas vẫn chưa triển khai; không coi đây là bổ sung tất cả chức năng ZK.

## Tốc độ băng tải

Theo phản hồi vận hành, mức 1–3 cũ chạy được, mức 4–5 đứng yên. Code cũ yêu cầu 400/800 kHz ở mức 4/5. Theo yêu cầu người dùng, thang mới giới hạn tại tần số của mức 3 cũ; chưa kết luận 200 kHz là giới hạn điện chính thức của driver khi chưa biết model hoặc đo xung thực tế.

| Mức mới | Tần số xung yêu cầu | Chu kỳ | So với bản cũ |
|---:|---:|---:|---|
| 1 | 95.238 kHz | 10.50 µs | Mức 1 cũ |
| 2 | 111.111 kHz | 9.00 µs | Mức trung gian |
| 3 | 133.333 kHz | 7.50 µs | Mức 2 cũ |
| 4 | 160.000 kHz | 6.25 µs | Mức trung gian |
| 5 | 200.000 kHz | 5.00 µs | **Mức 3 cũ** |

Đây là tần số xung điều khiển, không phải tốc độ m/s đã hiệu chuẩn. HMI, MQTT và Modbus cùng dùng thang này. Chiều chạy, ngưỡng cảm biến và điều kiện dừng không đổi. Không cho đổi tốc độ giữa lúc băng tải đang chạy.

**Lưu ý nâng/hạ phiên bản:** số mức đã lưu vẫn giữ nguyên. Bo lưu mức 3 sẽ dùng 133.333 kHz sau nâng cấp; chọn mức 5 để chạy bằng mức 3 cũ. Nếu hạ về firmware cũ khi đang lưu mức 5, mức đó lại tương ứng 800 kHz; cần chỉnh lại mức phù hợp trước khi chạy.

## Kiểm chứng và test tại máy

Đã qua toàn bộ native regression: engine antenna (readback, RAM/persistent, sai phản hồi, timeout, mismatch, STOP, không retry ghi); adapter ESP32 (độc quyền, không Home, chặn OTA/flash/remote); HMI (nhập số, +/- giới hạn, mask, xác nhận, BUSY, đọc lại); control/LEDC (5 mức tăng dần, có duty, mức 5 = 200 kHz); MQTT/Modbus/reader, lịch sử và các cơ chế dừng hiện có. Build ESP32 theo Arduino core 3.3.8.

**Chưa nạp bo hoặc xác nhận bằng reader/màn/băng tải thật trong lượt phát hành này.** Sau nạp cần kiểm tra:

| Ca | Thao tác | Kết quả cần đạt |
|---|---|---|
| A1 | Mở Antennas, đối chiếu reader/app ZK | Đúng mask và bốn mức công suất |
| A2 | Đổi một mức công suất, bật/tắt cổng, Apply | Báo verified; mở lại đọc đúng |
| A3 | Lưu persistent, tắt/bật nguồn reader | Cấu hình mới vẫn giữ |
| A4 | Đổi RAM, tắt/bật nguồn reader | Trở lại cấu hình đã lưu trước đó |
| A5 | Ngắt reader khi Apply | Không báo thành công giả; nối lại và mở trang đọc lại được |
| A6 | Máy chạy/scan hoặc remote đang giữ quyền | Không cho sửa; nhả quyền/dừng rồi thử lại |
| A7 | STOP khi đang Apply | Không khởi động motor/RF hoặc tự gửi lại lệnh ghi |
| S1 | Chọn từng mức 1–5 khi đứng yên, sau đó chạy/dừng | Cả 5 mức chạy, tăng dần, mức 5 bằng mức 3 cũ |
| S2 | Lưu mức, khởi động lại rồi đọc trên HMI/Modbus/MQTT | Giữ đúng số mức |
| S3 | Trong chuyển động thử đổi mức; thử STOP | Từ chối đổi giữa lúc chạy; STOP vẫn dừng |

User Guide PDF VI 1.1 vẫn mô tả firmware 1.16.4. Tài liệu này bổ sung thay đổi Antennas và tốc độ cho 1.17.0.
