# Sketch TunnelESP32

`TunnelESP32.ino` chỉ gọi setup/loop của [app](src/app/README.md). Mọi implementation nằm dưới [src](src/README.md), Arduino compile đệ quy. Include EspUsbReaderHost ở sketch để Arduino phát hiện thư viện USB vendored.

Build bằng [tools/build.py](../../tools/build.py), đọc [hướng dẫn](../../docs/BUILD_TEST.md). Không mở từng file `.cpp` như một sketch mới. Target ESP32-S3 flash8MB, core3.3.8; pin và thuật toán được giữ theo bo hiện tại.
