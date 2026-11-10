# Changelog

## 1.12.0

- Chuẩn hóa layout application/control/reader/HMI/protocol/platform/config/common; tách EspRuntime và modular Python SDK.
- Thêm contract sinh version/catalog/documentation; web source duy nhất, không duy trì hai bản HTML khác nhau.
- MQTT thêm method/params và state có tên, tương thích numeric1.11; giữ trả lời theo `/get`.
- Giữ toàn bộ hành vi chu trình/STOP/arbitration/NVS; format C/C++/Python, cập nhật test harness hiện hành.
- Có README cho từng thành phần, API chung ba giao thức, build/test/nạp/rollback và giới hạn kiểm chứng.

## Mốc 1.11.0

Thêm MQTT đọc theo yêu cầu `/get`→`/state`, correlation client/id; giữ Modbus TCP/RTU và DWIN network fields. Đây là mốc regression của refactor1.12.0.
