# Tunnel Controller 1.17.2 — ESP32 updates DWIN Scan buttons

Bản 1.17.2 tự sửa nút DWIN qua P1 sau khi ESP OTA xác nhận app mới: fingerprint, đọc lại RAM, CRC flash và phục hồi boot sau. Đồng thời xóa cả chữ/nền thông báo tạm hết hạn. Chỉ cần app ESP32 qua OTA; xem [hướng dẫn 1.17.2](docs/RELEASE_1_17_2.md).

Bản 1.17.1 sửa hai nút Scan START/STOP gửi cùng mã toggle: START chỉ bật scan, STOP chỉ dừng, bấm lặp không đảo trạng thái. **Cần cập nhật cả firmware và file nút DWIN**; xem [hướng dẫn 1.17.1](docs/RELEASE_1_17_1.md). Giữ các sửa antenna và tốc độ của 1.17.0.

Bản 1.17.0 nối trang **Antennas** trên HMI với reader thật: bật/tắt 4 cổng, công suất 0–30 dBm, lưu vào reader hoặc dùng RAM, đọc lại để xác minh. Chia lại 5 mức tốc độ khoảng **95 / 111 / 133 / 160 / 200 kHz**; mức 5 mới bằng mức 3 cũ. Xem [hướng dẫn và kiểm thử 1.17.0](docs/RELEASE_1_17_0.md). Đã kiểm thử phần mềm; cần xác nhận trên reader/băng tải thực tế sau nạp.

**[User Guide tiếng Việt - VI 1.1, 28 trang](api/docs/Tunnel_User_Guide_VI.pdf)** (cập nhật 17/11/2026).

Hướng dẫn kết nối Modbus TCP theo DHCP/Static (trang 24), cài đặt HMI, MQTT, OTA và scan độc lập (trang 25-28); kèm bảng thanh ghi và lệnh của ba giao thức. Guide cập nhật theo firmware **1.16.4**; các mục HMI/OTA cần firmware và gói màn tương ứng. Kiểm tra phiên bản thực tế của bo tại IR1 trước khi áp dụng.

**[User Guide tiếng Việt - VI 1.1, 28 trang](api/docs/Tunnel_User_Guide_VI.pdf)** (cập nhật 17/11/2026).

Hướng dẫn kết nối Modbus TCP theo DHCP/Static (trang 24), cài đặt HMI, MQTT, OTA và scan độc lập (trang 25-28); kèm bảng thanh ghi và lệnh của ba giao thức. Guide cập nhật theo firmware **1.16.4**; các mục HMI/OTA cần firmware và gói màn tương ứng. Kiểm tra phiên bản thực tế của bo tại IR1 trước khi áp dụng.

Source ESP32-S3 cho bo Tunnel: điều khiển hai cửa, băng tải, reader ZK-7182M, màn DWIN; PLC/HMI sử dụng Modbus TCP hoặc Modbus RTU, phần mềm sử dụng MQTT. Ba giao thức dùng chung trạng thái máy và quyền điều khiển.

Bản 1.16.4 sửa lỗi thiếu RAM khi bắt tay HTTPS: bảng chống trùng EPC chỉ chiếm
RAM trong phiên scan, giữ nguyên sức chứa và số đếm đã chốt. Xem
[nguyên nhân, thay đổi và kiểm chứng](docs/RELEASE_1_16_4.md).

Bản 1.16.2 chuyển cấu hình tin cậy OTA của bo thử nghiệm sang HTTPS công khai
trên VPS, dùng ISRG Root X1 và giữ nguyên khóa xác minh firmware. Xem
[triển khai và kết quả kiểm tra](docs/RELEASE_1_16_2.md).

Bản 1.16.1 cho phép OTA **nâng cấp, nạp lại hoặc hạ phiên bản** của file tương
thích. Đã bỏ so sánh phiên bản ở web, API, SDK và firmware; vẫn giữ chữ ký,
kiểm tra file và chống lệnh cũ/trùng. Xem [ghi chú 1.16.1](docs/RELEASE_1_16_1.md).

Bản 1.16.0 làm lại OTA ESP32: sửa lỗi tự hủy khi chuẩn bị STOP, đọc phiên bản
tự động từ `.bin`, đối chiếu URL/khóa ký trước khi gửi, theo dõi khởi động và
kết quả theo job. Xem [hướng dẫn và test case OTA](docs/OTA_1_16_0.md).
Kiểm thử phần mềm không thay thế thử tải/rollback trên bo thật.

Bản này chuẩn hóa từ 1.11.0: tách module, tách SDK, gom contract, thêm MQTT theo tên và tài liệu. Giữ nguyên thuật toán chu trình, map thanh ghi, OP và cấu hình NVS. Đây là source để build/kiểm thử; không tự nạp vào bo khi chạy build hoặc test.

