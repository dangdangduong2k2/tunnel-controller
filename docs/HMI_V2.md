# DWIN HMI v2 (cập nhật firmware 1.17.0)

Từ 1.17.0, trang Antennas đã hoạt động với reader thật và 5 mức tốc độ đã chia lại. Xem [hướng dẫn, giới hạn và test case](RELEASE_1_17_0.md). Các ghi nhận nạp phần cứng bên dưới thuộc phiên bản ghi rõ tại từng mục, không phải kiểm chứng phần cứng 1.17.0.

Firmware 1.15.0 đi cùng gói màn `Tunnel_HMI_DWIN_Package_v2` (1024×600, sự kiện giao diện trên VP 0x1090). Gói màn nằm ngoài repo; `tools/generate_hmi.py <thư mục gói>` sinh [HmiLayout.h](../firmware/TunnelESP32/src/hmi/HmiLayout.h) và [hmi_fields.json](../contracts/hmi_fields.json) từ `vp_map.csv`, `font_widths.csv`, `events.csv`, `manifest.json`. Đổi gói màn thì phải sinh lại và build lại.

Trạng thái kiểm chứng: logic chạy bằng test native (`hmi_ui`, `history`, `hmi_ui_events`, `finalize`, `config_service`) và firmware build được. Bản HMI v2 đầu tiên (1.14.0, nền 1.12.3) đã thử trên bo bằng cách bơm khung `5A A5 06 83 10 90 01 <mã>`: mở trang, chạm ô Unit → mở trình sửa số (trang 13), gõ phím, Done quay về trang 4; cấu hình NVS giữ nguyên. **Bản 1.15.0 (ghép lên nền 1.13.1 có FOTA) chưa được nạp và chưa thử với màn/máy thật.**

## Đã có

| Phần | Hành vi |
|---|---|
| Nút máy cũ (0x1001..0x1080) | Không đổi: cùng hàng đợi, cùng ưu tiên Emergency/Reset, cùng chu trình |
| Sự kiện giao diện (0x1090) | Hàng đợi riêng 32 mã, không lọc lặp (gõ "11" nhận đủ hai mã), không bao giờ tới máy; bị xóa khi STOP/lỗi UART |
| Trạng thái (`state`, `lock`) | 1 Ready, 2 Cycle running, 3 Remote control, 5 Verify count, 6 Emergency stop; lấy từ trạng thái máy thật |
| Ethernet / Modbus / MQTT | Hiện cấu hình đang lưu; DHCP thì hiện IP/mask/gateway/DNS đang dùng. Sửa trên màn → bản nháp → **Apply changes** → hộp thoại "Apply and restart?" → lưu NVS → bo tự khởi động lại |
| Machine | Hiện mức tốc độ băng tải đang dùng; đổi mức qua đường lệnh 0x19 sẵn có (lưu NVS, không restart) |
| History | 500 kết quả đã chốt gần nhất: `#id`, giờ `dd/mm/yyyy hh:mm:ss`, `Auto`/`Semi Auto`, số EPC; 7 dòng mỗi trang |
| Toast | Tự tắt sau khoảng 3,5 s |

Quy tắc sửa:

- Từ 1.15.1, quyền sửa tách khỏi nhãn trạng thái: máy không chuyển động/scan/chờ giao dịch reader/FOTA và đang ở Startup wait, Stopped, Manual, Wait sensor, Wait rearm hoặc Fault thì có thể sửa cài đặt, không cần Home. Nhãn Emergency/Stopped và chốt dừng vẫn giữ nguyên. Giữa chu trình hoặc chờ Confirm vẫn khóa. Các trạng thái bận báo `BUSY - wait until the machine is idle.` (hoặc `BUSY - release remote control first.`). Kiểm tra lại lần nữa khi bấm xác nhận trong hộp thoại.
- Kiểm tra từng ô như trình mô phỏng của gói màn; khi Apply còn kiểm tra chéo bằng `cfg::validate` (IP/mask/gateway cùng mạng, MQTT bật phải có host...).
- Ở chế độ DHCP các ô địa chỉ chỉ xem; chọn Static IP để sửa.
- Rời một mục khi còn thay đổi chưa Apply: hộp thoại "Discard changes?".
- Mật khẩu MQTT: ô ngoài trang chỉ hiện tối đa 12 dấu `*`; trong trình sửa hiện `*` cho tới khi bấm Show. Firmware không ghi mật khẩu ra log hay vùng chẩn đoán.
- Lưu từ màn không cần mật khẩu admin và không đổi tài khoản admin của trang web.

## Khi đang cập nhật firmware (FOTA)

Trong lúc `EspFotaBusy()` (từ lúc nhận lệnh cập nhật tới khi xong/hủy) màn coi như máy đang bận: trạng thái hiện "Cycle running", mở trình sửa/đổi nút chọn/đổi tốc độ/Apply đều bị từ chối bằng `BUSY - wait until the machine is idle.`; lịch sử không bắt đầu lần ghi flash mới (bản ghi vẫn ở RAM và được ghi sau). Ngược lại, lệnh cập nhật bị trả `busy` nếu đúng lúc một khối lịch sử đang được ghi. Lưu cấu hình từ màn vốn đã bị chặn bởi cổng cấu hình của 1.13.1 (`safety.owner`). Phần này chỉ được kiểm tra bằng đọc code và build, chưa thử OTA thật.

## Antennas (từ 1.17.0)

