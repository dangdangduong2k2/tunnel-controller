# PC UART tương thích

## Trách nhiệm

pc_control giữ binary PC protocol và reader bridge cũ. SharedUart tách frame DWIN khỏi ASCII @TUN:<HEX> newline của PC, phục vụ J5 cùng UART với DWIN.

## Giao diện và ngữ cảnh gọi

`PcControl_Init/ReceivePacket/ReceiveByte/Process/ProcessEmergency/DiscardInput`; `SharedUart_Init/ReceiveByte/Encode/Fence`.

## Phụ thuộc và quy tắc thay đổi

Phụ thuộc control/reader/HMI và runtime. Receive chỉ enqueue; Emergency có đường ưu tiên, không chạy lệnh thường trong ISR. PC owner status có thể gia hạn legacy lease. ASCII framing không giải quyết xung đột điện giữa TX màn và CH340.

## Kiểm thử

`integration`, `dual`, `shared_uart`, `modbus_integration`.

## Source

- [pc_control.c](pc_control.c)
- [pc_control.h](pc_control.h)
- [SharedUart.c](SharedUart.c)
- [SharedUart.h](SharedUart.h)
