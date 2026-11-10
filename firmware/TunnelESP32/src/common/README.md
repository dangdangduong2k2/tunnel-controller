# Hợp đồng chung

## Trách nhiệm

Version.h và OperationCatalog.h sinh từ contracts. ProtocolLimits.h giữ lease/token/heartbeat dùng chung C/C++. StopReason.h giữ mã stop ổn định; debug_state.h là cấu trúc chẩn đoán.

## Giao diện và ngữ cảnh gọi

Macro phiên bản/timing, enum STOP và namespace tunnelcontract. Không có task, heap hoặc IO.

## Phụ thuộc và quy tắc thay đổi

Không sửa generated header bằng tay; chạy tools/generate.py. Không đánh lại số STOP hoặc version map khi chỉ thay layout source. Thay giới hạn lease phải xét cả core và state machine.

## Kiểm thử

`mqtt_api`, `modbus_core`, `tools/generate.py --check`.

## Source

- [debug_state.h](debug_state.h)
- [OperationCatalog.h](OperationCatalog.h)
- [ProtocolLimits.h](ProtocolLimits.h)
- [StopReason.h](StopReason.h)
- [Version.h](Version.h)
