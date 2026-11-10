# Examples

Cài SDK trước; chạy tại thư mục gốc của bộ SDK:

```console
python example/read_tcp.py --host 192.168.1.211
python example/read_rtu.py --port COM7 --baud 19200 --parity E
python example/read_mqtt.py --host broker.example.com --root factory/tunnel01/v1
```

Ba script trên chỉ đọc. MQTT credentials lấy từ `TUNNEL_MQTT_USERNAME` và
`TUNNEL_MQTT_PASSWORD`. Thay địa chỉ, port và COM bằng cấu hình thật.

Khi máy rảnh, chủ động thực hiện phiên claim/maintenance/đọc công suất:

```console
python example/reader_power.py --transport tcp --host 192.168.1.211
python example/reader_power.py --transport rtu --serial-port COM7
python example/reader_power.py --transport mqtt --host broker.example.com --root factory/tunnel01/v1
```

Chọn một lệnh phù hợp. Claim làm máy STOP/HOLD. Script duy trì heartbeat, kiểm tra
reply, thoát maintenance/release khi có thể. Không ghi công suất RF.
`--help` không kết nối thiết bị. [Toàn bộ API](../docs/API_REFERENCE.md).
