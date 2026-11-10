# Source firmware

Các lớp: [app](app/README.md), [control](control/README.md), [reader](reader/README.md), [hmi](hmi/README.md), [protocol](protocol/README.md), [platform](platform/README.md), [config](config/README.md), [common](common/README.md), [web](web/README.md).

Arduino CLI compile đệ quy thư mục này. `.c` giữ C ABI; `.cpp` dùng C++. Dependency từ app → protocol/control → interface/backend; callback task mạng không đổi trạng thái máy trực tiếp.
