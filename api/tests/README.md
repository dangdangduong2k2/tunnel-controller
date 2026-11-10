# SDK tests

`python -m unittest discover -s tests -v` kiểm tra lifecycle, 19 OP, tham số,
reader reply và failure semantics với transport giả lập. Không mở cổng COM thật.

Trong source firmware, các bài `tests/test_api_integration.py` và
`tests/test_mqtt_on_demand.py` kiểm tra thêm với native production core và
broker localhost. Bộ SDK standalone không chứa firmware hoặc native simulator.
