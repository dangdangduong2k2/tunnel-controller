# Kiểm chứng

## Bản 1.12.1 — Scan thủ công độc lập

- 22 suite native C/C++ đạt, gồm AUTO/SEMI/cửa và test mới cho RF không homing.
- Test mới xác nhận không có lần start motor, EPC tăng, từ chối Reset/chuyển động/
  maintenance khi scan, dừng/drain, hết 30 giây không chạy AUTO, mất lease dừng RF,
  claim lại không xóa khóa Emergency. DWIN cũng scan độc lập với cửa mở.
- 19 test SDK, 5 test lỗi, 17 assertion TCP/RTU và MQTT localhost đạt.
- Build ESP32 thành công bằng core 3.3.8. Chi tiết API tại [Scan độc lập](SCAN_STANDALONE.md).
- Đã nạp 1.12.1 qua COM20; verify flash đạt, boot/partition/NVS giữ nguyên.
- Bo báo firmware 1121, DHCP và reader sẵn sàng. Từ STARTUP_WAIT chưa Home,
  Scan được chấp nhận; không gửi Reset hoặc lệnh motor. Các mẫu status không có
  bit motor hoạt động. ANT1 bật, công suất đọc lại 22 dBm.
- **Chưa đạt kiểm chứng đọc tag thật:** hai lần thử TCP có EPC = 0; lần đầu
  mất kết nối sau khoảng 2,4 giây, lần sau quan sát scan đến 7,8 giây rồi lỗi
  kết nối trong bước dừng/thu hồi. Trạng thái đọc lại: STOPPED, owner = 0,
  activity = 0, fault = 0. Chưa xác định nguyên nhân mất kết nối hoặc EPC = 0.
- Test mô phỏng không thay thế kiểm chứng RF, RS485 điện hoặc tải motor.

## Mốc kiểm chứng bản1.12.0

Hoàn tất kiểm tra source/build ngày11/11/2026 (Asia/Saigon). **Chưa nạp bản refactor lên bo, chưa chạy acceptance test phần cứng1.12.0.** Mốc1.11.0 đã qua21suite native trước khi refactor.

| Kiểm tra | Kết quả |
|---|---|
| Native C/C++ |22 suite đạt; gồm motor/cycle/reader/HMI/Modbus/MQTT/config |
| SDK failure |5test đạt: ACK loss, no retry, validation trước IO, correlation |
| TCP/RTU SDK |11assertion đạt; TCP localhost thật, RTU serial mock |
| Read-only diagnostic |4test đạt |
| MQTT |Broker localhost + Paho + production EspMqtt host; on-demand,2client, named method, reader power/error/heartbeat đạt |
| Browser |15assertion đạt; HTML được phục vụ từ header C++ đã compile, khớp web source;4kích thước375/768/1024/1440px |
| Bảo toàn thuật toán |13module không đổi token thuật toán, sau khi bỏ comment/layout/include path và thay macro lease bằng giá trị cũ |
| Codegen/format/lint/docs |Generated artifacts đồng bộ; clang-format18.1.8/Ruff; Python syntax và link Markdown hợp lệ |
| Firmware ESP32 |Build thành công với core 3.3.8 và FQBN đã pin |

App: `TunnelESP32_v1_12_0_app.bin`, 955552byte. SHA256:

```text
fd51f6818eb76f6e71a0a0fa4058cfe86e49106dcfcb86ded17648baf6c60192
```

Log/ảnh/manifest tại [validation](../validation/README.md). App/hash/build manifest tại `dist/`. `SOURCE_MANIFEST.json` trong ZIP kiểm tra hash từng file được bàn giao.

## Những gì test chưa chứng minh

Native test dùng IO/timer/USB/NVS giả; TCP/broker/browser dùng localhost. Chưa xác nhận W5500 thực, tín hiệu RS485/cách ly/termination, reconnect USB reader thật, DWIN hiển thị/touch thực, xung điện/tải motor và mất nguồn trên máy sau refactor. Cần test này trước phát hành sản xuất. Không suy ra chứng nhận an toàn/MISRA/TLS từ việc unit test đạt.

Trước nạp đọc [BUILD_TEST](BUILD_TEST.md). Source gốc1.11.0 và backup thiết bị giữ nguyên bên ngoài gói này; app mới không tự reset cấu hình NVS.
