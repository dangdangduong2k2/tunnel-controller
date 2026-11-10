# SDK validation - 11/11/2026

## 0.1.2 / firmware1.12.1

- 19 SDK unit tests pass; includes independent scan workflow with no Reset,
  maintenance or motor calls, rejection of old firmware/foreign owner, and cleanup
  after reader failure. Original 19-operation mapping remains unchanged.
- 5 failure tests, 17 localhost TCP/simulated RTU integration assertions and
  MQTT localhost tests pass. Firmware native suites:22 pass, including stationary
  RF/lease/Emergency and unchanged AUTO/SEMI door interlocks.
- Added `example/scan_tags.py`; the console still supports `scan 1` / `scan 0`.

## Previous 0.1.1 validation

- Built sdist and wheel from sdist; installed wheel into a new Python 3.12 venv.
  All 16 SDK tests passed against that installed wheel with isolated Python and
  no PYTHONPATH. All 5 example --help commands passed without device connections.
- Sdist includes console, tests and handoff documents. Markdown Python snippets
  parsed successfully. Ruff check passed.

- 16 SDK test methods including 57 operation/transport combinations (19 x 3),
  all 19 console commands, BUSY decoding on all transports, persistent ownership,
  pending-reader heartbeat, timeout/no replay and readback mismatch.
- 5 existing SDK failure tests passed.
- 17 native-core TCP/RTU integration assertions passed. TCP uses localhost sockets;
  RTU serial is simulated, including framing, CRC and fragmented replies.
- MQTT tested with localhost broker, Paho and native production core. Public API
  emits numeric OPs; new console read reader data and released control. A second
  MQTT client received BusyError/code5 while the first retained ownership.
- Four rebuilt native suites passed: integration (real Tunnel/reader),
  modbus_integration, modbus_core and mqtt_api. Inventory rejects maintenance,
  speed/mode changes and reader config without stopping the active scan. Pending
  reader rejects the next OP without queueing it. Reads, heartbeat and STOP remain
  available. MQTT preserves result2 and deduplicates the refused request.
- Project structure, Python syntax, local Markdown links and generated contracts passed.
- Firmware source and wire contract are unchanged; this release exposes and tests
  the BUSY behavior already implemented by the board firmware.

No physical machine, COM port or remote broker was used during these checks.
The workflow includes Python 3.10-3.13 on Windows/Linux, but that remote matrix
has not been run here. The local verification used Windows/Python 3.12.

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