Bản 1.15.0: **màn hình DWIN HMI v2** — cài đặt Ethernet/Modbus/MQTT và tốc độ băng tải ngay trên màn, lịch sử 500 kết quả quét gần nhất có giờ NTP. Antennas/Reader là giai đoạn 2. Xem [HMI v2](docs/HMI_V2.md).

Từ 1.12.2: **Scan thủ công độc lập, không cần homing**, không chạy motor. Chu trình AUTO/SEMI vẫn kiểm tra cửa như trước. Xem [hướng dẫn Scan độc lập](docs/SCAN_STANDALONE.md).

Bản 1.13.0 đã nạp qua USB và xác minh flash trên bo thử nghiệm; **chưa kiểm chứng cập nhật qua OTA/rollback trên bo thật** (bo đã Online và tự phát hiện qua MQTT). Xem [FOTA](docs/FOTA.md) và [công cụ test HTTPS](tools/fota/README.md).

[Trang cập nhật firmware](tools/fota/PORTAL.md): một trang, không đăng nhập,
tự nhận diện bo qua MQTT; không điền sẵn thiết bị. Cần firmware có discovery để
bo xuất hiện. Trang đã triển khai tại
https://103-9-78-139.sslip.io/tunnel/admin/ . Bo có cấu hình URL thử cục bộ cũ
cần nạp lại qua USB một lần để chuyển sang URL công khai.

Bản 1.13.1 tăng mã firmware lên 1131 để thử cập nhật từ 1.13.0;
không đổi chức năng máy. Xem [ghi chú bản test](docs/RELEASE_1_13_1.md).

## SDK theo giao thức

Chọn giao thức để xem danh sách hàm, tham số kết nối và ví dụ riêng.

### Modbus RTU / RS485

USB-RS485, mặc định ID 1 / 19200 / 8E1. PLC hoặc máy tính là master, bo là slave.

Tạo client: `TunnelDevice.rtu("COM7", baud=19200, parity="E", unit=1)`; dùng `connect()` hoặc `with` để mở kết nối.

| Nhóm | Hàm được cung cấp |
|---|---|
| Kết nối / đọc | `connect`, `is_connected`, `close`, `get_status`, `get_epc_count`, `get_functions` |
| Quyền điều khiển / dừng | `claim`, `has_control`, `heartbeat`, `release`, `stop`, `emergency` |
| Máy | `reset`, `set_gate`, `set_belt`, `set_scan`, `set_mode`, `confirm`, `rescan`, `set_speed`, `set_maintenance` |
| Reader / antenna | `get_reader_info`, `get_antenna_power`, `set_antenna_power`, `set_antenna_mask`, `get_rf_profile`, `set_rf_profile`, `get_gen2x`, `set_gen2x`, `get_ex10`, `measure_return_loss` |
| Gọi lệnh / lấy kết quả | `send_command`, `execute`, `get_reader_result` |

