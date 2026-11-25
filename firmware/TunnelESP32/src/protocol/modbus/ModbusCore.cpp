#include "../../common/ProtocolLimits.h"
#include "ModbusCore.h"
#include <string.h>
namespace mb {
static uint16_t get16(const uint8_t *p) {
    return (uint16_t(p[0]) << 8) | p[1];
}
static void put16(uint8_t *p, uint16_t v) {
    p[0] = uint8_t(v >> 8);
    p[1] = uint8_t(v);
}
uint32_t Core::controlLeaseMs() const {
    return controller == MQTT ? TUNNEL_MQTT_CONTROL_LEASE_MS : TUNNEL_CONTROL_LEASE_MS;
}
void Core::touch() {
    leaseAt = clock;
    io.heartbeat();
}
void Core::tick(uint32_t now) {
    clock = now;
    if (controller && uint32_t(clock - leaseAt) >= controlLeaseMs()) {
        controller = 0;
        io.stop(false);
    }
    if (readerState == 1 && io.readerReply(readerData, &readerLength, &readerStatus)) {
        if (readerLength > 64) {
            readerLength = 0;
            readerStatus = 5;
        }
        readerState = readerStatus ? 3 : 2;
    }
}
void Core::disconnect(uint8_t peer) {
    if (controller == peer) {
        controller = 0;
        io.stop(false);
    }
}
void Core::resetPeer(uint8_t peer) {
    if (peer > 0 && peer < PEERS) {
        disconnect(peer);
        history[peer] = History{};
    }
}
bool Core::legacyAllow(uint8_t command) {
    if (command == 0x17)
        return true; // STOP from every interface always accepted.
    if (command == 1) {
        if (controller == LEGACY)
            touch();
        return true;
    } // Only the owner renews its lease.
    if (command < 0x10 || command > 0x22 || command == 0x1A || command == 0x1B || command == 0x1C ||
        command == 0x1D || command == 0x1E || command == 0x1F)
        return false;
    if ((controller && controller != LEGACY) || readerState == 1)
        return false;
    controller = LEGACY;
    touch();
    return true;
}
uint16_t Core::read(uint8_t peer, uint16_t address) {
    if (address == 16)
        return controller;
    if (address == 17)
        return controller ? uint16_t(controlLeaseMs() - uint32_t(clock - leaseAt)) : 0;
    if (address == 18)
        return lastSequence;
    if (address == 19)
        return lastResult;
    if (address == 20)
        return readerState;
    if (address == 21)
        return readerSequence;
    if (address == 22)
        return readerStatus;
    if (address == 23)
        return readerLength;
    if (address >= 32 && address < 96)
        return readerData[address - 32];
    if (address == 0x100)
        return controller == peer;
    if (address == 0x101)
        return 0;
    if (address == 0x102)
        return 0;
    if (address >= 0x110 && address < 0x118)
        return history[peer].words[address - 0x110];
    if (address == 0x118)
        return history[peer].result;
    return io.read(address);
}
uint8_t Core::execute(const uint16_t *v) {
    const uint16_t op = v[1], arg = v[2];
    static const uint8_t commands[] = {0, 0x18, 0x10, 0x11, 0x12, 0x13, 0x15, 0x16, 0x19, 0x20};
    if (op >= 1 && op <= 9) {
        for (unsigned i = 3; i < 8; i++)
            if (v[i])
                return 1;
        if ((op == 1 || op == 6 || op == 7) && arg)
            return 1;
        if ((op == 2 || op == 3 || op == 4 || op == 5 || op == 9) && arg > 1)
            return 1;
        if (op == 8 && (arg < 1 || arg > 5))
            return 1;
        return io.command(commands[op], arg);
    }
    uint8_t cmd = 0, data[5] = {}, length = 0;
    unsigned used = 0;
    switch (op) {
    case 20:
        cmd = 0x21;
        break; // Reader information (contains antenna mask).
    case 21:
        cmd = 0x94;
        break; // Get four antenna powers.
    case 22:
        used = 5;
        cmd = 0x2F;
        length = 4;
        if (v[6] > 1)
            return 1;
        for (unsigned i = 0; i < 4; i++) {
            if (v[i + 2] > 30)
                return 1;
            data[i] = uint8_t(v[i + 2] | (v[6] ? 0x80 : 0));
        }
        break;
    case 23:
        used = 2;
        cmd = 0x3F;
        length = 1;
        if (arg < 1 || arg > 15 || v[3] > 1)
            return 1;
        data[0] = uint8_t(arg | (v[3] ? 0x80 : 0));
        break;
    case 24:
        cmd = 0x7F;
        length = 1;
        break;
    case 25: // Encoded legacy Set byte, validated again by the existing reader bridge.
        used = 1;
        cmd = 0x7F;
        length = 1;
        if (arg > 255 || !(arg & 0x80))
            return 1;
        data[0] = uint8_t(arg);
        break;
    case 26:
        cmd = 0x7F;
        length = 3;
        break;
    case 27:
        used = 1;
        cmd = 0x7F;
        length = 3;
        data[0] = 1;
        data[1] = uint8_t(arg >> 8);
        data[2] = uint8_t(arg);
        break;
    case 28:
        cmd = 0xEC;
        length = 1;
        break;
    case 29:
        used = 3;
        cmd = 0x91;
        length = 5;
        if (v[4] > 3)
            return 1;
        put16(data, arg);
        put16(data + 2, v[3]);
        data[4] = uint8_t(v[4]);
        break;
    default:
        return 7;
    }
    for (unsigned i = used + 2; i < 8; i++)
        if (v[i])
            return 1;
    if (!io.readerBegin(cmd, data, length))
        return 2;
    readerState = 1;
    readerStatus = 0;
    readerLength = 0;
    readerSequence = v[0];
    memset(readerData, 0, sizeof(readerData));
    return 0;
}
uint8_t Core::write(uint8_t peer, uint16_t address, const uint16_t *v, uint16_t count) {
    if (address == 0x102 && count == 1) {
        if (v[0] != 1)
            return EX_VALUE;
        controller = 0;
        io.stop(false);
        return 0;
    }
    if (address == 0x101 && count == 1) {
        if (v[0] != 1)
            return EX_VALUE;
        io.stop(true);
        return 0;
    }
    if (inhibited) return EX_BUSY;
    if (address == 0x100 && count == 1) {
        if (v[0] > 1)
            return EX_VALUE;
        if (controller && controller != peer)
            return EX_BUSY;
        if (!v[0]) {
            disconnect(peer);
            return 0;
        }
        if (!controller) {
            if (readerState == 1)
                return EX_BUSY;
            controller = peer;
            // Claim is a stop/takeover, never an implicit motion command.
            if (io.command(0x22, 1)) {
                controller = 0;
                return EX_FAILURE;
            }
        }
        touch();
        return 0;
    }
    if (address != 0x110 || count != 8)
        return EX_ADDRESS;
    if (controller != peer)
        return EX_BUSY;
    History &h = history[peer];
    if (h.valid && v[0] == h.words[0]) {
        if (memcmp(v, h.words, sizeof(h.words)))
            return EX_VALUE;
        // A repeat acknowledges the original operation, without replay or lease renewal.
        return 0;
    }
    if (v[0] != uint16_t((h.valid ? h.words[0] : 0) + 1))
        return EX_VALUE;
    if (readerState == 1)
        return EX_BUSY;
    touch();
    memcpy(h.words, v, sizeof(h.words));
    h.valid = true;
    h.result = execute(v);
    lastSequence = v[0];
    lastResult = h.result;
    return 0;
}
size_t Core::pdu(uint8_t peer, const uint8_t *q, size_t n, uint8_t *r) {
    if (!n || peer == 0 || peer == LEGACY || peer >= PEERS)
        return 0;
    const uint8_t fc = q[0];
    uint8_t error = 0;
    uint16_t address = 0, count = 0;
    r[0] = fc;
    if (fc == 3 || fc == 4) {
        if (n != 5)
            error = EX_VALUE;
        else {
            address = get16(q + 1);
            count = get16(q + 3);
            if (!count || count > 125)
                error = EX_VALUE;
            else {
                for (unsigned i = 0; i < count; i++) {
                    uint32_t a = uint32_t(address) + i;
                    bool valid = fc == 4
                                     ? (a < 100)
                                     : ((a >= 0x100 && a <= 0x102) || (a >= 0x110 && a <= 0x118));
                    if (!valid) {
                        error = EX_ADDRESS;
                        break;
                    }
                }
                if (!error) {
                    r[1] = uint8_t(count * 2);
                    for (unsigned i = 0; i < count; i++)
                        put16(r + 2 + i * 2, read(peer, uint16_t(address + i)));
                    return 2 + count * 2;
                }
            }
        }
    } else if (fc == 6) {
        if (n != 5)
            error = EX_VALUE;
        else {
            uint16_t value = get16(q + 3);
            error = write(peer, get16(q + 1), &value, 1);
        }
        if (!error) {
            memcpy(r, q, 5);
            return 5;
        }
    } else if (fc == 16) {
        if (n < 6)
            error = EX_VALUE;
        else {
            address = get16(q + 1);
            count = get16(q + 3);
            if (!count || count > 123 || q[5] != count * 2 || n != size_t(6 + count * 2))
                error = EX_VALUE;
            else if (count > 8)
                error = EX_ADDRESS;
            else {
                uint16_t values[8];
                for (unsigned i = 0; i < count; i++)
                    values[i] = get16(q + 6 + i * 2);
                error = write(peer, address, values, count);
            }
        }
        if (!error) {
            memcpy(r, q, 5);
            return 5;
        }
    } else
        error = EX_FUNCTION;
    r[0] = fc | 0x80;
    r[1] = error;
    return 2;
}
uint16_t Core::crc(const uint8_t *data, size_t n) {
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= data[i];
        for (unsigned b = 0; b < 8; b++)
            c = (c >> 1) ^ ((c & 1) ? 0xA001 : 0);
    }
    return c;
}
size_t Core::rtu(uint8_t unit, const uint8_t *q, size_t n, uint8_t *r) {
    // No broadcast motion/configuration: address zero is intentionally ignored.
    if (n < 4 || n > 256 || q[0] != unit || !unit || crc(q, n) != 0)
        return 0;
    r[0] = unit;
    size_t length = pdu(RTU, q + 1, n - 3, r + 1);
    if (!length)
        return 0;
    ++length;
    uint16_t c = crc(r, length);
    r[length++] = uint8_t(c);
    r[length++] = uint8_t(c >> 8);
    return length;
}
size_t Core::tcp(uint8_t peer, const uint8_t *q, size_t n, uint8_t *r, uint8_t unit) {
    if (n < 8 || n > 260 || get16(q + 2) != 0 || size_t(get16(q + 4)) + 6 != n)
        return 0;
    // Dedicated server: Unit ID 1; echo it in the response. Not a RTU gateway.
    if (q[6] != unit) {
        memcpy(r, q, 7);
        put16(r + 4, 3);
        r[7] = q[7] | 0x80;
        r[8] = 0x0B;
        return 9;
    }
    size_t length = pdu(peer, q + 7, n - 7, r + 7);
    if (!length)
        return 0;
    memcpy(r, q, 4);
    put16(r + 4, uint16_t(length + 1));
    r[6] = q[6];
    return length + 7;
}
} // namespace mb
