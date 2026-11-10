# Modbus core

## Trách nhiệm

ModbusCore giữ map, FC03/04/06/16, đóng/mở frame RTU/TCP, owner/lease, mailbox sequence/replay và bridge19OP. ModbusConfig chỉ chứa chân W5500/SP3485.

## Giao diện và ngữ cảnh gọi

`mb::Core::tick/pdu/rtu/tcp/resetPeer/disconnect/legacyAllow`; backend callbacks truyền qua constructor. tick phải chạy trước request để xử lý lease/reader completion.

## Phụ thuộc và quy tắc thay đổi

Không phụ thuộc Arduino ở core. State thuộc control task. read không gia hạn lease; repeat mailbox cùng sequence không chạy lại. Mọi giao thức mới phải đi qua cùng instance core. Không đổi offsets/endianness khi sửa transport.

## Kiểm thử

`modbus_core`, `modbus_integration`, `protocol_sim`, `test_api_integration.py`.

## Source

- [ModbusConfig.h](ModbusConfig.h)
- [ModbusCore.cpp](ModbusCore.cpp)
- [ModbusCore.h](ModbusCore.h)
