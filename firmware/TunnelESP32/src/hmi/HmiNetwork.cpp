#include "HmiNetwork.h"
#include "hmi.h"
#include <stdio.h>

namespace {
struct Field {
    uint16_t vp, bytes;
};
// Matches contracts/hmi_fields.json exactly.
const Field fields[] = {{0x2100, 32}, {0x2130, 32},  {0x2200, 32}, {0x2480, 16},
                        {0x2210, 16}, {0x2220, 16},  {0x2230, 16}, {0x2240, 16},
                        {0x2250, 16}, {0x2180, 128}, {0x2260, 16}, {0x2270, 32}};
void address(char *out, size_t size, const uint8_t *ip) {
    snprintf(out, size, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}
} // namespace

void HmiNetworkPublisher::poll(uint32_t now, const cfg::Settings &s, const HmiNetworkStatus &n) {
    if (sent && uint32_t(now - last) < 100)
        return;
    sent = true;
    last = now;
    char value[128] = {};
    const char *text = value;
    const bool online = n.link && n.hasIp;
    switch (field) {
    case 0:
        if (online)
            address(value, sizeof(value), n.ip);
        else
            text = n.link ? "WAIT IP" : "NO LINK";
        break;
    case 1:
        if (online)
            address(value, sizeof(value), n.mask);
        else
            text = "--";
        break;
    case 2:
        if (online)
            address(value, sizeof(value), n.gateway);
        else
            text = "--";
        break;
    case 3:
        text = n.fallback ? "FALLBACK" : s.dhcp ? "DHCP" : "STATIC";
        break;
    case 4:
        snprintf(value, sizeof(value), "%u", s.tcpPort);
        break;
    case 5:
    case 6:
        snprintf(value, sizeof(value), "%u", s.unit);
        break;
    case 7:
        snprintf(value, sizeof(value), "%lu", (unsigned long)s.baud);
        break;
    case 8:
        snprintf(value, sizeof(value), "8%c%u",
                 s.parity == 1   ? 'E'
                 : s.parity == 2 ? 'O'
                                 : 'N',
                 s.stopBits);
        break;
    case 9:
        text = s.host[0] ? s.host : "(not set)";
        break;
    case 10:
        snprintf(value, sizeof(value), "%u", s.mqttPort);
        break;
    case 11:
        text = !s.mqttEnabled    ? "OFF"
               : !n.link         ? "NO LINK"
               : !n.hasIp        ? "WAIT IP"
               : n.mqttConnected ? "CONNECTED"
                                 : "CONNECTING";
        break;
    }
    // On a full UART queue, retry this field at the next interval.
    if (Hmi_SendText(fields[field].vp, text, fields[field].bytes))
        field = (field + 1) % 12;
}
