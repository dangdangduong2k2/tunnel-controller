# Màn hình DWIN

## Trách nhiệm

hmi.c parse DGUS có/không CRC, phân biệt ACK/touch, queue event, chống lặp và nhận Emergency. Sự kiện giao diện HMI v2 (VP0x1090) đi hàng đợi riêng `Hmi_PopUiEvent`, không lọc lặp, không tới máy. HmiUi (C++ thuần, không Arduino) giữ trang/trình sửa/bản nháp/hộp thoại/toast/lịch sử và gửi text+word không chặn; HmiLayout.h sinh từ gói màn. HmiNetwork là publisher12ô của giao diện cũ, còn giữ file nhưng firmware không gọi nữa.

## Giao diện và ngữ cảnh gọi

Từ 1.17.1, Scan VP0x1020 nhận 2=START, 3=STOP (idempotent), 1=toggle legacy. Gói HMI v2 phải nạp lại file touch 13 để hai nút gửi 2/3; đổi firmware một mình không sửa được hai nút cùng gửi 1. `dwin_manual` kiểm tra lặp START/STOP qua parser và reader production.

`Hmi_Init/ReceiveByte/ReceiveError/Process`, `PopEvent`, `PopUiEvent`, `GetLastPage`, `SendWord/SendText/SendPage/SendQuantity`, `hmiui::Ui::tick/event/flush` với `hmiui::Host`. Hmi_SendText yêu cầu số byte chẵn2..128, kết thúc FF FF và padding0.

## Phụ thuộc và quy tắc thay đổi

Phụ thuộc runtimeUART, DeviceConfig, control chỉ qua event/lock. Không thực thi motor trực tiếp khi parse nút thường. VP Quantity0x2000; Emergency0x1040. HmiLayout.h và contracts/hmi_fields.json sinh bằng tools/generate_hmi.py, phải đồng bộ với gói màn đã nạp; source ESP32 không chứa bộ ảnh/DGUS flash cho màn.

## Kiểm thử

1.17.2: `DwinPatch` chạy với Port/Journal, verify fingerprint/RAM/flash, restart và phục hồi boot sau; `DwinScanPatchData.h` do `tools/generate_dwin_patch.py` sinh, không sửa tay. Không chạy cùng publisher HMI. Toast hết hạn xóa text và repaint cùng trang sau khi flush biến; thay dài→ngắn cũng repaint.

Từ 1.17.0: [AntennaSettings](AntennaSettings.h) là job C++ độc lập I/O, ghi power/mask rồi xác minh readback; không retry ghi. `EspHmiAntenna` cấp quyền độc quyền reader và nhả sau khi drain phản hồi; Ui chỉ gửi yêu cầu đã xác nhận. Hai suite `antenna_settings`, `esp_hmi_antenna` và ca Antennas trong `hmi_ui` kiểm tra lỗi/STOP/timeout/BUSY.

`hmi`, `esp_hmi`, `hmi_ui_events`, `hmi_ui`, `hmi_network`, `dwin_manual`, `shared_uart`.

## Source

- [hmi.c](hmi.c)
- [hmi.h](hmi.h)
- [HmiNetwork.cpp](HmiNetwork.cpp)
- [HmiNetwork.h](HmiNetwork.h)
- [HmiUi.cpp](HmiUi.cpp)
- [HmiUi.h](HmiUi.h)
- [HmiLayout.h](HmiLayout.h)
