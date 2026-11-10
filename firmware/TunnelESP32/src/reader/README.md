# Reader ZK-7182M

## Trách nhiệm

zk7182.c parse stream từ USB CP210x, CRC, ACK inventory, EPC duy nhất/tổng lượt đọc, pause/resume, timeout/reconnect và bridge cấu hình có allowlist.

## Giao diện và ngữ cảnh gọi

`ZK7182_Init/Process/TransportReady/TransportLost`, `StartScan/PauseScan/ResumeScan/CancelScan`, `BeginPcCommand/TakePcReply`, getter count/status. TakePcReply trả command/status/payload tối đa64byte, transport status tách riêng.

## Phụ thuộc và quy tắc thay đổi

Phụ thuộc app_config, USB adapter và debug state; không tự claim hay gửi MQTT. Một transaction cấu hình tại một thời điểm, bridge chỉ được coordinator cho dùng trong maintenance. Boot/inventory thường chỉ START0x50 TargetA và STOP0x51, không ghi lại power/mask/profile.

## Kiểm thử

`reader_frequency`, `reader_cycle`, `integration`, `modbus_integration`.

## Source

- [zk7182.c](zk7182.c)
- [zk7182.h](zk7182.h)
