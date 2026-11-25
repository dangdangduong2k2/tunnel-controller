# Contract và code generation

- [release.json](release.json): phiên bản firmware, số IR1, core/FQBN và phiên bản giao thức.
- [operations.json](operations.json): 19OP, tên method, thứ tự tham số/range và tác động. Tham số thứi ánh xạ ARGi.
- [hmi_fields.json](hmi_fields.json): các ô text kết nối của màn DWIN HMI v2 (tên, VP, số byte); sinh bằng `tools/generate_hmi.py` từ `vp_map.csv` của gói màn cùng lúc với `HmiLayout.h`.

Chạy `python tools/generate.py` từ root để tạo Version.h, OperationCatalog.h, Python operations.py, docs/COMMANDS.md và ConfigPage.h từ HTML. Chạy lại với `--check` trong validation; file stale phải fail. Operation catalog mô tả shape/range chung, liên động cơ khí và điều kiện reader vẫn được backend kiểm tra khi thực hiện.

Đổi OP/offset/enum đang dùng là đổi wire contract; không thực hiện như đổi tên nội bộ. Cập nhật test, migration và ví dụ tương ứng. HMI manifest được dùng bởi hardware diagnostic; test `hmi_ui` kiểm tra VP/frame thực tế.