Đọc bốn công suất và mask thực khi mở trang; bật/tắt từng cổng, nhập hoặc +/- công suất 0–30 dBm, chọn RAM/persistent. Apply có xác nhận, ghi rồi đọc lại, chỉ báo thành công khi khớp. Không cần Home khi máy rảnh. Job độc quyền reader, không chồng với phiên điều khiển/OTA/ghi flash và bị hủy khi STOP. Không đổi gói ảnh/VP màn.

## Reader: giai đoạn 2 (chưa có)

Các trang **Reader** (profile, return loss, maintenance/information) vẫn hiện `--` và thao tác báo `Not available in this firmware yet.`. Điều này không còn áp dụng cho trang Antennas từ 1.17.0.

## Lịch sử: khi nào ghi

| Chế độ | Thời điểm thêm một dòng |
|---|---|
| Semi Auto | Khi kết quả được Confirm (trên màn hoặc lệnh Confirm từ xa) và cửa bắt đầu mở |
| Auto | Khi cửa mở xong và chu trình không ghi nhận lỗi nào |

Rescan, scan tay, lỗi, Emergency và chu trình Auto có lỗi (ví dụ reader chưa sẵn sàng, kết quả 0) không thêm dòng. Hook trong `tunnel.c` chỉ quan sát, không tham gia điều khiển.

Lưu trữ: NVS namespace `tunnel-hist`, khóa `h00`..`h24`, mỗi khóa 244 byte (20 bản ghi × 12 byte + CRC-32). Mỗi kết quả mới ghi lại đúng một khóa. Không có khối chỉ mục: thứ tự lấy từ số thứ tự của bản ghi; khối hỏng hoặc thiếu chỉ mất 20 dòng của nó.

Lưu ý:

- Bản ghi vào RAM ngay, nhưng **flash chỉ ghi khi máy đứng yên** (không motor, không scan, ngoài chu trình, sau 300 ms) và do một task nền thực hiện. Mất nguồn trước lúc đó thì mất các dòng chưa kịp ghi.
- Nếu khối chứa các dòng mới nhất bị hỏng, số `#id` tiếp theo nối từ số lớn nhất còn đọc được.
- NVS 20 KB dùng chung với cấu hình. Lịch sử đầy chiếm khoảng 250 trong 504 entry; mỗi lần ghi tốn 10 entry nên NVS phải dọn trang (xóa sector) khoảng mỗi 5–12 kết quả. Cần đo trên bo: thời gian xóa sector có vượt ngưỡng watchdog vòng lặp 100 ms hay không, và độ bền flash với sản lượng thực tế.

## Giờ / NTP

- SNTP (`pool.ntp.org`, `time.google.com`) bắt đầu khi Ethernet có IP và máy đang đứng yên; múi giờ cố định UTC+7.
- Giờ chỉ được coi là "đã biết" sau lần đồng bộ thành công đầu tiên. Bo không có RTC/pin: sau mỗi lần khởi động lại giờ là "chưa biết" cho tới khi NTP trả lời.
- Kết quả chốt lúc chưa có giờ lưu thời gian 0 và hiện `--/--/---- --:--:--`; không được điền lại về sau.
- Cần DNS và đường ra Internet (hoặc đổi tên server NTP trong `EspHistory.cpp` sang máy chủ nội bộ). Ở IP fallback 192.168.4.1 không có gateway nên không có giờ.

## Giới hạn cần biết

- Firmware vẫn ép trang như 1.12.3: trang 1 khi máy đổi trạng thái, trang 2 khi chờ Confirm và **gửi lại trang 2 mỗi 2 s** trong lúc chờ. Vì vậy xem Settings/History trong lúc chờ Confirm sẽ bị đưa lại trang 2 sau tối đa 2 s. Hành vi này được giữ nguyên có chủ đích (gói màn hiện tại đã bỏ nút Cancel trên trang Verify).
- Trạng thái lỗi chu trình (FAULT) không có nhãn riêng trên màn; hiện như "Emergency stop" vì cùng cần Reset.
- "HMI link lost" (4) không bao giờ được gửi: mất liên lạc thì cũng không gửi được.
- Khi STOP, các chạm giao diện đang chờ bị bỏ; nếu đúng lúc đó người dùng đổi trang thì firmware biết trang mới ở lần đổi trang kế tiếp.
- Khung text gửi tối đa 110 byte (108 ký tự); ba ô lớn hơn (`edit_entry`, `mqtt_host`, `dlg_body`) chỉ ghi phần đầu kèm `FF FF`.
- `HmiNetwork.cpp/.h` và test `hmi_network` còn trong repo nhưng firmware không gọi nữa. Các VP cũ `rtu_id`, `rtu_format`, `mqtt_state`, `dhcp_text` không còn được ghi.

## Kiểm chứng 1.15.1

Native `hmi_ui` và `config_service` đã đạt sau thay đổi quyền sửa; kiểm tra phần cứng và ghi NVS thực tế của bản này cần được ghi riêng, không suy từ kết quả test native.

Ngày 2026-11-16: đã nạp 1.15.1 qua COM20, verify flash đạt; so sánh 64 KiB đầu trước/sau giống hệt (bootloader, phân vùng, otadata, NVS). ESP32 thật mở được trình sửa Unit khi nhận sự kiện UI từ PC, hủy không Apply; trạng thái quan sát Ready/lock=0. Màn tách khỏi P1 trong phép thử. Trường hợp Stopped và đổi quyền ngay trước Confirm được kiểm tra native, chưa xác nhận lại bằng chạm màn thật sau khi nối P1.
