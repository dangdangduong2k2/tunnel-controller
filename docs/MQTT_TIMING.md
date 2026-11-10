# MQTT: giá trị cố định từ firmware 1.12.3 / SDK 0.1.4

| Giới hạn | Giá trị |
|---|---:|
| Tuổi token tối đa do bo kiểm tra | 5000 ms |
| Lease của chủ MQTT | 10000 ms |
| Lease TCP/RTU/legacy | 3000 ms |
| Khoảng nghỉ giữa lần heartbeat của console MQTT | 1000 ms |
| Timeout mặc định mỗi request MQTT của SDK | 5000 ms |
| Cache state cho kiểm tra owner và token | dưới 1000 ms từ khi nhận |
| Giới hạn thêm để dùng cache | dưới 3000 ms từ lúc gửi GET |

SDK chỉ dùng cache với firmware >=1123. `get_status()` vẫn luôn gửi GET mới.
Claim vẫn đọc lại owner và epoch sau ACK; các bước ngay sau đó có thể dùng
cache vừa xác nhận. Khi cache hết hạn, SDK đọc lại; không tự tính hay sửa seen_ms.
Heartbeat là lệnh HEARTBEAT, không phải claim; hết lease không tự lấy lại quyền.

Session/epoch, ID tăng, kiểm tra retained và chống thực thi trùng giữ nguyên.
Lệnh trùng không gia hạn lease. Đọc trạng thái không gia hạn lease.
ACK lỗi, release/STOP/Emergency hoặc kết quả không rõ hủy cache phù hợp;
không có retry lệnh tự động. Firmware cũ vẫn đọc token trước mỗi lệnh.

Nếu chưa phát hiện ngắt kết nối, mất heartbeat MQTT có thể mất tới 10 giây
mới thu hồi quyền và dừng. Ngắt broker được phát hiện hoặc STOP/Emergency vẫn
dừng qua cơ chế hiện có. Độ trễ vượt giới hạn vẫn bị từ chối; đây không phải
cam kết mọi đường Internet đều đáp ứng. Các giá trị chưa có giao diện cấu hình.

Kiểm chứng: 22 suite native, 24 test SDK, 5 test lỗi và 17 assertion TCP/RTU đạt.
Có test dùng cả ModbusCore và Tunnel để tránh watchdog 3 giây còn sót; có test
cache hết hạn, độ trễ GET, firmware cũ, token bị từ chối và release hủy cache.

Xem [SDK](../api/docs/MQTT_TIMING.md).

## Kiểm tra trên bo thật (2026-11-11)

Đã nạp firmware 1.12.3 vào ESP32-S3, xác minh flash và giữ nguyên vùng
boot/partition/NVS. SDK 0.1.4 điều khiển qua broker.emqx.io:1883:

- Scan độc lập không Home: 2 EPC, 272 lượt đọc; dừng và nhả quyền thành công.
- Không gửi heartbeat, chỉ GET trạng thái: RF vẫn chạy sau 3 giây; lease giảm
  từ 8345 xuống 670 ms, sau đó owner=0 và activity_flags=0. Client nhận trạng
  thái hết quyền ở khoảng 11.24 giây, bao gồm độ trễ truy vấn MQTT.
- Không phát lệnh chạy cơ khí trong các thử nghiệm trên.
- Test broker cục bộ với transport production cũng đạt (254 yêu cầu quan sát).

Quan sát lease qua TCP bị timeout; đã dùng GET MQTT để hoàn tất kiểm tra.
Bản này chưa khắc phục hiện tượng timeout TCP thỉnh thoảng trên bo thật.
Các kết quả là kiểm tra tại thời điểm trên, không bảo đảm độ trễ Internet.
