# Kiến trúc

## Các lớp

```text
DWIN / PC UART     Modbus TCP / RS485      MQTT broker       Web cấu hình
       |                   |                   |                  |
   Hmi + SharedUart     EspModbus           EspMqtt           EspConfig
       |                   +----- ModbusCore <-- MqttApi          |
       +--------------------------+-------------------------------+
                                  |
                       DualControl_Process (một task)
                                  |
                     Tunnel state machine / ZK7182
                                  |
                    EspMotors / EspUsb / EspRuntime
                                  |
                      GPIO, GPTimer, LEDC, USB, NVS
```

`ModbusCore` nhận một `Backend` gồm đọc trạng thái, lệnh máy, bắt đầu/nhận kết quả reader, heartbeat và stop. Core không biết socket, UART, JSON hay chân GPIO. `MqttApi` chuyển yêu cầu MQTT về cùng core để quyền điều khiển không bị tách đôi. `MqttJson` chỉ decode/encode, không điều khiển phần cứng.

## Quyền sở hữu dữ liệu

| Ngữ cảnh | Trách nhiệm | Không được làm |
|---|---|---|
| Arduino control task | State machine, HMI, reader, ModbusCore, MQTT dispatch, quyết định cho phép lưu | Chờ network/reader vô hạn |
| GPTimer ISR | Phát STEP, cập nhật bộ đếm và dừng xung bằng lock của backend | Parse JSON, NVS, socket |
| Safety task | Giám sát heartbeat control loop và Emergency | Tự chạy lại chuyển động |
| MQTT callback | Copy gói có giới hạn vào queue, ghi cờ mất kết nối/lỗi | Gọi Tunnel hoặc đổi owner trực tiếp |
| MQTT sender task | Enqueue ACK/state vào MQTT client, ACK ưu tiên | Gia hạn lease, replay lệnh máy |
| HTTP task / config writer | Xác thực, chuẩn bị job, ghi NVS sau khi được cấp phép | Ghi NVS trong lúc motor/RF/owner đang hoạt động |
| USB callbacks | Báo trạng thái và chuyển dữ liệu qua adapter | Tự thay đổi trạng thái chu trình |

Các cờ trao đổi task dùng atomic hoặc FreeRTOS queue. Motor có critical section riêng. Header tương thích STM32 chỉ giữ ABI của thuật toán C; không giả lập một vi điều khiển STM32.

## Khởi động

`TunnelESP32.ino` gọi `tunnelSetup()`. Thứ tự: đặt motor về trạng thái an toàn → input GPIO → UART/lưu tốc độ → parser PC/DWIN → motor/reader/HMI/Tunnel → USB/Modbus/cấu hình → đánh dấu control alive → safety task. Nếu cấp phát UART/queue/timer/task thất bại, motor dừng và vòng điều khiển không tiếp tục.

`tunnelLoop()` cập nhật watchdog, chạy coordinator, xử lý USB/reader, đẩy một phần TX PC rồi nhường CPU. `EspRuntime` giữ việc UART/GPIO/SpeedStore; `EspApp` chỉ giữ lifecycle.

## Thứ tự một vòng điều khiển

1. Capture và parse DWIN; Emergency được ưu tiên.
2. Nếu có STOP, chốt dừng và xóa lệnh cũ đang chờ (*fence*), kết thúc vòng.
3. Poll Modbus/MQTT/cấu hình; mỗi lượt xử lý có giới hạn và luân phiên RTU/TCP.
4. PC Emergency, kiểm tra STOP lần nữa.
5. Nếu đang lưu cấu hình thì không chạy lệnh thường.
6. Xử lý PC thường, sensor edges, state machine; kiểm tra STOP lại.

Không đặt thêm callback điều khiển máy ở task mạng. Nếu thêm giao thức, chuyển lệnh vào coordinator và dùng cùng core.

## Giới hạn thiết kế

- Một controller tại một thời điểm; TCP tối đa 3 kết nối, MQTT tối đa 8 client logic trong một epoch.
- Lease 3000 ms, heartbeat khuyến nghị 500 ms. Đọc trạng thái qua Modbus/MQTT không gia hạn.
- Reader chỉ có một yêu cầu cấu hình đang chờ; reply tối đa 64 byte.
- MQTT `/cmd` tối đa 512 byte; `/get` 128 byte. Không tự phát state định kỳ.
- EPC duy nhất tối đa 1024, mỗi EPC tối đa 64 byte; vượt giới hạn trả lỗi thay vì im lặng bỏ qua.
- Chu trình scan có ngân sách 30 giây; không tự ghi cấu hình reader lúc bắt đầu inventory.

## Quy tắc sửa code

Giữ nguyên số enum trên wire, offset thanh ghi và VP. Thêm field MQTT theo hướng bổ sung; khi phá tương thích phải tạo contract/version mới có hướng dẫn migration. Đọc và ghi cùng một khai báo từ `contracts` khi có thể. Thay đổi thuật toán motor/STOP phải chạy các suite `cycle`, `two_doors`, `in7_confirmation`, `esp_step`, `esp_outputs`, `modbus_integration`; thay đổi parser phải kiểm thử dữ liệu thiếu, dư, trùng, quá dài và replay.
