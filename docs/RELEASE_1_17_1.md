# 1.17.1 — Scan STOP bấm lặp vẫn dừng

## Nguyên nhân và thay đổi

Hai nút riêng START và STOP trên trang Home của DWIN đều được dựng với VP `0x1020`, giá trị `1`. Firmware cũ hiểu giá trị đó là đảo trạng thái, vì vậy STOP lúc đã dừng lại trở thành START. Video người dùng ngày 2026-11-25 xác định nút đang bấm thuộc ô Scan, không phải Emergency.

Firmware 1.17.1 (IR1 = **1171**) nhận thêm hai giá trị tuyệt đối trên cùng VP:

| Nút | Value | Khung DGUS tự gửi, không CRC | Tác động khi bấm lặp |
|---|---:|---|---|
| START | 2 | `5A A5 06 83 10 20 01 00 02` | Giữ phiên scan đang chạy, không xóa EPC |
| STOP | 3 | `5A A5 06 83 10 20 01 00 03` | Dừng/giữ dừng, không tạo phiên scan |

Value `1` giữ nghĩa toggle cho màn cũ một nút. Value `0` và giá trị không nhận diện không khởi động scan. Scan độc lập không chạy cửa/băng tải; các điều kiện BUSY, điều khiển từ xa và Emergency vẫn được giữ. STOP này chỉ điều khiển scan thủ công; không dùng thay Emergency để dừng cả chu trình Auto/Semi.

## Cần cập nhật cả ESP32 và DWIN

1. Nạp app ESP32 **1.17.1** bằng OTA/USB theo phân vùng app đã xác minh; file `app.bin` không phải merged image và không ghi tại offset 0.
2. Cập nhật file touch DWIN **font13.bin** từ gói sửa Scan. Firmware riêng không phân biệt được hai nút khi màn vẫn gửi cùng value 1.
3. Gói DWIN chỉ thay đổi **hai byte** so với gói v2 trước sửa: START 1→2 và STOP 1→3. Tất cả font/ảnh/trang/VP giữ nguyên. File 13 không phải firmware ESP32 và không được tải vào trang OTA ESP32.

Thứ tự nên là ESP32 trước, DWIN sau. Nếu hạ ESP32 về trước 1.17.1 mà giữ màn mới, các nút Scan value 2/3 sẽ bị bỏ qua; cần dùng cặp firmware/gói màn tương thích.

## Nạp màn bằng USB–TTL

Gói ZIP bàn giao chứa `DWIN/source/deploy.py`, `dgus.py`, `package/` và bản `rollback/` của gói v2 trước sửa. Tool mặc định chỉ kiểm tra, không nạp. Tắt nguồn công suất motor/servo, tách UART màn khỏi ESP32 trước khi nối USB–TTL; không đấu chung TX của ESP32 và USB–TTL. Giữ nguồn màn phù hợp. Đóng Hercules/phần mềm đang giữ COM.

```powershell
python -m pip install pyserial
python DWIN/source/deploy.py --port COMxx
python DWIN/source/deploy.py --port COMxx --write
```

Thay COMxx bằng cổng USB–TTL thật của màn; UART 115200, 8N1. Nếu màn đúng gói v2 trước sửa, tool chỉ cần ghi **một block 32 KiB của slot 13**, kiểm tra CRC rồi khởi động lại màn. Nếu preflight báo dữ liệu không khớp, dừng lại để xác định gói đang nạp, không ép ghi. `--simulate` chạy trên panel giả, không mở COM. Nạp xong tắt nguồn trước khi nối lại UART màn vào P1.

## Kiểm thử

Regression dùng parser UART/DWIN, state machine và reader production với phần cứng giả: STOP hai lần khi idle; START lặp trong phiên; STOP liên tiếp trong lúc dừng và sau khi dừng xong; START mới sau STOP; Emergency chặn START. Kiểm tra số phiên scan, EPC không bị xóa bởi START lặp, không motor nào chạy.

Kiểm tra binary panel độc lập: đúng hai touch START/STOP, value 2/3 tại trang Home; so sánh toàn bộ slot chỉ khác hai byte của font13. Full native regression và HMI layout/codegen được chạy lại. Chưa nạp/thử bản sửa trên phần cứng trong lượt này.

Build Arduino core 3.3.8 đạt, kiểm tra identity target `tunnel-esp32s3-v1`, version 1171. SHA-256 app: `1ecaa432e4807319ecbf384adb53dba22bc1c318295f3d8ec723288ac8f47ec6`. Mô phỏng bộ nạp trong đúng gói bàn giao đạt: chỉ slot 13/block 0 được ghi, CRC `ebdb`, giá trị khởi động và trang 1 được xác minh. Hai byte thay đổi nằm tại offset 341 (1→2) và 373 (1→3).

Sau khi nạp cả hai phần, thử START → STOP → STOP → STOP: số EPC giữ lại, đèn scan không bật lại. Sau đó START → START: scan tiếp tục cùng phiên, không đưa số EPC về 0. Thử Emergency theo quy trình máy hiện có.

Thang tốc độ và sửa Antennas của [1.17.0](RELEASE_1_17_0.md) được giữ nguyên.
