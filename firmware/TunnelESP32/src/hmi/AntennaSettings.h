#pragma once
#include <stdint.h>

namespace antenna {
struct Values {
    uint8_t power[4] = {};
    uint8_t mask = 0;
};
inline bool valid(const Values &v) {
    if (v.mask < 1 || v.mask > 15) return false;
    for (uint8_t p : v.power) if (p > 30) return false;
    return true;
}
inline bool same(const Values &a, const Values &b) {
    if (a.mask != b.mask) return false;
    for (unsigned i = 0; i < 4; ++i) if (a.power[i] != b.power[i]) return false;
    return true;
}
enum Result : uint8_t { NONE, LOADED, SAVED, REJECTED, TIMEOUT, READER_ERROR, BAD_REPLY, MISMATCH, CANCELLED };
struct State {
    Values values;
    bool valid = false, busy = false;
    Result result = NONE;
    uint32_t revision = 0;
};
class Transport {
  public:
    virtual bool acquire() = 0;
    virtual bool allowed() = 0;
    virtual bool ready() = 0;
    virtual bool begin(uint8_t command, const uint8_t *data, uint8_t size) = 0;
    virtual bool reply(uint8_t *data, uint8_t *size, uint8_t *status) = 0;
    virtual void release() = 0;
    virtual ~Transport() {}
};
// One exclusive, bounded transaction. Writes are never automatically retried.
// Success requires GetPower + GetInfo readback, not just the two Set ACKs.
class Job {
  public:
    explicit Job(Transport &transport) : io(transport) {}
    bool start(uint32_t now, const Values *next = nullptr, bool save = true);
    void tick(uint32_t now);
    void cancel();
    const State &state() const { return view; }
  private:
    void finish(Result result);
    Transport &io;
    State view;
    Values target, observed;
    bool writing = false, persist = true, waiting = false, draining = false;
    uint8_t step = 0, expected = 0;
    uint32_t since = 0;
    Result failure = NONE;
};
} // namespace antenna
