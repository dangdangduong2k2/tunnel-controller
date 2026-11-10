# Chi tiết cấu hình reader

Phạm vi là các lệnh đã được firmware cho phép, không phải toàn bộ SDK ZK. Xem [COMMANDS](COMMANDS.md) để biết OP và tên tham số. Các giá trị dưới đây mô tả bộ kiểm tra hiện có tại [zk7182.c](../firmware/TunnelESP32/src/reader/zk7182.c); model/firmware reader vẫn có thể trả status không hỗ trợ.

## Điều kiện và kết quả

Claim → OP9 maintenance=1 → OP reader → chờ IR20 khác1 và IR21 khớp sequence → kiểm tra IR22 → đọc số byte IR23 từ IR32 → kiểm tra byte reader status → OP9=0 → release. Gửi heartbeat trong lúc chờ. Timeout command reader mặc định2000ms; SDK ví dụ cho phép thời gian chờ lớn hơn để đọc kết quả.

Transport status IR22:0 nhận được reply,4 mất USB/transport,5 lỗi/độ dài reply không hợp lệ,6 timeout. Reader status là byte thứ2 của reply (IR33), không gộp với IR22. Byte đầu IR32 là command reader, ví dụ0x94. Nếu params vượt kiểm tra core thì result1; nếu bị reader bridge từ chối (allowlist/readiness/transaction) có thể result2 và không có transaction mới.

## Công suất và antenna

OP21 trả `[0x94,status,ANT1,ANT2,ANT3,ANT4]` khi thành công. Công suất đơn vị dBm. OP22 đặt cả4 cổng,0..30dBm mỗi cổng. `ram_only=1` đặt bit7 của từng byte gửi ZK, chỉ RAM;0 yêu cầu lưu reader. Không tự ghi lặp mỗi lần boot/scan. Set ACK chưa thay cho việc Get đọc lại xác nhận.

OP23 mask1..15: bit0 ANT1, bit1 ANT2, bit2 ANT3, bit3 ANT4;0 bị từ chối. `mask=5` bật ANT1 và ANT3. `ram_only` giống power. Đọc thông tin/mask qua OP20; không có OP riêng `antenna.mask.get` trong contract hiện tại. Reply OP20 được chuyển nguyên payload do reader trả, không tự đổi schema theo model.

## RF profile

OP24/25 dùng ZK0x7F với1byte. `encoded` của OP25 phải có bit7=1;6bit thấp là profile. Các giá trị6bit thấp được cho phép:0,1,2,3,5,7,11,12,13,15,50,51,52,53. Bit6 được chuyển nguyên sang reader; API không diễn giải nó thành một cài đặt mới. Ví dụ0xCD được test với profile13. Không xem mọi số128..255 là hợp lệ ở backend, dù đây là khoảng của trường JSON.

OP26/27 dùng ZK0x7F với3byte: action0 Get +0000, action1 Set +ID big-endian. Các ID Gen2X cho phép:

```text
1 3 5 7 11 12 13 15 102 103 104 120 123 124 125 126
141 146 147 148 185 202 203 205 222 223 224 225 226
241 244 285 302 323 324 325 326 342 343 344 345 382
4123 4124 4141 4146 4148 4185 5123 5124 5141 5146 5148 5185
```

Không gán tên chế độ RF cho ID khi chưa đối chiếu manual đúng model reader; đây là ID được bridge cho đi qua. OP28 gửi read-only Ex10 version `EC 00`, reply giữ nguyên byte từ reader.

## Return loss

OP29 nhận tần số **kHz** dạng32bit chia `frequency_hi`, `frequency_lo`; `frequency_khz=(hi<<16)|lo`. Firmware cho phép840000..960000kHz và số phải chia hết cho125 hoặc100. `antenna=0..3` tương ứng ANT1..ANT4.

Ví dụ840.125MHz, ANT4: `frequency_hi=12`, `frequency_lo=53693` (0xD1BD), `antenna=3`. Backend gửi `91 00 0C D1 BD 03`; CRC/length reader do driver đóng. Test host xác nhận các byte này; đáp ứng RF thật cần reader/antenna thực tế.

API hiện tại trả EPC **count** và tổng lượt đọc qua state; không có topic/holding register stream danh sách từng EPC đầy đủ trong bộ contract này.
