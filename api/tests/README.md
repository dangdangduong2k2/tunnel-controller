# SDK tests

`python -m unittest discover -s tests -v` kiểm tra lifecycle, 19 OP, tham số,
reader reply và failure semantics với transport giả lập. Không mở cổng COM thật.

`test_examples.py` kiểm tra console đủ19lệnh, giữ nguyên phiên khi BUSY, heartbeat
khi reader pending, timeout không lặp Set, readback mismatch, mất quyền không
claim lại, và giải mã BUSY TCP/RTU/MQTT. `test_device.py` đối chiếu19OP trên cả
ba transport. Test không gửi lệnh vào máy thật.

Trong source firmware, các bài `tests/test_api_integration.py` và
`tests/test_mqtt_on_demand.py` kiểm tra thêm với native production core và
broker localhost. Bộ SDK standalone không chứa firmware hoặc native simulator.
