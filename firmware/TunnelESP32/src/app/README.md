# Điều phối ứng dụng

## Trách nhiệm

EspApp giữ setup/loop. dual_control xác định thứ tự DWIN → STOP → giao thức mạng → PC → sensor → Tunnel; STOP xóa lệnh cũ đang chờ trước khi có thể chạy vòng tiếp theo. History giữ500 kết quả đã chốt gần nhất trong RAM, logic thuần với storage tiêm vào (25 khối×20 bản ghi, CRC từng khối).

## Giao diện và ngữ cảnh gọi

`tunnelSetup`, `tunnelLoop`, `DualControl_Process`; chỉ Arduino task gọi. Safety task chỉ giám sát backend motor.

## Phụ thuộc và quy tắc thay đổi

Phụ thuộc platform/esp32, control, reader, hmi, protocol. Không thêm network wait hoặc parse JSON vào lifecycle. Giữ thứ tự init và fence.

## Kiểm thử

`dual`, `shared_uart`, `modbus_integration`, `history`, build ESP32 thật.

## Source

- [Application.h](Application.h)
- [dual_control.c](dual_control.c)
- [dual_control.h](dual_control.h)
- [EspApp.cpp](EspApp.cpp)
- [History.cpp](History.cpp)
- [History.h](History.h)
