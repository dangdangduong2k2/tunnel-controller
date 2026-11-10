# EspUsbReaderHost

Upstream: [Espressif esp-usb](https://github.com/espressif/esp-usb), commit `fc9bead300f82484eace377d5dea2f3e11163206`; CDC ACM2.4.1, CP210x VCP2.2.0. [UPSTREAM.txt](UPSTREAM.txt) ghi thay đổi packaging; [LICENSE](LICENSE) Apache2.0 và SPDX gốc được giữ.

`src/` chứa driver CDC/descriptor/CP210x; `src/usb` public headers; `src/esp_private` private adapter header. `EspUsbReaderHost.h` giúp Arduino library discovery. Không thay implementation upstream trong bản refactor này. EspUsb.cpp của project quản lý lifecycle/queue kết nối reader.

Để nâng driver: chọn commit/version cụ thể, cập nhật provenance/license, build core đã pin, test USB disconnect/reconnect và inventory trên reader thật. Host tests không thay thế USB enumeration thực tế.
