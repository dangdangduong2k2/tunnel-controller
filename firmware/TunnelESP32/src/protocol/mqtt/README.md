# MQTT application API

## Trách nhiệm

MqttApi quản lý session/epoch/client/replay và gọi ModbusCore. MqttJson parse JSON có giới hạn và encode snapshot. Nhận cả numeric API và named API; chuẩn hóa về một Request.

## Giao diện và ngữ cảnh gọi

`Api::connect/disconnect/fence/process/snapshot`; `decode`, `decodeGet`, `encodeState`. Decode trả false thì không dispatch; output buffer thiếu trả false, không publish JSON cụt.

## Phụ thuộc và quy tắc thay đổi

Phụ thuộc ModbusCore, cJSON, generated OperationCatalog/Version. Không truy cập esp_mqtt_client ở lớp này. Kiểm tra session/epoch/tuổi token, không dùng clock máy tính. Không tự publish sau subscribe hoặc claim. Giữ khả năng field state bổ sung mà client cũ vẫn dùng registers.

## Kiểm thử

`mqtt_api`, `mqtt_transport`, `test_mqtt_on_demand.py`, `test_api_failures.py`.

## Source

- [MqttApi.cpp](MqttApi.cpp)
- [MqttApi.h](MqttApi.h)
- [MqttJson.cpp](MqttJson.cpp)
