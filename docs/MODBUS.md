# Modbus TCP / RTU

## Kết nối và Function Code

Mặc định nhà máy: TCP port 502, Ethernet DHCP; RTU ID1, 19200 baud, 8E1. Cấu hình đã lưu trên bo được ưu tiên; đọc màn Kết nối hoặc web để biết giá trị thật. TCP dùng cùng Unit ID đã cấu hình với RTU, không phải gateway chuyển tiếp RS485.

Hỗ trợ FC03 Read Holding Registers, FC04 Read Input Registers, FC06 Write Single Register, FC16 (0x10) Write Multiple Registers. Không hỗ trợ coil/discrete input/FC23, không nhận broadcast RTU ID0. Địa chỉ dưới đây là **offset thập phân bắt đầu từ 0**. Nếu công cụ dùng ký hiệu 3xxxx/4xxxx thì IR0=30001, HR256=40257. Không cộng 30001/40001 vào frame.

Mỗi thanh ghi unsigned 16-bit big-endian. Số 32-bit: thanh ghi cao trước, `value=(HI<<16)|LO`. Riêng CRC RTU gửi low byte trước. Snapshot MQTT/SDK96 thanh ghi là IR0..95; IR96..99 đọc riêng nếu cần baud32/parity/stop bits.

## Bảng đọc FC04

| Offset | Nội dung | Giá trị / tác động |
|---:|---|---|
| 0 | Dự phòng tương thích nội bộ | Không cần dùng trong vận hành; vẫn giữ offset để client cũ không lệch |
| 1 | Firmware | 1120 = 1.12.0 |
| 2 | Machine state | [STATES](STATES.md) |
| 3 | Fault | 0 bình thường; bảng fault trong STATES |
| 4 | Mode | 0 Auto, 1 Semi |
| 5 | Input bits | bit0..6 = mức GPIO IN1..IN7, không tự diễn giải cơ khí |
| 6 | Activity bits | bit0 cửa1, bit1 cửa2, bit2 băng tải đang chạy; bit3 scan active; bit4 maintenance; bit5 EN released |
| 7 | Stop reason | Bảng STOP trong STATES |
| 8 | Stop flags | bit0 stop latch; bit1 homed; bit2 released; bit3 PC supervision; bit4 HMI locked |
| 9 | Speed level | 1..5 |
| 10 | Số EPC duy nhất | Trong phiên scan hiện tại, không phải số lần đọc |
| 11..12 | Tổng lượt đọc | uint32 HI/LO |
| 13..14 | Thời gian scan ms | uint32 HI/LO |
| 15 | Reader flags | bit0 ready, bit1 inventory running, bit2 response pending; byte cao scan result |
| 16 | Owner | 0 none,1 RTU,2..4 TCP,5 PC,6 MQTT |
| 17 | Lease còn lại | ms; đọc không gia hạn |
| 18 | Sequence lệnh gần nhất toàn máy | uint16 |
| 19 | Result lệnh gần nhất toàn máy | Mã ứng dụng |
| 20 | Reader transaction state | 0 idle,1 pending,2 reply,3 transport error |
| 21 | Reader sequence | Ghép với sequence của lệnh reader |
| 22 | Reader transport status | 0 transport OK, khác 0 lỗi bridge/timeout/USB; không thay thế status trong payload |
| 23 | Số byte reply | 0..64 |
| 24 | Ethernet flags | bit0 initialized, bit1 link, bit2 has IP |
| 25..26 | IPv4 | Hai octet/thanh ghi: 192.168.1.211 = 0xC0A8,0x01D3 |
| 27 | Unit ID | 1..247 |
| 28 | Baud low16 cũ | Dùng 96..97 nếu baud lớn hơn 65535 |
| 29 | RTU ready | 0/1 |
| 30..31 | Uptime ms | uint32 HI/LO, wrap khoảng 49.7 ngày |
| 32..95 | Reader reply | Mỗi thanh ghi một byte, chỉ 23 byte-count đầu có nghĩa |
| 96..97 | Baud đầy đủ | uint32 HI/LO |
| 98 | Parity | 0 None,1 Even,2 Odd |
| 99 | Stop bits | 1 hoặc 2 |

## Bảng ghi/đọc FC03,06,16

| Offset | Chức năng | Ghi gì / tác động |
|---:|---|---|
| 256 | Claim / heartbeat / release | 1 claim hoặc gia hạn; 0 release. Đọc =1 nếu chính peer đang sở hữu |
| 257 | Emergency | Ghi 1; giá trị đọc luôn 0 |
| 258 | STOP | Ghi 1; giá trị đọc luôn 0 |
| 272 | Sequence | Chỉ ghi cùng toàn bộ mailbox |
| 273 | OP | Theo [COMMANDS](COMMANDS.md) |
| 274..279 | ARG0..ARG5 | uint16; ô không dùng bằng 0 |
| 280 | Result của peer này | Chỉ đọc; kết quả ứng dụng lần ghi mailbox gần nhất |

Ghi mailbox **một FC16 duy nhất**, start272/count8. Không ghi OP hay ARG từng thanh ghi bằng FC06. Sequence đầu tiên của một peer mới là1; tiếp theo +1 modulo65536. Đọc HR272 lấy sequence của peer, không dùng IR18 của client khác. Gửi lại cùng sequence và cùng8word trả kết quả cũ, không chạy lại và không gia hạn lease; cùng sequence khác nội dung bị exception03. Peer TCP mới reset lịch sử riêng; RTU giữ lịch sử tới khi bo khởi động lại.

## Frame mẫu chỉ đọc trạng thái

Đọc IR0..31, Unit1:

```text
TCP: 00 01 00 00 00 06 01 04 00 00 00 20
     TID   PID   Length U  FC Start Count
RTU: 01 04 00 00 00 20 F1 D2
     U  FC Start Count CRC-lo CRC-hi
```

TCP response: MBAP giữ Transaction ID, Protocol ID0, length là số byte từ UnitID đến hết PDU; PDU `04 40` +64byte data. RTU response: Unit1 +PDU giống TCP +CRC16. Không thêm CRC vào TCP.

Ví dụ mailbox speed level3, sequence1: FC16 start0x0110 count8, words `[1,8,3,0,0,0,0,0]`. Chỉ có tác dụng khi đã claim và máy thỏa điều kiện thay đổi tốc độ; đọc HR280 sau FC16. Chức năng cụ thể và điều kiện tại [API](API.md).

## Exception

FC phản hồi = FC yêu cầu |0x80, kèm một byte mã: 01 function; 02 address/mailbox shape; 03 value/length/sequence; 04 backend failure; 06 busy/not owner; TCP0B sai UnitID. RTU sai CRC/sai UnitID/broadcast bị bỏ qua không phản hồi. TCP sai MBAP/frame có thể bị đóng hoặc bỏ qua. Frame chưa đủ bị timeout; client không nên gửi lệnh điều khiển đồng thời trên cùng một kết nối.

## Modbus Poll

Connection → Modbus TCP/IP, nhập IP thực tế của bo/port502/SlaveID thực tế. Read/Write Definition: FC04, Address0, Quantity32, scan500ms. Bắt đầu bằng thao tác đọc; `Tx` tăng, `Err=0`, IR1=1120 xác nhận đang dùng bản này. RTU chọn USB-RS485, đúng COM/baud/parity/stopbits và ID; giữ nguyên bảng trên.
