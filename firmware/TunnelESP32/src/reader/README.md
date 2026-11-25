# Reader ZK-7182M

## Trách nhiệm

zk7182.c parse stream từ USB CP210x, CRC, ACK inventory, EPC duy nhất/tổng lượt đọc, pause/resume, timeout/reconnect và bridge cấu hình có allowlist.

## Giao diện và ngữ cảnh gọi

`ZK7182_Init/Process/TransportReady/TransportLost`, `StartScan/PauseScan/ResumeScan/CancelScan`, `BeginPcCommand/TakePcReply`, getter count/status. TakePcReply trả command/status/payload tối đa64byte, transport status tách riêng.

## Phụ thuộc và quy tắc thay đổi

Phụ thuộc app_config, USB adapter và debug state; không tự claim hay gửi MQTT. Một transaction cấu hình tại một thời điểm, bridge chỉ được coordinator cho dùng trong maintenance. Boot/inventory thường chỉ START0x50 TargetA và STOP0x51, không ghi lại power/mask/profile.

## Kiểm thử

`reader_frequency`, `reader_cycle`, `integration`, `modbus_integration`.

Từ 1.16.4, bảng chống trùng được cấp phát theo các khối 32 EPC trước START,
đủ toàn bộ 1.024 tag × tối đa 64 byte. Nếu cấp phát thiếu, StartScan trả thất bại,
không phát RF START và không xóa số đếm trước đó. Pause/resume và phục hồi trong
cùng phiên giữ nguyên bảng. Khi phiên đã kết thúc/hủy, Process giải phóng bảng
để HTTPS OTA dùng RAM; số EPC, tổng lượt đọc, EPC cuối và lịch sử không bị xóa.
Init/StartScan/Process phải chạy trên task chủ sở hữu, không gọi từ ISR;
pause/cancel chỉ đổi cờ, không cấp phát/giải phóng trong ISR.
Test bao gồm thiếu bộ nhớ tại từng khối, giải phóng sau timeout/mất USB,
giữ số đếm và từ chối bản tin đến muộn sau khi giải phóng.

## Source

- [zk7182.c](zk7182.c)
- [zk7182.h](zk7182.h)
