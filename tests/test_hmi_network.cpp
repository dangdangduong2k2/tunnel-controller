#include "HmiNetwork.h"
#include "hmi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static unsigned calls;
static bool ready = true;
static uint16_t vp, bytes;
static char value[140];
extern "C" uint8_t Hmi_SendText(uint16_t v, const char *s, uint16_t n) {
    ++calls;
    vp = v;
    bytes = n;
    snprintf(value, sizeof(value), "%s", s);
    return ready;
}
static const uint16_t addresses[] = {0x2100, 0x2130, 0x2200, 0x2480, 0x2210, 0x2220,
                                     0x2230, 0x2240, 0x2250, 0x2180, 0x2260, 0x2270};
static const uint16_t capacities[] = {32, 32, 32, 16, 16, 16, 16, 16, 16, 128, 16, 32};
static void cycle(HmiNetworkPublisher &p, uint32_t &t, const cfg::Settings &s,
                  const HmiNetworkStatus &n, const char *const *expect) {
    for (unsigned i = 0; i < 12; i++) {
        unsigned before = calls;
        p.poll(t, s, n);
        CHECK(calls == before + 1);
        CHECK(vp == addresses[i] && bytes == capacities[i] && !strcmp(value, expect[i]));
        p.poll(t + 99, s, n);
        CHECK(calls == before + 1);
        t += 100;
    }
}
int main() {
    cfg::Settings s = {};
    s.dhcp = 1;
    s.unit = 1;
    s.parity = 1;
    s.stopBits = 1;
    s.baud = 19200;
    s.tcpPort = 502;
    s.mqttPort = 1883;
    strcpy(s.host, "broker.example.com");
    strcpy(s.password, "secret-not-for-screen");
    HmiNetworkStatus n = {
        true, true, false, true, {192, 168, 1, 211}, {255, 255, 255, 0}, {192, 168, 1, 1}};
    s.mqttEnabled = 1;
    HmiNetworkPublisher p;
    uint32_t t = 0;
    const char *online[] = {
        "192.168.1.211", "255.255.255.0", "192.168.1.1",        "DHCP", "502",      "1", "1",
        "19200",         "8E1",           "broker.example.com", "1883", "CONNECTED"};
    cycle(p, t, s, n, online);
    // Refresh unchanged values after DWIN reset, including independent clock wrap.
    cycle(p, t, s, n, online);
    n.link = false;
    const char *offline[] = {"NO LINK", "--",     "--",    "DHCP", "502",
                             "1",       "1",      "19200", "8E1",  "broker.example.com",
                             "1883",    "NO LINK"};
    cycle(p, t, s, n, offline); // Old DHCP address must disappear when disconnected.
    n.link = true;
    n.hasIp = false;
    offline[0] = "WAIT IP";
    offline[11] = "WAIT IP";
    cycle(p, t, s, n, offline);
    n.hasIp = true;
    n.fallback = true;
    n.mqttConnected = false;
    n.ip[2] = 4;
    n.ip[3] = 1;
    memset(n.gateway, 0, 4);
    online[0] = "192.168.4.1";
    online[2] = "0.0.0.0";
    online[3] = "FALLBACK";
    online[11] = "CONNECTING";
    cycle(p, t, s, n, online);
    s.dhcp = 0;
    n.fallback = false;
    s.mqttEnabled = 0;
    s.host[0] = 0;
    s.parity = 0;
    s.stopBits = 2;
    s.unit = 247;
    s.baud = 115200;
    s.tcpPort = 1502;
    online[3] = "STATIC";
    online[4] = "1502";
    online[5] = online[6] = "247";
    online[7] = "115200";
    online[8] = "8N2";
    online[9] = "(not set)";
    online[11] = "OFF";
    cycle(p, t, s, n, online);
    s.parity = 2;
    online[8] = "8O2";
    cycle(p, t, s, n, online);
    ready = false;
    p.poll(t, s, n);
    CHECK(vp == 0x2100);
    t += 100;
    ready = true;
    p.poll(t, s, n);
    CHECK(vp == 0x2100);
    t += 100;
    p.poll(t, s, n);
    CHECK(vp == 0x2130);
    HmiNetworkPublisher wrap;
    t = 0xffffffd0U;
    cycle(wrap, t, s, n, online);
    puts("HMI network passed: exact VP map, live/offline/fallback, serial settings, all MQTT "
         "states, refresh, UART retry, timer wrap");
}
