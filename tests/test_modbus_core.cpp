#include "ModbusCore.h"
#include <cassert>
#include <cstdio>
#include <cstring>
static unsigned commands, stops, emergencies, beats, readerWrites;
static uint8_t lastCmd, lastLength, lastData[5], replyStatus;
static uint16_t lastArg;
static bool maintenance, readerAllowed = true, replyReady;
static uint16_t readStatus(uint16_t a) {
    return uint16_t(a + 100);
}
static uint8_t command(uint8_t c, uint16_t v) {
    commands++;
    lastCmd = c;
    lastArg = v;
    if (c == 0x20)
        maintenance = v != 0;
    return 0;
}
static bool readerBegin(uint8_t c, const uint8_t *p, uint8_t n) {
    if (!maintenance || !readerAllowed)
        return false;
    readerWrites++;
    lastCmd = c;
    lastLength = n;
    memcpy(lastData, p, n);
    return true;
}
static bool readerReply(uint8_t *p, uint8_t *n, uint8_t *s) {
    if (!replyReady)
        return false;
    replyReady = false;
    *n = 6;
    *s = replyStatus;
    p[0] = lastCmd;
    p[1] = 0;
    p[2] = 30;
    p[3] = 29;
    p[4] = 28;
    p[5] = 27;
    return true;
}
static void heartbeat() {
    beats++;
}
static void stop(bool e) {
    stops++;
    emergencies += e;
}
static mb::Backend backend = {readStatus, command, readerBegin, readerReply, heartbeat, stop};
static uint8_t out[260];
static uint16_t get(const uint8_t *p) {
    return uint16_t(p[0]) * 256 + p[1];
}
static void put(uint8_t *p, uint16_t v) {
    p[0] = uint8_t(v >> 8);
    p[1] = uint8_t(v);
}
static size_t single(mb::Core &c, uint8_t peer, uint16_t a, uint16_t v) {
    uint8_t q[5] = {6};
    put(q + 1, a);
    put(q + 3, v);
    return c.pdu(peer, q, 5, out);
}
static size_t issue(mb::Core &c, uint8_t peer, uint16_t seq, uint16_t op, uint16_t a = 0,
                    uint16_t b = 0, uint16_t d = 0, uint16_t e = 0, uint16_t f = 0,
                    uint16_t g = 0) {
    uint16_t v[8] = {seq, op, a, b, d, e, f, g};
    uint8_t q[22] = {16, 1, 0x10, 0, 8, 16};
    for (unsigned i = 0; i < 8; i++)
        put(q + 6 + 2 * i, v[i]);
    return c.pdu(peer, q, sizeof(q), out);
}
static uint16_t reg(mb::Core &c, uint8_t peer, uint16_t a, uint8_t fc = 4) {
    uint8_t q[5] = {fc};
    put(q + 1, a);
    put(q + 3, 1);
    assert(c.pdu(peer, q, 5, out) == 4);
    return get(out + 2);
}
static void framing() {
    mb::Core c(backend);
    // Published standard CRC example (01 03 0000 000A -> C5 CD).
    uint8_t sample[] = {1, 3, 0, 0, 0, 10};
    assert(mb::Core::crc(sample, 6) == 0xCDC5);
    uint8_t q[] = {1, 4, 0, 0, 0, 2, 0, 0};
    uint16_t crc = mb::Core::crc(q, 6);
    q[6] = uint8_t(crc);
    q[7] = uint8_t(crc >> 8);
    assert(c.rtu(1, q, 8, out) == 9 && out[1] == 4 && get(out + 3) == 100 && get(out + 5) == 101 &&
           mb::Core::crc(out, 9) == 0);
    q[7] ^= 1;
    assert(c.rtu(1, q, 8, out) == 0);
    q[7] ^= 1;
    assert(c.rtu(2, q, 8, out) == 0);
    q[0] = 0;
    assert(c.rtu(1, q, 8, out) == 0);
    uint8_t tcp[] = {0x12, 0x34, 0, 0, 0, 6, 1, 4, 0, 0, 0, 2};
    assert(c.tcp(2, tcp, 12, out) == 13 && get(out) == 0x1234 && get(out + 4) == 7 && out[6] == 1);
    for (unsigned i = 0; i < 12; i++)
        assert(c.tcp(2, tcp, i, out) == 0);
    tcp[3] = 1;
    assert(c.tcp(2, tcp, 12, out) == 0);
    tcp[3] = 0;
    tcp[6] = 2;
    assert(c.tcp(2, tcp, 12, out) == 9 && out[7] == 0x84 && out[8] == 0x0B);
    assert(c.tcp(2, tcp, 12, out, 2) == 13 && out[6] == 2); // configured Unit ID
    uint8_t metadata[] = {4, 0, 96, 0, 4};
    assert(c.pdu(2, metadata, 5, out) == 10);
    uint8_t outside[] = {4, 0, 100, 0, 1};
    assert(c.pdu(2, outside, 5, out) == 2 && out[1] == 2);
    uint8_t unknown[] = {0x7F};
    assert(c.pdu(2, unknown, 1, out) == 2 && out[1] == 1);
    uint8_t overflow[] = {4, 0xFF, 0xFF, 0, 2};
    assert(c.pdu(2, overflow, 5, out) == 2 && out[1] == 2);
    uint8_t zero[] = {4, 0, 0, 0, 0};
    assert(c.pdu(2, zero, 5, out) == 2 && out[1] == 3);
    uint8_t malformed[] = {16, 1, 0x10, 0, 8, 15};
    assert(c.pdu(2, malformed, 6, out) == 2 && out[1] == 3);
    assert(single(c, 2, 0x110, 1) == 2 && out[1] == 2);
}
static void ownership() {
    mb::Core c(backend);
    c.tick(100);
    unsigned n = commands;
    assert(issue(c, 1, 1, 1) == 2 && out[1] == 6 && commands == n);
    assert(single(c, 1, 0x100, 1) == 5 && c.owns(1) && lastCmd == 0x22);
    assert(single(c, 2, 0x100, 1) == 2 && out[1] == 6);
    assert(!c.legacyAllow(0x18));
    assert(c.legacyAllow(1));
    assert(issue(c, 1, 1, 1) == 5 && lastCmd == 0x18);
    n = commands;
    assert(issue(c, 1, 1, 1) == 5 && commands == n); // no duplicate Reset/homing
    assert(issue(c, 1, 1, 2, 1) == 2 && out[1] == 3 && commands == n);
    assert(issue(c, 1, 3, 2, 1) == 2 && out[1] == 3);
    assert(issue(c, 1, 2, 2, 1) == 5 && lastCmd == 0x10 && lastArg == 1);
    assert(reg(c, 1, 0x118, 3) == 0);
    unsigned e = emergencies;
    assert(single(c, 2, 0x101, 1) == 5 && emergencies == e + 1);
    unsigned b = beats;
    c.tick(3099);
    reg(c, 2, 2);
    assert(beats == b); // monitoring is not keepalive
    unsigned s = stops;
    c.tick(3100);
    assert(c.owner() == 0 && stops == s + 1);
    assert(issue(c, 1, 3, 1) == 2 && out[1] == 6); // late Reset requires a new claim
    assert(single(c, 2, 0x100, 1) == 5);
    c.tick(5000);
    assert(single(c, 2, 0x100, 1) == 5);
    c.tick(7999);
    assert(c.owns(2));
    c.tick(8000);
    assert(!c.active());
    assert(c.legacyAllow(0x18) && c.owns(mb::LEGACY));
    c.tick(10000);
    assert(c.legacyAllow(1));
    c.tick(12999);
    assert(c.owns(mb::LEGACY));
    c.tick(13000);
    assert(!c.active());
    assert(single(c, 3, 0x100, 1) == 5);
    c.disconnect(2);
    assert(c.owns(3));
    c.disconnect(3);
    assert(!c.active());
    c.tick(0xFFFFFF00);
    assert(single(c, 1, 0x100, 1) == 5);
    c.tick(0x00000AB8);
    assert(!c.active()); // wrap
}
static void antenna() {
    mb::Core c(backend);
    c.tick(0);
    maintenance = false;
    replyStatus = 0;
    single(c, 1, 0x100, 1);
    assert(issue(c, 1, 1, 22, 30, 29, 28, 27, 1) == 5 &&
           reg(c, 1, 19) == 2); // maintenance required
    assert(issue(c, 1, 2, 9, 1) == 5 && maintenance);
    unsigned n = readerWrites;
    assert(issue(c, 1, 3, 22, 30, 29, 28, 27, 1) == 5 && readerWrites == n + 1);
    assert(lastCmd == 0x2F && lastLength == 4 && lastData[0] == 0x9E && lastData[3] == 0x9B);
    assert(c.readerBusy() && reg(c, 1, 20) == 1);
    assert(issue(c, 1, 3, 22, 30, 29, 28, 27, 1) == 5 && readerWrites == n + 1);
    assert(issue(c, 1, 4, 21) == 2 && out[1] == 6);
    replyReady = true;
    c.tick(10);
    assert(!c.readerBusy() && reg(c, 1, 20) == 2 && reg(c, 1, 34) == 30);
    assert(issue(c, 1, 4, 22, 31, 29, 28, 27, 1) == 5 && reg(c, 1, 19) == 1 &&
           readerWrites == n + 1);
    assert(issue(c, 1, 5, 23, 5, 1) == 5 && lastCmd == 0x3F && lastData[0] == 0x85);
    replyStatus = 6;
    replyReady = true;
    c.tick(20);
    assert(reg(c, 1, 20) == 3 && reg(c, 1, 22) == 6);
    assert(issue(c, 1, 6, 27, 4123) == 5 && lastCmd == 0x7F && lastData[0] == 1 &&
           lastData[1] == 0x10 && lastData[2] == 0x1B);
    replyStatus = 0;
    replyReady = true;
    c.tick(30);
    assert(issue(c, 1, 7, 29, 0x000E, 0x1190, 2) == 5 && lastCmd == 0x91 && lastLength == 5 &&
           lastData[4] == 2);
    replyReady = true;
    c.tick(40);
    assert(issue(c, 1, 8, 22, 0, 10, 20, 30, 0) == 5 && lastData[0] == 0 && lastData[3] == 30);
    unsigned s = stops;
    c.tick(3040);
    assert(!c.active() && stops == s + 1);
    assert(single(c, 2, 0x100, 1) == 2 && out[1] == 6); // cannot steal pending reader reply
    replyReady = true;
    c.tick(3050);
    assert(single(c, 2, 0x100, 1) == 5);
}
int main() {
    framing();
    ownership();
    antenna();
    puts("PASS Modbus: CRC/MBAP, malformed lengths, units, exceptions, ownership, lease/wrap, "
         "replay, STOP from non-owner, antenna RAM/persistent bytes, async replies/errors");
}
