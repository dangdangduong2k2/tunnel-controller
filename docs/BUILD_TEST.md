# Build, test, nạp và rollback

## Môi trường

Mốc đã dùng: Windows, Python3.11, Visual Studio2022 C++ Build Tools/Community (`cl` và Windows SDK), Arduino CLI với ESP32 Arduino core3.3.8. FQBN và core pin tại [release.json](../contracts/release.json). ESP32-S3, flash8MB, DIO, PSRAM disabled, partition default_8MB, USB host qua J4.

Arduino dùng thư mục sketch `src` để compile đệ quy: [Arduino CLI sketch specification](https://docs.arduino.cc/arduino-cli/sketch-specification/). Cài core theo [Espressif Arduino](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html). Khi nâng core phải chạy lại test và kiểm tra trên bo; không bỏ pin phiên bản chỉ để build qua.

```powershell
python -m venv .venv
.venv\Scripts\python.exe -m pip install -r tests\requirements.txt
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.8 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
.venv\Scripts\python.exe tools\generate.py --check
.venv\Scripts\python.exe tools\build.py
```

Nếu CLI không nằm trên PATH, dùng `--cli "đường dẫn arduino-cli.exe"` hoặc biến `ARDUINO_CLI`. Tool cũng tìm CLI đi kèm Arduino IDE ở Program Files. Không tự cài core hoặc tự đổi core trong build.

Output: `dist/TunnelESP32_v1_12_0_app.bin`, `dist/SHA256SUMS.txt`, `dist/build.json`. Đây là **app image**, không phải merged flash image. `build/` là cache có thể tái tạo. Các bước này không mở COM, không upload.

## Chạy test

```powershell
$env:PYTHON_EXE = (Resolve-Path .venv\Scripts\python.exe).Path
.\RUN_TESTS.cmd
.venv\Scripts\python.exe tests\test_api_failures.py
.venv\Scripts\python.exe tests\test_api_integration.py
.venv\Scripts\python.exe tests\test_mqtt_on_demand.py
.venv\Scripts\python.exe tests\test_config_browser.py
.venv\Scripts\python.exe tools\check_project.py
```

`RUN_TESTS.cmd` tìm VS qua vswhere hoặc `VSDEVCMD`; có thể chạy một suite: `RUN_TESTS.cmd mqtt_api`. Tên suite sai phải báo lỗi. Browser test dùng Chrome đã cài qua Playwright, không chạm thiết bị thật. Chi tiết phạm vi từng suite tại [tests](../tests/README.md).

Test Python broker chỉ bind127.0.0.1, dùng cổng động. Modbus RTU được giả lập byte/CRC qua Serial mock. Test cấu hình dùng handler C++ thật với NVS/GPIO giả. Không coi chúng là kiểm chứng RS485 điện, W5500, DWIN thực tế hoặc tải motor thật.

## Định dạng và codegen

```powershell
.venv\Scripts\python.exe -m pip install clang-format==18.1.8 ruff==0.8.6
.venv\Scripts\python.exe tools\format_source.py
.venv\Scripts\python.exe tools\generate.py
.venv\Scripts\python.exe tools\generate.py --check
```

Generated file không sửa tay và không format độc lập. Không format thư viện vendored; giữ diff/license upstream. File Python dùng Ruff format; C/C++ dùng `.clang-format`. Đây là quy ước project, không tuyên bố tuân thủ chứng nhận MISRA.

## Nạp có chủ đích

Trước khi nạp: xác định đúng bo/COM, tắt nguồn công suất motor/servo, giữ nguồn logic/màn theo sơ đồ; backup app và NVS/partition đang dùng vào nơi riêng. Không đóng gói backup NVS vì chứa credential. Đóng app đang giữ COM. J4 cần tháo reader khi dùng USB native để nạp.

Với **bo hiện tại đã có partition default_8MB và app ở0x10000**, dùng esptool ghi app mới tại0x10000 rồi verify SHA/digest. Không ghi app vào0x0. Nếu layout/chip/flash khác, phải đọc partition và xác minh trước; bo trắng cần bootloader/partition/boot app phù hợp từ build, không chỉ app.bin. Không chạy erase-all như một bước mặc định.

Sau nạp: đọc firmware=1120, owner/fault/motor trạng thái, xem IP và RTU trên DWIN; test đọc TCP trước. Kiểm tra MQTT `/get`/correlation; RS485 cần USB-RS485 và đúng baud/parity đang lưu. Chỉ thử chuyển động khi operator xác nhận cơ khí sẵn sàng.

Rollback: khi đã lưu app cũ và partition không đổi, ghi lại app cũ tại offset đã xác minh. Schema NVS1 và namespace tốc độ được giữ nguyên trong1.12.0; không cần xóa cấu hình để rollback1.11.0. Cấu hình broker/mạng/RTU hiện lưu trên bo không được suy ra từ defaults trong source.
