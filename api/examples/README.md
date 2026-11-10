# JSON mẫu

`get.example.json`, `state.example.json`, `ack.example.json`, `commands.example.json` minh họa shape numeric từ1.11. `named.example.json` minh họa method/params1.12. Ví dụ state hiện hành gồm thêm trường tên.

Các token, epoch và ms là dữ liệu minh họa, không copy gửi nguyên vào bo. Client phải subscribe state/ack rồi `/get` lấy token mới; id tăng trong mỗi client; retain=false. Khi đọc antenna, claim và maintenance trước, theo dõi reply/sequence và heartbeat trong lúc chờ.
