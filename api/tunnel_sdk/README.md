# Module SDK

| File | Vai trò |
|---|---|
| [modbus.py](modbus.py) | Socket TCP/Serial RTU, framing/CRC, mailbox sequence và no-retry khi lỗi |
| [mqtt.py](mqtt.py) | Paho client, SUBACK, get/state correlation, token/ACK, named command |
| [status.py](status.py) | Decode96registers thành cùng object cho ba transport |
| [common.py](common.py) | CRC, packing args, Commands và validate named params trước IO |
| [errors.py](errors.py) | ProtocolError và UnknownOutcome |
| [operations.py](operations.py) | Catalog sinh từ contracts; không sửa tay |
| [__init__.py](__init__.py) | Public exports và from_settings |

Một connection giữ lock để serialize request. Các callback MQTT chỉ cập nhật dữ liệu/correlation dưới condition; user thread chờ timeout hữu hạn. Không nhận retained state như một snapshot mới. Sequence lệnh MQTT và ID đọc độc lập; overflow buộc tạo client mới. Python3.11, paho2.1.0 và pyserial3.5 đã pin.

Khi sửa transport chạy `test_api_failures.py`, `test_api_integration.py`, `test_mqtt_on_demand.py`; không dùng lệnh motor trên thiết bị thật làm unit test.
