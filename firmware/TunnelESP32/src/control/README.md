# Điều khiển máy

## Trách nhiệm

tunnel.c sở hữu state machine Auto/Semi/Manual, hai cửa, homing, thời gian scan, Confirm/Rescan, liên động và STOP. motor_profile tính lịch xung; dwin_control giữ ngữ nghĩa nút màn. motor_pwm.h và speed_store.h là interface tới backend ESP32.

## Giao diện và ngữ cảnh gọi

`Tunnel_Init/Process/Tick1ms`, `Tunnel_RemoteCommand`, các getter trạng thái; `MotorProfile_*`, `MotorPwm_*`, `SpeedStore_Load/Save`. Public signature tại header cùng tên.

## Phụ thuộc và quy tắc thay đổi

Phụ thuộc reader/HMI và interface platform; không gọi socket/MQTT. State chỉ đổi ở control task; ISR chỉ báo/dừng backend. Giữ nguyên enum wire, independent limit IN2/IN7, tổng6000/5100xung, timeout và stop latch. Scan thường không ghi cấu hình reader. Các trạng thái cũ vẫn giữ giá trị enum để không lệch API.

## Kiểm thử

`profile`, `cycle`, `reader_cycle`, `two_doors`, `in7_confirmation`, `dwin_manual`, `esp_step`, `esp_outputs`.

## Source

- [dwin_control.c](dwin_control.c)
- [dwin_control.h](dwin_control.h)
- [motor_profile.c](motor_profile.c)
- [motor_profile.h](motor_profile.h)
- [motor_pwm.h](motor_pwm.h)
- [speed_store.h](speed_store.h)
- [tunnel.c](tunnel.c)
- [tunnel.h](tunnel.h)
