# Ví dụ Python và JSON

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

## JSON MQTT mẫu

| File | Nội dung |
|---|---|
| [get.example.json](get.example.json) | Yêu cầu đọc `/get`, dùng từ firmware 1.11.0. |
| [state.example.json](state.example.json) | Snapshot `/state`; bản 1.12 có thêm các trường tên. |
| [ack.example.json](ack.example.json) | Kết quả nhận lệnh `/ack`. |
| [commands.example.json](commands.example.json) | Payload lệnh numeric OP/args, dùng từ firmware 1.11.0. |
| [named.example.json](named.example.json) | Payload method/params; chỉ dùng với firmware 1.12.0 trở lên. |

Token, epoch và ms trong các file là dữ liệu minh họa. Subscribe state/ack trước,
rồi gửi `/get` để lấy token mới; tăng id cho từng yêu cầu mới, đặt retain=false.
Chỉ publish object payload của lệnh cần dùng, không gửi cả danh sách ví dụ.
Đọc/cài antenna cần claim và maintenance; theo dõi reply đúng sequence, duy trì
heartbeat trong lúc chờ. Thư mục này chứa chung script chạy được và JSON tham khảo.
