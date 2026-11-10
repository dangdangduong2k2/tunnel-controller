# Scan RFID độc lập — firmware 1.12.1

Lệnh Scan thủ công đọc tag khi không có cơ khí, không cần homing hoặc cảm biến
cửa. Không thay đổi OP/register: OP4 ARG0=1 bắt đầu, ARG0=0 dừng; SDK gọi
`set_scan(1)` / `set_scan(0)` trên TCP, RTU hoặc MQTT. DWIN Scan vẫn nhấn để bật/tắt.

## Thứ tự thao tác

1. Kết nối và đọc trạng thái. Chỉ tiếp tục khi máy rảnh, không có chủ khác.
2. `claim()`; duy trì heartbeat khoảng500ms trong cùng client.
3. `set_scan(1)`; kiểm tra `accepted=True`, không chỉ kiểm tra FC16 hoặc MQTT code0.
4. Đọc `epc_count`, `total_reads`, `scan_elapsed_ms`, `scan_result` và `activity_flags`.
5. `set_scan(0)`; chờ hết scan và reader pending, sau đó `release()`.

Không gọi Reset hoặc bật maintenance để scan. Phiên tối đa30giây; EPC/lượt đọc
được xóa khi bắt đầu phiên mới. Ví dụ [scan_tags.py](../api/example/scan_tags.py)
thực hiện toàn bộ luồng với thời gian mặc định8giây và giữ heartbeat riêng.

## Điều kiện và giới hạn

- Mọi motor phải đứng yên. Lệnh Scan không khởi tạo motor, không đổi EN hoặc homing.
- Scan độc lập không dùng các cảm biến cửa. **AUTO/SEMI/Rescan của chu trình vẫn
  dùng liên động cửa và homing hiện có**; không dùng Scan thủ công để vượt chu trình.
- Khi RF đang chạy/đang dừng, Reset, cửa, băng tải, mode và cài đặt trả BUSY.
- Mất lease, disconnect/STOP hoặc Emergency dừng RF; không tự chạy lại.
- Kết thúc Scan giữ STOPPED, không chuyển sang chờ kích AUTO bằng cảm biến.
- Emergency vẫn khóa Scan; chỉ Reset có chủ đích mới xóa khóa Emergency. Claim
  mới không xóa khóa này. Reader chưa ready trả4; fault/trạng thái không phù hợp trả2.
- Scan ON lặp trong cùng phiên đang ON không xóa số EPC. Đang drain trả BUSY,
  không xếp hàng một lần ON mới. Đọc status không gia hạn lease.

## MQTT công cộng

`get_status()` chỉ gửi `/get`, không Scan. Lệnh điều khiển gửi `/cmd` và phải nhận
ACK tương ứng. Token của bo có hạn1500ms; lease3000ms. Đã quan sát broker công cộng
trả state với tuổi token700–1200ms và từ chối claim code3 do trễ lượt tiếp theo.
Đó là từ chối token, không phải lỗi antenna. Không tự nới giới hạn an toàn hoặc
lặp lệnh không rõ kết quả; test trực tiếp qua TCP LAN trước khi chọn broker phù hợp.

Tài liệu PDF bàn giao cũ mô tả hành vi firmware1.12.0. Trang này và API Reference
là phần cập nhật cho Scan thủ công từ1.12.1.

## Kiểm tra trên bo ngày 11/11/2026

Đã nạp và xác nhận firmware 1.12.1. Scan được chấp nhận khi chưa Home;
không gửi Reset hoặc lệnh motor. ANT1 bật, công suất 22 dBm.
Hai lần thử TCP vẫn có EPC = 0 và có lỗi đóng kết nối; sau đó bo báo STOPPED,
owner = 0, activity = 0, fault = 0. Vì vậy phần đọc tag thật và độ ổn định
kết nối trong Scan **chưa được xác nhận đạt**. Không tự lặp lệnh khi mất ACK.

## Bản sửa 1.12.2 — quyền START ở lớp USB

1.12.1 chỉ bỏ điều kiện Home ở tầng điều khiển. Lớp USB vẫn dùng khóa motor
`EspEmergencyLatched()` để chặn START 0x50; claim chủ động khóa motor nên START
không ra tới reader, dù status đã báo scan đang tiến hành. Vì vậy không dùng
1.12.1 để nghiệm thu Scan độc lập. Ví dụ SDK 0.1.3 yêu cầu firmware >= 1.12.2.

1.12.2 cấp quyền RF riêng sau khi kiểm tra Scan hợp lệ, không xóa khóa motor.
Mỗi yêu cầu START mang token; STOP/Emergency/Scan OFF vô hiệu token cũ, kể cả
START còn trong hàng đợi USB. STOP xảy ra trong lúc xin quyền làm xin quyền thất bại.
Test dùng backend motor ESP thật xác nhận các trường hợp này; 22 suite native đạt.
Đã nạp 1.12.2 và verify flash; boot/partition/NVS giữ nguyên. Scan thực tế từ
STARTUP_WAIT, chưa Home, đọc được 2 EPC. Phiên 3 giây có 51 lượt đọc, phiên
12 giây có 193 lượt đọc; cả hai dừng và release thành công. Không gửi Reset
hoặc lệnh motor; các mẫu status không có bit motor hoạt động. Trạng thái cuối:
STOPPED, owner = 0, activity = 0, fault = 0, EPC = 2.

Lần thử đầu vẫn gặp lỗi đóng TCP giữa phiên (đã đọc 2 EPC); hai lần tiếp theo
không tái hiện. Chưa xác định nguyên nhân TCP gián đoạn, chưa coi lỗi này đã sửa.
SDK giữ lỗi gốc nếu cleanup cũng lỗi, không tự retry hoặc claim lại.

## Cập nhật MQTT từ 1.12.3

Thời hạn 1500/3000 ms mô tả bản trước. Bản 1.12.3 dùng token 5000 ms,
lease MQTT 10000 ms; xem [giới hạn MQTT](MQTT_TIMING.md).
