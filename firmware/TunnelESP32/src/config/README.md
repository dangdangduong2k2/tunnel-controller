# Cấu hình

## Trách nhiệm

DeviceConfig chứa Settings/Record/schema, defaults, validation, partial JSON, CRC và Store hai slot. app_config chứa tham số thuật toán. SafetyConfig chứa mức EN, Emergency và đảo hướng cửa.

## Giao diện và ngữ cảnh gọi

`cfg::defaults/validate/parse/json/canApply`, `cfg::Store::load/save/get/revision`. Settings đọc runtime qua EspConfigGet; không sửa trực tiếp từ HTTP callback.

## Phụ thuộc và quy tắc thay đổi

DeviceConfig chỉ phụ thuộc cJSON/standard library nên host-test được. Giữ schema1/packed layout để đọc NVS1.11. Secret không trả trong JSON public. NVS write chỉ sau coordinator cho phép và fence; changed config áp qua reboot.

## Kiểm thử

`device_config`, `config_service`, `test_config_browser.py`.

## Source

- [app_config.h](app_config.h)
- [DeviceConfig.cpp](DeviceConfig.cpp)
- [DeviceConfig.h](DeviceConfig.h)
- [SafetyConfig.h](SafetyConfig.h)
