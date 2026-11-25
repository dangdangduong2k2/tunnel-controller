# Backend ESP32

## Trách nhiệm

EspRuntime: UART0, virtual GPIO HAL và Preferences tốc độ. EspMotors: GPTimer/LEDC, xung, EN, watchdog. EspUsb: USB host CP210x. EspModbus: socket TCP, UART1 RS485, W5500, arbitration theo lượt và status backend. EspMqtt: callbacks/queues/sender. EspConfig: web/token/job/NVS, kèm `EspConfigSubmitLocal` cho màn DWIN. EspHmiUi: nối HmiUi với UART màn, trạng thái máy và cấu hình (cùng task điều khiển). EspHistory: NVS `tunnel-hist`, SNTP UTC+7, hook chốt kết quả; flash chỉ ghi khi máy đứng yên, trên task riêng. BoardProfile/StepSequence giữ pin và bộ đếm xung.

## Giao diện và ngữ cảnh gọi

`EspDwinUpdateBegin/Poll` giữ máy dừng, chờ ESP OTA VALID rồi lấy độc quyền UART0 trên control task để sửa block13/0. Giữ watchdog, theo dõi token STOP. NVS `dwin-upd/pending` phục hồi; `EspDwinUpdateHold` chặn điều khiển, history/config/OTA mới và HMI publisher tới khi xác minh. HTTP đọc status atomic. Không tự chạy motor sau cập nhật.

`EspHmiAntenna` (1.17.0) nối job AntennaSettings với ZK7182 trên cùng control task. Chỉ cấp quyền khi máy rảnh, không có remote owner/reply chờ, reader ready, không config/OTA/ghi lịch sử. Maintenance + HMI lock chặn máy; coordinator inhibit chặn lệnh remote, vẫn nhận STOP. Hủy phải drain lệnh đang bay trước khi nhả reader. Không giữ quyền để chỉnh bản nháp; chỉ giữ trong lúc đọc/ghi/readback.

`EspMotorsBegin`, `EspRuntimeBegin/Ready/Disable`, `EspUsbBegin/Poll`, `EspModbusBegin/Poll/Fence`, `EspMqttBegin/Poll`, `EspConfigBegin/Poll/WebBegin`. Header tương ứng là giao diện C/C++ chính thức.

## Phụ thuộc và quy tắc thay đổi

Chỉ lớp này phụ thuộc Arduino/ESP-IDF. Callback network không gọi state machine. Mutex/atomic/queue bảo vệ dữ liệu liên task; ISR motor giữ critical section thực, không dùng no-op HAL adapter để bảo vệ ISR. EspModbus giữ coordinator transport trong một file để thứ tự tick/fence/config/MQTT không phân tán sang nhiều task.

## Kiểm thử

`esp_step`, `esp_outputs`, `mqtt_transport`, `config_service`, `modbus_integration`, build Arduino.

## Source

- [BoardProfile.h](BoardProfile.h)
- [EspConfig.cpp](EspConfig.cpp)
- [EspConfig.h](EspConfig.h)
- [EspHistory.cpp](EspHistory.cpp)
- [EspHistory.h](EspHistory.h)
- [EspHmiUi.cpp](EspHmiUi.cpp)
- [EspHmiUi.h](EspHmiUi.h)
- [EspModbus.cpp](EspModbus.cpp)
- [EspModbus.h](EspModbus.h)
- [EspMotors.cpp](EspMotors.cpp)
- [EspMqtt.cpp](EspMqtt.cpp)
- [EspMqtt.h](EspMqtt.h)
- [EspPlatform.h](EspPlatform.h)
- [EspRuntime.cpp](EspRuntime.cpp)
- [EspRuntime.h](EspRuntime.h)
- [EspUsb.cpp](EspUsb.cpp)
- [StepSequence.h](StepSequence.h)
