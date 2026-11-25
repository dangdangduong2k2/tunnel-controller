#pragma once
#include <stddef.h>
#include <stdint.h>
namespace fota {
constexpr size_t MAX_JSON = 1536;
constexpr const char *TARGET = "tunnel-esp32s3-v1";
struct Request {
    char job[33] = {}, session[17] = {}, client[17] = {};
    char url[257] = {}, target[33] = {}, sha256[65] = {}, signature[145] = {};
    uint32_t epoch = 0, id = 0, seen_ms = 0, version = 0, size = 0, unix_time = 0;
};
bool decode(const char *data, size_t size, Request &out);
bool canonical(const Request &q, char *out, size_t size);
const char *validate(const Request &q, const char *session, uint32_t epoch, uint32_t now,
                     uint32_t runningVersion, size_t slotSize, const char *urlPrefix);
// Called on the control loop only, after signature verification.
class Replay {
  public:
    // 0 new, 1 duplicate, -1 changed/stale ID, -2 client table full.
    int check(const Request &q);
    void remember(const Request &q);
    void reset();

  private:
    struct Entry {
        bool used = false;
        Request q;
    } entries[8];
};
} // namespace fota