[Xem cách gọi từng hàm Modbus RTU / RS485 và ví dụ](api/README.md#modbus-rtu).

### Modbus TCP/IP

RJ45; dùng IP thực tế trên DWIN. Mặc định TCP 502, Unit ID 1. Giữ cùng socket khi điều khiển.

Tạo client: `TunnelDevice.tcp("192.168.1.211", port=502, unit=1)`; dùng `connect()` hoặc `with` để mở kết nối.

| Nhóm | Hàm được cung cấp |
|---|---|
| Kết nối / đọc | `connect`, `is_connected`, `close`, `get_status`, `get_epc_count`, `get_functions` |
| Quyền điều khiển / dừng | `claim`, `has_control`, `heartbeat`, `release`, `stop`, `emergency` |
| Máy | `reset`, `set_gate`, `set_belt`, `set_scan`, `set_mode`, `confirm`, `rescan`, `set_speed`, `set_maintenance` |
| Reader / antenna | `get_reader_info`, `get_antenna_power`, `set_antenna_power`, `set_antenna_mask`, `get_rf_profile`, `set_rf_profile`, `get_gen2x`, `set_gen2x`, `get_ex10`, `measure_return_loss` |
| Gọi lệnh / lấy kết quả | `send_command`, `execute`, `get_reader_result` |

[Xem cách gọi từng hàm Modbus TCP/IP và ví dụ](api/README.md#modbus-tcp).

### MQTT

Bo và ứng dụng cùng broker/root. Đọc /get → /state, lệnh /cmd → /ack; heartbeat do ứng dụng chủ động gửi.

Tạo client: `TunnelDevice.mqtt("broker.example.com", root="factory/tunnel01/v1")`; dùng `connect()` hoặc `with` để mở kết nối.

| Nhóm | Hàm được cung cấp |
|---|---|
| Kết nối / đọc | `connect`, `is_connected`, `close`, `get_status`, `get_epc_count`, `get_functions` |
| Quyền điều khiển / dừng | `claim`, `has_control`, `heartbeat`, `release`, `stop`, `emergency` |
| Máy | `reset`, `set_gate`, `set_belt`, `set_scan`, `set_mode`, `confirm`, `rescan`, `set_speed`, `set_maintenance` |
| Reader / antenna | `get_reader_info`, `get_antenna_power`, `set_antenna_power`, `set_antenna_mask`, `get_rf_profile`, `set_rf_profile`, `get_gen2x`, `set_gen2x`, `get_ex10`, `measure_return_loss` |
| Gọi lệnh / lấy kết quả | `send_command`, `execute`, `get_reader_result` |

[Xem cách gọi từng hàm MQTT và ví dụ](api/README.md#mqtt).

## Bắt đầu

1. Đọc [phần cứng](docs/HARDWARE.md), [build/test](docs/BUILD_TEST.md).
2. Dùng Python 3.11 và Arduino ESP32 core **3.3.8**. Chạy `python tools/generate.py --check`, rồi `python tools/build.py`.
3. Firmware nằm trong `dist/`; đọc hướng dẫn nạp/rollback trước khi nạp. Không dùng erase-all nếu muốn giữ cấu hình.
4. Khi tích hợp thiết bị, dùng [API chung](docs/API.md), [Modbus](docs/MODBUS.md), [MQTT](docs/MQTT.md), [SDK Python](api/README.md).

SDK Python hiện cài được bằng `python -m pip install "./api[all]"`.
API `TunnelDevice` có đủ 19 lệnh; xem [cách gọi từng hàm](api/docs/API_REFERENCE.md).
SDK **0.1.4** có [console đủ 19 lệnh](api/example/README.md) dùng chung RTU/TCP/MQTT,
giữ phiên Confirm/Rescan, kiểm tra readback và báo [BUSY](api/docs/STATUS_AND_ERRORS.md#busy-khi-chưa-thể-nhận-lệnh-trong-phiên-hiện-tại)
khi bo từ chối. Firmware đã có mã BUSY; SDK không tự retry hay giành quyền.
Xuất bộ SDK độc lập: `python tools/export_sdk.py <thu-muc-moi>` rồi build trong thư mục đó.

## Thành phần

| Thư mục | Nội dung |
|---|---|
| [firmware/TunnelESP32](firmware/TunnelESP32/README.md) | Sketch và source ứng dụng |
| [app](firmware/TunnelESP32/src/app/README.md) | Khởi động, vòng lặp, thứ tự xử lý STOP |
| [control](firmware/TunnelESP32/src/control/README.md) | Chu trình máy, trạng thái, quỹ đạo xung |
| [reader](firmware/TunnelESP32/src/reader/README.md) | Driver ZK và EPC |
| [hmi](firmware/TunnelESP32/src/hmi/README.md) | DWIN/DGUS và trạng thái kết nối |
| [protocol](firmware/TunnelESP32/src/protocol/README.md) | Modbus, MQTT, giao thức PC cũ |
| [platform](firmware/TunnelESP32/src/platform/README.md) | Backend ESP32: GPIO, timer, USB, mạng, NVS |
| [config](firmware/TunnelESP32/src/config/README.md) | Cấu hình chạy máy và cấu hình lưu bền |
| [common](firmware/TunnelESP32/src/common/README.md) | Phiên bản, thời gian lease, mã STOP |
| [contracts](contracts/README.md) | Định nghĩa phiên bản và lệnh, đầu vào codegen |
| [api](api/README.md) | SDK, ví dụ và tài liệu bàn giao tích hợp |
| [web](web/README.md) | Trang cấu hình Ethernet/RTU/MQTT |
| [libraries](libraries/README.md) | Driver USB vendored và license gốc |
| [tests](tests/README.md) | Regression native, SDK, broker và trình duyệt |
| [tools](tools/README.md) | Sinh mã, build, kiểm tra tài liệu, đóng gói |

## Tài liệu kỹ thuật

- [Kiến trúc và luồng xử lý](docs/ARCHITECTURE.md)
- [API, quyền điều khiển, kết quả và lỗi](docs/API.md)
- [Các lệnh và tác động](docs/COMMANDS.md)
- [Reader, antenna và mã hóa tham số](docs/READER_API.md)
- [Trạng thái, fault, STOP](docs/STATES.md)
- [Cấu hình và vận hành](docs/CONFIGURATION.md)
- [Migration và tương thích](docs/MIGRATION.md)
- [Phạm vi bảo mật và giới hạn](docs/SECURITY.md)
- [Kết quả kiểm chứng](docs/VALIDATION.md)
- [Changelog](CHANGELOG.md)

Mỗi thư mục thành phần có README nêu trách nhiệm, entry point, phụ thuộc, quy tắc khi sửa và bài test liên quan. Public header là định nghĩa hàm chính xác; không sửa các file có dòng `Generated` bằng tay.

[MQTT timing](docs/MQTT_TIMING.md)
