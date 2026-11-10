# Phạm vi bảo mật và giới hạn

Bản này có kiểm tra quyền điều khiển, sequence/replay, token phiên MQTT, giới hạn parser/queue, STOP khi mất lease, và xác thực trang cấu hình. Những cơ chế đó bảo vệ tính nhất quán điều khiển; không thay thế xác thực đường truyền.

- MQTT là TCP không TLS; web là HTTP; Modbus TCP/RTU không có xác thực ứng dụng. Dùng mạng điều khiển riêng/VPN/gateway khi đi qua mạng không tin cậy, không public port80/502 trực tiếp ra Internet.
- Broker cần ACL theo root từng bo và tài khoản riêng; ai được publish `/cmd` có thể xin quyền điều khiển/STOP. Session/client/id không phải mật khẩu.
- Password broker nằm trong NVS; không đưa dump flash/NVS hoặc file cấu hình thật vào source/ZIP. Password web được salt + SHA256 trong firmware hiện tại; không tuyên bố đây là password KDF chống brute force ngoại tuyến.
- Chưa có TLS certificate provisioning, secure OTA, secure boot/flash encryption được thiết lập bởi bộ source này, audit log bền, hoặc chứng nhận an toàn chức năng.
- DWIN Emergency qua UART và STOP mạng là chức năng phần mềm; phần cứng cắt công suất và liên động phải được thiết kế/xác minh riêng cho máy thực tế.

Kiểm thử host không xác nhận EMC, cách ly RS485, nguồn, nhiệt độ, độ bền flash hoặc tải cơ khí. Xem [VALIDATION](VALIDATION.md) để phân biệt phần đã chạy và phần cần test tại máy.
