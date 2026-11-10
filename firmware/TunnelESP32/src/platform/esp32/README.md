# Backend ESP32

## Trách nhiệm

EspRuntime: UART0, virtual GPIO HAL và Preferences tốc độ. EspMotors: GPTimer/LEDC, xung, EN, watchdog. EspUsb: USB host CP210x. EspModbus: socket TCP, UART1 RS485, W5500, arbitration theo lượt và status backend. EspMqtt: callbacks/queues/sender. EspConfig: web/token/job/NVS. BoardProfile/StepSequence giữ pin và bộ đếm xung.

## Giao diện và ngữ cảnh gọi

`EspMotorsBegin`, `EspRuntimeBegin/Ready/Disable`, `EspUsbBegin/Poll`, `EspModbusBegin/Poll/Fence`, `EspMqttBegin/Poll`, `EspConfigBegin/Poll/WebBegin`. Header tương ứng là giao diện C/C++ chính thức.

## Phụ thuộc và quy tắc thay đổi

Chỉ lớp này phụ thuộc Arduino/ESP-IDF. Callback network không gọi state machine. Mutex/atomic/queue bảo vệ dữ liệu liên task; ISR motor giữ critical section thực, không dùng no-op HAL adapter để bảo vệ ISR. EspModbus giữ coordinator transport trong một file để thứ tự tick/fence/config/MQTT không phân tán sang nhiều task.

## Kiểm thử

`esp_step`, `esp_outputs`, `mqtt_transport`, `config_service`, `modbus_integration`, build Arduino.

## Source

- [BoardProfile.h](BoardProfile.h)
- [EspConfig.cpp](EspConfig.cpp)
- [EspConfig.h](EspConfig.h)
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
