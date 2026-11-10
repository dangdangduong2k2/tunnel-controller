# Trạng thái và mã lỗi

Số enum giữ tương thích1.11.0. Những trạng thái midpoint cũ vẫn có tên nhưng không nhất thiết được đi qua trong chu trình hiện tại.

## Machine state

| Giá trị | Tên trong firmware |
|---:|---|
| 0 | `TUNNEL_WAIT_SS1_LOW` |
| 1 | `TUNNEL_CONVEYOR_TO_SS1_HIGH` |
| 2 | `TUNNEL_CLOSE_STEP_TO_SS2` |
| 3 | `TUNNEL_WAIT_READER` |
| 4 | `TUNNEL_CONVEYOR_INVENTORY_TO_SS3` |
| 5 | `TUNNEL_DISPLAY_RESULT` |
| 6 | `TUNNEL_OPEN_STEP` |
| 7 | `TUNNEL_WAIT_REARM` |
| 8 | `TUNNEL_FAULT` |
| 9 | `TUNNEL_WAIT_CONFIRM` |
| 10 | `TUNNEL_RESCAN_TO_SS1` |
| 11 | `TUNNEL_RESCAN_WAIT_READER` |
| 12 | `TUNNEL_RESCAN_TO_SS3` |
| 13 | `TUNNEL_RETURN_GATE_TO_SS2` |
| 14 | `TUNNEL_STOPPED` |
| 15 | `TUNNEL_MANUAL` |
| 16 | `TUNNEL_MODE_OPEN_GATE` |
| 17 | `TUNNEL_STARTUP_WAIT` |
| 18 | `TUNNEL_CENTER_REVERSE_WAIT` |
| 19 | `TUNNEL_CENTER_REVERSE` |
| 20 | `TUNNEL_CENTER_INVENTORY` |
| 21 | `TUNNEL_CENTER_FORWARD_TO_SS3` |
| 22 | `TUNNEL_SS3_TIMEOUT_DRAIN` |
| 23 | `TUNNEL_WAIT_INVENTORY_START` |

## Fault

| Giá trị | Tên trong firmware |
|---:|---|
| 0 | `TUNNEL_FAULT_NONE` |
| 1 | `TUNNEL_FAULT_MOTOR_START` |
| 2 | `TUNNEL_FAULT_STEP_CLOSE_TIMEOUT` |
| 3 | `TUNNEL_FAULT_STEP_OPEN_COUNT` |
| 4 | `TUNNEL_FAULT_STEP_OPEN_TIMEOUT` |
| 5 | `TUNNEL_FAULT_CONVEYOR_TIMEOUT` |
| 6 | `TUNNEL_FAULT_READER_NOT_READY` |
| 7 | `TUNNEL_FAULT_SCAN_ERROR` |
| 8 | `TUNNEL_FAULT_EPC_CAPACITY` |
| 9 | `TUNNEL_FAULT_HMI` |
| 10 | `TUNNEL_FAULT_SENSOR_ORDER` |
| 11 | `TUNNEL_FAULT_INVALID_STATE` |
| 12 | `TUNNEL_FAULT_RESCAN_TIMEOUT` |
| 13 | `TUNNEL_FAULT_HOME_LIMIT` |

## Scan result

| Giá trị | Tên trong firmware |
|---:|---|
| 0 | `ZK7182_SCAN_RESULT_IDLE` |
| 1 | `ZK7182_SCAN_RESULT_IN_PROGRESS` |
| 2 | `ZK7182_SCAN_RESULT_OK` |
| 3 | `ZK7182_SCAN_RESULT_TRANSPORT_LOST` |
| 4 | `ZK7182_SCAN_RESULT_NO_RESPONSE` |
| 5 | `ZK7182_SCAN_RESULT_READER_ERROR` |
| 6 | `ZK7182_SCAN_RESULT_PROTOCOL_ERROR` |
| 7 | `ZK7182_SCAN_RESULT_CAPACITY_OVERFLOW` |
| 8 | `ZK7182_SCAN_RESULT_INCOMPLETE_ANTENNAS` |

## Stop reason

| Mã | Nghĩa |
|---:|---|
| 0 | Không STOP |
| 1 | Emergency; nhả EN |
| 2 | Lỗi UART |
| 3 | PC packet lỗi |
| 4 | RX overflow |
| 5 | TX queue đầy |
| 6 | PC/control heartbeat timeout |
| 7 | Control loop watchdog |
| 8 | Motor backend lỗi |
| 9 | Khởi tạo lỗi |
| 10 | Disconnect/release/STOP giao thức |
| 11 | Takeover/claim |

STOP khác Emergency giữ EN/HOLD. Cờ stopped không đồng nghĩa đã mất nguồn công suất. Sau claim/timeout/reconnect không tự Reset; Reset là lệnh có thể chạy homing.

## Chu trình chính

Startup/homing → chờ kiện IN1 → băng tải đưa kiện → đóng hai cửa theo limit riêng IN2/IN7 → chờ reader → inventory khi băng tải chạy tới IN3 → hoàn tất/hiển thị kết quả. Auto mở cửa/ra kiện và rearm; Semi chờ Confirm hoặc Rescan. Rescan đảo băng tải theo state machine và có ngân sách scan mới. Bất kỳ fault/STOP phù hợp đều chốt dừng; không nhảy thẳng sang trạng thái chạy khi điều kiện lỗi vừa biến mất.

IN7 xác nhận cửa2 đã đóng; dropout sau xác nhận có hành vi riêng được test, không được đồng nhất với liên động IN2. Chi tiết thực thi tại [tunnel.c](../firmware/TunnelESP32/src/control/tunnel.c) và các suite two_doors/in7_confirmation.
