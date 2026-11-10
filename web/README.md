# Web cấu hình

[index.html](index.html) là source duy nhất của UI web tiếng Việt. Trang chạy offline từ ESP32, quản trị mạng/RS485/broker và password. Không có nút chạy motor; không cần Node/frontend bundler.

Sửa HTML/CSS/JS tại đây → `python tools/generate.py` → build. Header `firmware/TunnelESP32/src/web/ConfigPage.h` là generated artifact, dùng chunk raw string để cả MSVC test và ESP32 compile được.

UI gọi API `/api/login`, `/api/config`, `/api/status`, `/api/logout` tại [CONFIGURATION](../docs/CONFIGURATION.md). Không coi HTTP202 là save đã hoàn tất; poll job/phase, hướng dẫn reconnect khi bo restart/đổi IP. Không điền lại password broker đã lưu vào DOM.

Kiểm thử `tests/test_config_browser.py`: Chrome headless + C++ handlers thật, NVS/GPIO giả; login, validation, sửa broker, credential redaction, reboot, logout và JavaScript errors.
