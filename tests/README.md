# Kiểm thử

Tất cả test tự động mặc định chạy trên host/loopback, không nạp firmware hay mở COM thật. Chi tiết môi trường và lệnh tại [BUILD_TEST](../docs/BUILD_TEST.md).

## Native suites

`RUN_TESTS.cmd` → `run_modbus_regressions.py`: compile C và C++ riêng giữ ABI, mỗi suite build binary riêng trong `test_build/`. Assertion bật; fail compile/link/assert trả exit code khác0. Tên suite không tồn tại bị từ chối.

| Suite | Hành vi kiểm tra |
|---|---|
| reader_frequency | START/STOP thật, không config reader ngầm, fragmented EPC, timeout/reconnect/overflow |
| mqtt_transport | EspMqtt thật với fake ESP queue/client; get/state, flood isolation, retained/fragment, reconnect |
| mqtt_api | Numeric/named JSON, range/duplicate/mixed schema, ownership/replay/lease và reader |
| modbus_core | FC/frame/address/CRC/mailbox, sequence, owner, exceptions |
| hmi_network | VP/text/frame kết nối, NO LINK, config, round-robin và UART retry |
| config_service | HTTP/NVS job và fence thật với fake ESP/NVS |
| device_config | Schema/CRC/two slots, partial update, secrets, validation |
| protocol_sim | Build process host cho SDK integration; production core với fake IO |
| profile | Tính quỹ đạo xung |
| cycle | Chu trình/state machine |
| reader_cycle | Chu trình cùng driver ZK thật |
| integration | PC legacy + control + reader |
| hmi | DWIN parsing và event |
| esp_hmi | ESP streaming DWIN |
| dual | Thứ tự HMI/PC/STOP |
| shared_uart | DWIN + ASCII PC multiplexing, fragmentation và fence |
| dwin_manual | Nút cửa/scan/băng tải, repeat filtering, giới hạn liên động |
| two_doors | Hai cửa độc lập/limit/timeout/homing |
| in7_confirmation | Debounce/latch/dropout IN7 và khác biệt IN2 |
| esp_step | 55513 kiểm tra xung, profile, duty, pause/resume |
| esp_outputs | Backend output thật với fake timers/GPIO; STOP giữ EN, Emergency nhả EN |
| modbus_integration | Core + control + reader, local lock, STOP stale Reset, config fence |

## Integration Python

- `test_api_failures.py`: validate trước IO, ACK loss không replay, corrupt frame đóng transport, correlation/retained.
- `test_api_integration.py`: socket TCP thật localhost, pyserial mock fragmented RTU/CRC, cùng production core. Không mô phỏng chất lượng đường điện RS485.
- `test_mqtt_on_demand.py`: broker amqtt localhost + Paho + production EspMqtt host; hai client, không tự state, không renew lease khi đọc, tên method, đọc power/error/demo.
- `test_config_browser.py`: Chrome + HTTP handler C++ thật; test web config và simulated persistence/reboot.
- `test_read_modbus_status.py`: công cụ chẩn đoán đọc, localhost fragmented/CRC/exception.

## Phần cứng, chạy riêng có chủ đích

`verify_device_uart.py --port COMx` mở UART115200 và hỏi PC status, không Reset/motion; quan sát frame TX ESP32 không chứng minh DWIN đã hiển thị. PC status có thể gia hạn lease legacy đang có; không dùng để đánh giá timeout của owner legacy. Tool này không nằm trong test tự động. GPIO/frame fake không chứng minh nguồn, cách ly, xung điện hoặc motor cơ khí thực tế.

## Stub và thư viện test

`stubs_tunnel`, `stubs_hmi`, `stubs_reader`, `esp_stubs`, `config_stubs`, `mqtt_transport_stubs.h` cung cấp IO/clock/queue giả có mục đích. `vendor/cJSON` chỉ cho host compile và giữ license gốc. Production dùng cJSON từ ESP32 core. Không sửa stub để chỉ làm assertion qua khi production có lỗi.

Các test STM32 PWM/flash cũ không có backend tương ứng không được đưa sang target ESP32. Test HTTP gateway đã lỗi thời được thay bằng test SDK đang bàn giao, không giữ script import module không tồn tại.
