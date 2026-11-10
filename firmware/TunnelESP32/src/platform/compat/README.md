# Adapter C kế thừa

## Trách nhiệm

Các tên HAL/USB STM32 giữ source thuật toán C tái sử dụng trên ESP32 và host test. Đây là compatibility ABI, không phải firmware STM32 được hỗ trợ build.

## Giao diện và ngữ cảnh gọi

`HAL_GetTick`, `HAL_GPIO_ReadPin`, callback sensor và USBH_CP210X interface; implementation thật tại EspRuntime/EspUsb. TUNNEL_HOST_TEST chọn test_hal.h.

## Phụ thuộc và quy tắc thay đổi

Không thêm điều khiển thanh ghi STM32 vào đây. Các macro IRQ của adapter production không thay lock GPTimer. Test fake GPIO/tick không mô phỏng timing điện thực tế.

## Kiểm thử

Toàn bộ suite native C/C++; build Arduino kiểm tra ABI thật.

## Source

- [board.h](board.h)
- [stm32f4xx_hal.h](stm32f4xx_hal.h)
- [test_hal.h](test_hal.h)
- [usb_host.h](usb_host.h)
- [usbh_core.h](usbh_core.h)
- [usbh_cp210x.h](usbh_cp210x.h)
