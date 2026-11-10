# Phần cứng và chân kết nối

Target hiện tại: ESP32-S3, flash8MB, không PSRAM. Pin dưới đây lấy từ BoardProfile/ModbusConfig hiện hành, giữ nguyên bo đã dùng; đối chiếu nhãn connector thực tế khi đi dây. Không tự chuyển pin trong lúc refactor.

| Chức năng | GPIO ESP32 |
|---|---|
| Cửa1 EN/DIR/PUL | 9 /10 /48 |
| Cửa2 EN/DIR/PUL | 7 /15 /8 |
| Cửa1 limit IN2 | 39 |
| Cửa2 limit IN7 | 1 |
| Băng tải DIR/PUL | 5 /6; GPIO4 EN chưa sử dụng |
| IN1..IN7 | 38,39,40,41,42,2,1 |
| DWIN UART0 RX/TX | 44 /43 |
| USB native J4 D-/D+ | 19 /20 |
| RS485 UART1 RX/TX/DE | 16 /18 /17 |
| W5500 SCK/MISO/MOSI/CS | 13 /12 /11 /14 |
| W5500 IRQ/RESET | 21 /47 |

J3/J6:1+5V,2EN,3DIR,4PUL,5GND. Nhãn P6/P3/P2 từng được đối chiếu với J6/J3/J2. STEP CLOSE logical0, OPEN1; đảo cơ khí riêng từng cửa qua SafetyConfig, không đổi nghĩa API OP2. Conveyor thuận DIR LOW, reverse HIGH ở backend hiện tại.

## DWIN / USB

DWIN UART2 phải TTL,115200,8N1, GND chung. P1 chân3 là RX ESP32 nhận TX2 màn; chân4 TX ESP32 tới RX2 màn; chân2 GND. Màn cần nguồn DC phù hợp với phần cứng; không suy ra khả năng cấp nguồn của USB chỉ từ việc ESP32 hiện COM.

J5 CH340 nối UART0 chung với DWIN; PC giao thức ASCII không phải cách ly điện. TX của CH340 và TX màn có thể tranh chấp trên RX44. Không sửa parser để che lỗi này. J4 dùng USB host cho reader CP210x; tháo reader khi dùng native USB J4 để nạp. USB host reader và native download không dùng đồng thời cùng cổng.

Source này chỉ gửi word/text VP tới giao diện DWIN đã nạp. Nó không chứa source project DGUS/ảnh/fonts của màn và không tự vẽ lại toàn bộ giao diện khi màn chưa có widget đúng VP. [hmi_fields.json](../contracts/hmi_fields.json) là map12ô kết nối; Quantity VP0x2000.

## Ethernet / RS485

RJ45 W5500 nối router/switch hoặc máy tính. Nguồn/link cần ổn định trước khi test TCP; `NO LINK` trên DWIN là chưa có link vật lý. Máy tính cùng LAN mới truy cập được IP bo. W5500 có DHCP/static/fallback, không phải Wi-Fi trên ESP32.

RS485 nối A/B và reference ground theo transceiver/adapter thực tế, dùng dây và termination phù hợp topology bus. Đây là RTU slave, không phải cổng phát lệnh master. Điều khiển DE do backend UART half duplex; không tự kéo DE bằng ứng dụng PLC. Không nối TTL UART trực tiếp vào A/B RS485.

## Phân biệt xác minh

Pin map kế thừa source/schematic của bo, host tests kiểm tra mapping đầu ra. Bản refactor cần acceptance test trên máy thật trước phát hành sản xuất: link W5500, RS485 có tải bus, USB reader reconnect, DWIN touch/text, hướng/limit/homing motor, mất mạng/nguồn và STOP. Xem [VALIDATION](VALIDATION.md).
