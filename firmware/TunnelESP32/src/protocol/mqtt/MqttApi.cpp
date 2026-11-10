#include "../../common/ProtocolLimits.h"
#include "MqttApi.h"
#include <string.h>
namespace tunnelmqtt {
static void put(uint8_t *p, uint16_t v) {
    p[0] = uint8_t(v >> 8);
    p[1] = uint8_t(v);
}
static bool hex(const char *p) {
    if (strlen(p) != 16)
        return false;
    for (unsigned i = 0; i < 16; i++)
        if (!((p[i] >= '0' && p[i] <= '9') || (p[i] >= 'a' && p[i] <= 'f')))
            return false;
    return true;
}
void Api::connect(const char *s) {
    core.resetPeer(mb::MQTT);
    strncpy(bootSession, s, 16);
    bootSession[16] = 0;
    controller[0] = 0;
    epoch = 0;
    fence();
}
void Api::disconnect() {
    core.disconnect(mb::MQTT);
    controller[0] = 0;
    fence();
}
void Api::fence() {
    ++epoch;
    for (auto &e : entries)
        e = Entry{};
}
uint8_t Api::single(uint16_t a, uint16_t v) {
    uint8_t q[5] = {6}, r[260];
    put(q + 1, a);
    put(q + 3, v);
    size_t n = core.pdu(mb::MQTT, q, sizeof(q), r);
    return n == 5 ? 0 : (n == 2 ? r[1] : 4);
}
uint16_t Api::holding(uint16_t a) {
    uint8_t q[5] = {3, 0, 0, 0, 1}, r[260];
    put(q + 1, a);
    return core.pdu(mb::MQTT, q, sizeof(q), r) == 4 ? uint16_t(r[2] * 256 + r[3]) : 0;
}
void Api::snapshot(uint16_t *v) {
    uint8_t q[] = {4, 0, 0, 0, 96}, r[260];
    if (core.pdu(mb::MQTT, q, sizeof(q), r) != 194) {
        memset(v, 0, 96 * sizeof(*v));
        return;
    }
    for (unsigned i = 0; i < 96; i++)
        v[i] = uint16_t(r[2 + i * 2] * 256 + r[3 + i * 2]);
}
Reply Api::dispatch(const Request &q) {
    Reply r;
    bool owns = core.owns(mb::MQTT) && !strcmp(controller, q.client);
    if (q.action == STOP || q.action == EMERGENCY) {
        r.exception = single(q.action == STOP ? 0x102 : 0x101, 1);
    } else if (q.action == CLAIM) {
        if (core.active() && !owns) {
            r.code = 5;
            return r;
        }
        r.exception = single(0x100, 1);
        if (!r.exception)
            strcpy(controller, q.client);
    } else {
        if (!owns) {
            r.code = 5;
            return r;
        }
        if (q.action == HEARTBEAT)
            r.exception = single(0x100, 1);
        else if (q.action == RELEASE) {
            r.exception = single(0x100, 0);
            controller[0] = 0;
        } else if (q.action == EXECUTE) {
            uint8_t pdu[22] = {16, 1, 0x10, 0, 8, 16}, out[260];
            r.sequence = uint16_t(holding(0x110) + 1);
            put(pdu + 6, r.sequence);
            put(pdu + 8, q.op);
            for (unsigned i = 0; i < 6; i++)
                put(pdu + 10 + i * 2, q.args[i]);
            size_t n = core.pdu(mb::MQTT, pdu, sizeof(pdu), out);
            if (n == 2)
                r.exception = out[1];
            else if (n != 5)
                r.exception = 4;
            else
                r.result = holding(0x118);
        } else
            r.code = 1;
    }
    if (r.exception)
        r.code = 6;
    return r;
}
Reply Api::process(const Request &q, bool retained, uint32_t now) {
    Reply r;
    if (!hex(q.client) || !hex(q.session) || !q.id || q.action < CLAIM || q.action > EXECUTE) {
        r.code = 1;
        return r;
    }
    if (retained) {
        r.code = 4;
        return r;
    }
    if (strcmp(q.session, bootSession) || q.epoch != epoch) {
        r.code = 2;
        return r;
    }
    Entry *slot = nullptr;
    for (auto &e : entries)
        if (e.used && !strcmp(e.request.client, q.client)) {
            slot = &e;
            break;
        }
    if (slot && q.id <= slot->request.id) {
        const Request &old = slot->request;
        if (q.id == old.id && q.action == old.action && q.op == old.op && q.seen == old.seen &&
            !memcmp(q.args, old.args, sizeof(q.args)))
            return slot->reply;
        r.code = 7;
        return r;
    }
    if (uint32_t(now - q.seen) > TUNNEL_MQTT_TOKEN_MAX_AGE_MS) {
        r.code = 3;
        return r;
    }
    if (!slot)
        for (auto &e : entries)
            if (!e.used) {
                slot = &e;
                break;
            }
    if (!slot) {
        r.code = 8;
        return r;
    }
    r = dispatch(q);
    slot->used = true;
    slot->request = q;
    slot->reply = r;
    return r;
}
} // namespace tunnelmqtt
