# Công cụ phát triển

| Tool | Tác dụng |
|---|---|
| [generate.py](generate.py) | Sinh version/operation catalog/Python mapping/COMMANDS.md/web header; `--check` kiểm tra stale |
| [build.py](build.py) | Kiểm tra core pin, build ESP32, xuất app/hash/manifest; không upload |
| [format_source.py](format_source.py) | clang-format18.1.8 và Ruff format; `--check`; bỏ generated/vendor |
| [check_project.py](check_project.py) | Syntax Python, link Markdown, README từng component, mapping migration và codegen |
| [package.py](package.py) | Tạo ZIP sạch, manifest/hash; chỉ những thư mục được liệt kê, không NVS/cache |
| [read_modbus_status.py](read_modbus_status.py) | Chẩn đoán FC04 đọc32registers; cần `--host` rõ ràng; không motion |

Chạy từ bất kỳ cwd: tool tự định vị project theo `__file__`. Build/format có dependency khai báo trong [BUILD_TEST](../docs/BUILD_TEST.md); generate/check/package dùng thư viện chuẩn Python. Chỉ diagnostic mở kết nối bo khi người vận hành yêu cầu. Log/test output nằm trong `test_build/`, binary trong `dist/`.
