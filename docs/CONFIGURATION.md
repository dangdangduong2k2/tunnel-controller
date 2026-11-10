# Cấu hình và vận hành

Ethernet bo → router/switch; máy tính cùng LAN qua Wi-Fi hoặc cáp. Xem DWIN → Kết nối → Ethernet/IP để biết IP DHCP, hoặc xem DHCP lease của router. Truy cập `http://<ip>/`. Cắm thẳng máy tính thì cần IP máy tính cùng subnet; sau30giây không có DHCP, firmware chỉ áp fallback192.168.4.1/24 khi máy rảnh và không có owner.

Mật khẩu web mặc định khi chưa đặt là `admin`; không có username web. Lưu cấu hình lần đầu yêu cầu đặt mật khẩu quản trị riêng. Username MQTT là tài khoản do broker cấp, khác với mật khẩu quản trị bo.

## Mặc định nhà máy

| Mục | Giá trị |
|---|---|
| Ethernet | DHCP; fallback192.168.4.1/24 |
| Modbus TCP | port502, UnitID1 |
| RS485 | ID1,19200,8E1 |
| MQTT | Disabled, hostname rỗng,port1883,root `tunnel/01/v1` |
| MQTT username/password | Rỗng; điền theo broker |

Bo đã cấu hình dùng giá trị NVS, không tự reset về defaults khi cập nhật app. Mỗi bo cần topic root riêng. Nếu MQTT chưa dùng, để disabled; TCP/RTU vẫn hoạt động.

## Lưu và đổi broker

Đọc config/revision → sửa trường → Save. Không gửi password MQTT thì giữ password cũ; gửi chuỗi rỗng là xóa. Secret/hash/salt không trả lại qua API. HTTP202 chỉ nghĩa job đã nhận, UI phải poll `/api/status` đến `restarting`, `busy` hoặc `save_failed`. Cần không có owner, không motor, không scan, không reader pending; coordinator fence trước khi writer ghi flash. Sau lưu/readback thành công, bo restart sau khoảng1.5giây và khi BOOT đã thả.

NVS hai slot CRC, ghi slot còn lại rồi đọc kiểm tra, chọn revision hợp lệ mới nhất. Không erase record cũ trước khi save. `Settings` packed/schema1 giữ nguyên để tương thích; đổi struct cần migration có version, không chỉ sửa sizeof.

## API web cấu hình

| Method/path | Nội dung |
|---|---|
| GET `/` | UI tĩnh |
| POST `/api/login` | `{"password":"..."}` → token64hex |
| GET `/api/config` | Cấu hình public + revision |
| POST `/api/config` | Partial update + revision bắt buộc;202/job |
| GET `/api/status` | Link/IP,phase,job,storage_ready,can_apply,protocol readiness |
| POST `/api/logout` | Hủy token |

Ngoài trang/login, dùng `Authorization: Bearer <token>`. Một token hiện hành, hết hạn sau30phút không hoạt động; đăng nhập mới thay token cũ. Sai login5lần chờ10giây. API này phục vụ cấu hình, không có endpoint HTTP chạy motor.

Các key cấu hình hợp lệ được định nghĩa trong `cfg::parse` tại [DeviceConfig.cpp](../firmware/TunnelESP32/src/config/DeviceConfig.cpp): `revision`, `dhcp`, `ip`, `mask`, `gateway`, `dns`, `unit`, `baud`, `parity`, `stop_bits`, `tcp_port`, `mqtt_enabled`, `mqtt_host`, `mqtt_port`, `mqtt_user`, `mqtt_password`, `mqtt_root`, `admin_password`. Sai/unknown/duplicate field bị từ chối. Parity số0=None,1=Even,2=Odd; baud cho phép1200/2400/4800/9600/19200/38400/57600/115200.

## Khôi phục vật lý

Sau khi bo chạy hơn10giây, giữ BOOT/GPIO0 ít nhất5giây **khi máy rảnh**, rồi thả. Đây là thao tác reset cấu hình có chủ đích: về IP recovery192.168.4.1/24, MQTT disabled và quản trị mặc định. Không giữ BOOT qua reset vì có thể vào ROM download. Không dùng thao tác này để chữa mọi lỗi kết nối; xem nguồn/cáp/link trước.
