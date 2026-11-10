#pragma once
#include "../modbus/ModbusCore.h"

namespace tunnelmqtt {
enum Action : uint8_t { CLAIM = 1, HEARTBEAT, RELEASE, STOP, EMERGENCY, EXECUTE };
struct Request {
    char session[17] = {}, client[17] = {};
    uint32_t epoch = 0, id = 0, seen = 0;
    Action action = CLAIM;
    uint16_t op = 0, args[6] = {};
};
struct Reply {
    // code: 0 processed, 1 malformed, 2 wrong session/epoch, 3 stale,
    // 4 retained, 5 ownership, 6 Modbus exception, 7 replay, 8 client table full.
    uint8_t code = 0, exception = 0;
    uint16_t result = 0, sequence = 0;
};
class Api {
  public:
    explicit Api(mb::Core &shared) : core(shared) {}
    void connect(const char *newSession);
    void disconnect();
    void fence();
    Reply process(const Request &q, bool retained, uint32_t now);
    void snapshot(uint16_t *registers);
    const char *session() const {
        return bootSession;
    }
    uint32_t generation() const {
        return epoch;
    }
    const char *owner() const {
        return core.owns(mb::MQTT) ? controller : "";
    }

  private:
    struct Entry {
        bool used = false;
        Request request;
        Reply reply;
    } entries[8];
    mb::Core &core;
    char bootSession[17] = {}, controller[17] = {};
    uint32_t epoch = 0;
    Reply dispatch(const Request &q);
    uint16_t holding(uint16_t a);
    uint8_t single(uint16_t a, uint16_t v);
};
// Strict, bounded JSON; implemented separately so the application is host-testable.
bool decode(const char *json, size_t length, Request &request);
// Read-only bootstrap: no session, ownership or lease renewal is required.
struct ReadRequest {
    char client[17] = {};
    uint32_t id = 0;
};
bool decodeGet(const char *json, size_t length, ReadRequest &request);
bool encodeState(Api &api, const ReadRequest &request, uint32_t now, char *out, size_t size);
} // namespace tunnelmqtt
