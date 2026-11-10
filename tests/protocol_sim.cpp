#include "Version.h"
// Test-only host process. Production protocol/application code; IO is simulated.
#include "MqttApi.h"
#include <iostream>
#include <sstream>
#include <string>
#include <cstdio>
#include <cstring>
static uint32_t now = 0, commandCount = 0;
static bool fence = false, readerPending = false;
static uint8_t readerCmd = 0, power[4] = {30, 30, 30, 30};
static uint16_t read(uint16_t a) {
    if (a == 0)
        return 3;
    if (a == 1)
        return TUNNEL_FIRMWARE_REGISTER;
    if (a == 5)
        return 85;
    if (a == 10)
        return 7;
    if (a == 15)
        return 1;
    if (a == 24)
        return 7;
    if (a == 29)
        return 1;
    if (a == 30)
        return uint16_t(now >> 16);
    if (a == 31)
        return uint16_t(now);
    return 0;
}
static uint8_t command(uint8_t c, uint16_t) {
    ++commandCount;
    if (c == 0x22)
        fence = true;
    return 0;
}
static bool reader(uint8_t c, const uint8_t *p, uint8_t n) {
    ++commandCount;
    readerPending = true;
    readerCmd = c;
    if (c == 0x2F && n == 4)
        for (int i = 0; i < 4; i++)
            power[i] = p[i] & 127;
    return true;
}
static bool reply(uint8_t *p, uint8_t *n, uint8_t *s) {
    if (!readerPending)
        return false;
    readerPending = false;
    *s = 0;
    *n = readerCmd == 0x94 ? 6 : 2;
    p[0] = readerCmd;
    p[1] = 0;
    if (*n == 6)
        memcpy(p + 2, power, 4);
    return true;
}
static void heartbeat() {}
static void stop(bool) {
    fence = true;
}
static mb::Backend backend = {read, command, reader, reply, heartbeat, stop};
int main(int argc, char **) {
    if (argc == 1) {
        puts("PASS simulator compiled (use --serve for integration)");
        return 0;
    }
    mb::Core core(backend);
    tunnelmqtt::Api api(core);
    api.connect("0123456789abcdef");
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        char type;
        in >> type;
        if (type == 'T') {
            in >> now;
            core.tick(now);
            std::cout << "OK";
        } else if (type == 'Q') {
            auto raw = line.substr(2);
            tunnelmqtt::Request q;
            if (!tunnelmqtt::decode(raw.c_str(), raw.size(), q))
                std::cout << "{}";
            else {
                auto r = api.process(q, false, now);
                std::cout << "{\"api_version\":1,\"session\":\"" << q.session
                          << "\",\"epoch\":" << q.epoch << ",\"client\":\"" << q.client
                          << "\",\"id\":" << q.id << ",\"code\":" << unsigned(r.code)
                          << ",\"exception\":" << unsigned(r.exception)
                          << ",\"result\":" << r.result << ",\"sequence\":" << r.sequence << "}";
            }
        } else if (type == 'P') {
            unsigned peer;
            std::string hex;
            in >> peer >> hex;
            uint8_t q[260], r[260];
            size_t n = hex.size() / 2;
            if (n > 260)
                return 2;
            for (size_t i = 0; i < n; i++)
                q[i] = uint8_t(std::stoul(hex.substr(i * 2, 2), nullptr, 16));
            size_t out = core.pdu(uint8_t(peer), q, n, r);
            char b[3];
            for (size_t i = 0; i < out; i++) {
                snprintf(b, sizeof(b), "%02x", r[i]);
                std::cout << b;
            }
        } else if (type == 'S') {
            uint16_t v[96];
            api.snapshot(v);
            std::cout << "{\"api_version\":1,\"session\":\"" << api.session()
                      << "\",\"epoch\":" << api.generation() << ",\"ms\":" << now
                      << ",\"owner_client\":\"" << api.owner() << "\",\"registers\":[";
            for (int i = 0; i < 96; i++)
                std::cout << (i ? "," : "") << v[i];
            std::cout << "]}";
        } else if (type == 'D') {
            unsigned peer;
            in >> peer;
            core.resetPeer(uint8_t(peer));
            std::cout << "OK";
        } else if (type == 'N')
            std::cout << commandCount;
        else
            return 3;
        if (fence) {
            fence = false;
            api.fence();
        }
        std::cout << std::endl;
    }
}
