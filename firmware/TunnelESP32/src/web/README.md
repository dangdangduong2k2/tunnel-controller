# Web nhúng trong firmware

## Trách nhiệm

ConfigPage.h là chuỗi HTML được sinh từ web/index.html để HTTP handler phục vụ trang cấu hình offline, không phụ thuộc CDN.

## Giao diện và ngữ cảnh gọi

`CONFIG_PAGE` là static string; EspConfig index handler gửi trực tiếp.

## Phụ thuộc và quy tắc thay đổi

Không sửa file generated; sửa web/index.html rồi codegen. Chunk string để MSVC host tests và ESP compiler cùng chấp nhận. Không đưa credential thật vào HTML.

## Kiểm thử

`config_service`, `test_config_browser.py`, codegen drift check.

## Source

- [ConfigPage.h](ConfigPage.h)
