#include "AntennaSettings.h"

namespace antenna {
bool Job::start(uint32_t now, const Values *next, bool save) {
    if (view.busy || (next && !valid(*next)) || !io.acquire()) return false;
    writing = next != nullptr;
    if (next) target = *next;
    persist = save;
    step = writing ? 0 : 2;
    waiting = draining = false;
    failure = NONE;
    observed = Values{};
    view.busy = true;
    view.valid = false;
    view.result = NONE;
    since = now;
    return true;
}
void Job::finish(Result result) {
    view.result = result;
    view.valid = result == LOADED || result == SAVED;
    if (view.valid) view.values = observed;
    view.busy = false;
    waiting = draining = false;
    ++view.revision;
    io.release();
}
void Job::cancel() {
    if (!view.busy) return;
    draining = true;
    failure = CANCELLED;
    view.valid = false;
}
void Job::tick(uint32_t now) {
    if (!view.busy) return;
    if (!io.allowed()) cancel();
    uint8_t bytes[64] = {}, size = 0, status = 0;
    if (draining) {
        // Do not hand the reader to another controller until its outstanding
        // command has completed/timed out. Driver timeout is independently bounded.
        if (!waiting || io.reply(bytes, &size, &status) || io.ready()) finish(failure);
        return;
    }
    if (uint32_t(now - since) >= 3000) {
        failure = TIMEOUT;
        draining = true;
        return;
    }
    if (!waiting) {
        if (!io.ready()) return;
        uint8_t data[4] = {}, count = 0;
        switch (step) {
        case 0:
            expected = 0x2F; count = 4;
            for (unsigned i = 0; i < 4; ++i) data[i] = target.power[i] | (persist ? 0 : 0x80);
            break;
        case 1:
            expected = 0x3F; count = 1; data[0] = target.mask | (persist ? 0 : 0x80);
            break;
        case 2: expected = 0x94; break;
        default: expected = 0x21; break;
        }
        if (!io.begin(expected, data, count)) { finish(REJECTED); return; }
        waiting = true;
        since = now;
        return;
    }
    if (!io.reply(bytes, &size, &status)) return;
    waiting = false;
    if (status) { finish(READER_ERROR); return; }
    if (size < 2 || size > 64 || bytes[0] != expected) { finish(BAD_REPLY); return; }
    if (bytes[1]) { finish(READER_ERROR); return; }
    if (step == 2) {
        if (size != 6) { finish(BAD_REPLY); return; }
        for (unsigned i = 0; i < 4; ++i) {
            observed.power[i] = bytes[i + 2];
            if (observed.power[i] > 30) { finish(BAD_REPLY); return; }
        }
    } else if (step == 3) {
        // ZK GetReaderInformation payload byte 8 is the antenna bitmap.
        // The command/status bytes precede the payload in TakePcReply.
        if (size < 11) { finish(BAD_REPLY); return; }
        observed.mask = bytes[10] & 15;
        if (!valid(observed)) { finish(BAD_REPLY); return; }
        if (writing && !same(target, observed)) { finish(MISMATCH); return; }
        finish(writing ? SAVED : LOADED);
        return;
    }
    ++step;
    since = now;
}
} // namespace antenna
