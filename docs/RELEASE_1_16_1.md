# 1.16.1 — OTA nâng cấp, nạp lại và hạ phiên bản

Theo yêu cầu, đã bỏ điều kiện `target_version > running_version` ở cả firmware,
SDK, HTTP API và giao diện. Người dùng chọn file nào thì hệ thống gửi đúng phiên
bản nhúng trong file đó. Hộp xác nhận hiện hành ghi rõ “Cập nhật”, “Nạp lại” hoặc
“Hạ phiên bản”, cùng phiên bản trước/sau; không thêm bước xin quyền riêng.

Chữ ký, checksum/SHA-256, target ESP32-S3, phiên bản khớp file, URL tin cậy,
BUSY, chống replay và chống thực thi trùng vẫn giữ nguyên. Mỗi lần nạp lại là
một job mới do người dùng xác nhận; gửi lặp cùng job không chạy lần thứ hai.

Giới hạn tương thích: các binary cũ trước 1.16.0 không có identity OTA mới,
vẫn cần nạp bằng USB. Không đổi số phiên bản trong lệnh để vượt kiểm tra file.
Bo đang chạy 1.16.0 có thể nâng lên 1.16.1 qua OTA; từ bản 1.16.1 mới hỗ trợ
đầy đủ nạp lại/hạ phiên bản. Nếu hạ về 1.16.0 thì chính sách của 1.16.0 sẽ áp
dụng trở lại. Khả năng đọc cấu hình do phiên bản mới ghi cần được đánh giá khi
phát hành từng bản; tính năng này không tự chuyển đổi schema NVS của tương lai.

Kiểm thử phần mềm đã chạy ngày 16/11/2026:

| Lớp | Kết quả |
| --- | --- |
| Native protocol | Cùng/cũ/mới đều hợp lệ; version=0, stale, replay, sai target và URL vẫn bị chặn |
| SDK | Nạp lại/hạ phiên bản gửi đúng một lệnh có chữ ký, QoS 1, retain=false |
| HTTP API | Chấp nhận release cùng phiên bản và thấp hơn; bảo vệ job đang chạy vẫn giữ |
| Trình duyệt | Nút bật cho cả ba trường hợp, đúng nhãn và phiên bản; Cancel không tạo job |
| Toàn bộ Python OTA | 31 test PASS (18 portal, 7 SDK, 6 HTTPS) |

Firmware: `dist/TunnelESP32_v1_16_1_app.bin`. Metadata và SHA-256 nằm trong
`dist/TunnelESP32_v1_16_1_app.release.json`. Kết quả nạp/kiểm tra phần cứng,+nếu thực hiện, nằm riêng trong `dist/TunnelESP32_v1_16_1_verification.json`;
test giả lập không chứng minh OTA/rollback trên bo thật.

Kết quả trên bo `b43a45cf4b14`, COM5 ngày 16/11/2026: đã nạp 1.16.1 qua USB,
verify flash PASS, bootloader/partition/NVS giữ nguyên trong lần nạp và MQTT
báo firmware 1161. Đã gửi đúng một job OTA nạp lại 1161: bo báo `accepted`,
sau đó `failed / https_connect` khi truy cập HTTPS cục bộ. Điều kiện cùng
phiên bản đã được xác minh trên bo; tải/cài đặt OTA hoàn chỉnh chưa thành công.
Bo báo IP `192.168.0.122`; máy phục vụ HTTPS ở `192.168.1.18`. CA khớp cấu hình,
nhưng chưa xác định đầy đủ nguyên nhân lỗi kết nối. Không tự gửi lại job.
