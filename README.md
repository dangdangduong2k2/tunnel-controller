# Tunnel Controller 1.12.0

Source ESP32-S3 cho bo Tunnel: điều khiển hai cửa, băng tải, reader ZK-7182M, màn DWIN; PLC/HMI sử dụng Modbus TCP hoặc Modbus RTU, phần mềm sử dụng MQTT. Ba giao thức dùng chung trạng thái máy và quyền điều khiển.

Bản này chuẩn hóa từ 1.11.0: tách module, tách SDK, gom contract, thêm MQTT theo tên và tài liệu. Giữ nguyên thuật toán chu trình, map thanh ghi, OP và cấu hình NVS. Đây là source để build/kiểm thử; không tự nạp vào bo khi chạy build hoặc test.

## Bắt đầu

1. Đọc [phần cứng](docs/HARDWARE.md), [build/test](docs/BUILD_TEST.md).
2. Dùng Python 3.11 và Arduino ESP32 core **3.3.8**. Chạy `python tools/generate.py --check`, rồi `python tools/build.py`.
3. Firmware nằm trong `dist/`; đọc hướng dẫn nạp/rollback trước khi nạp. Không dùng erase-all nếu muốn giữ cấu hình.
4. Khi tích hợp thiết bị, dùng [API chung](docs/API.md), [Modbus](docs/MODBUS.md), [MQTT](docs/MQTT.md), [SDK Python](api/README.md).

SDK Python hiện cài được bằng `python -m pip install "./api[all]"`.
API `TunnelDevice` có đủ 19 lệnh; xem [cách gọi từng hàm](api/docs/API_REFERENCE.md).
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
