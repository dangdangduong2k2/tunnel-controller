# Màn hình DWIN

## Trách nhiệm

hmi.c parse DGUS có/không CRC, phân biệt ACK/touch, queue event, chống lặp và nhận Emergency. HmiNetwork cập nhật12ô text IP/subnet/gateway/DHCP/TCP/RTU/MQTT mỗi100ms một ô.

## Giao diện và ngữ cảnh gọi

`Hmi_Init/ReceiveByte/ReceiveError/Process`, `PopEvent`, `SendWord/SendText/SendPage/SendQuantity`, `HmiNetworkPublisher::poll`. Hmi_SendText yêu cầu số byte chẵn2..128, kết thúc FF FF và padding0.

## Phụ thuộc và quy tắc thay đổi

Phụ thuộc runtimeUART, DeviceConfig, control chỉ qua event/lock. Không thực thi motor trực tiếp khi parse nút thường. VP Quantity0x2000; Emergency0x1040. Map network trong contracts/hmi_fields.json phải đồng bộ với project màn đã nạp; source ESP32 không chứa bộ ảnh/DGUS flash cho màn.

## Kiểm thử

`hmi`, `esp_hmi`, `hmi_network`, `dwin_manual`, `shared_uart`.

## Source

- [hmi.c](hmi.c)
- [hmi.h](hmi.h)
- [HmiNetwork.cpp](HmiNetwork.cpp)
- [HmiNetwork.h](HmiNetwork.h)
