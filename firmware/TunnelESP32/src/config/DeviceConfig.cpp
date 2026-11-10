#include "DeviceConfig.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <cJSON.h>
namespace cfg {
Settings defaults(bool recovery) {
    Settings s = {};
    s.dhcp = recovery ? 0 : 1;
    s.unit = 1;
    s.parity = 1;
    s.stopBits = 1;
    s.baud = 19200;
    s.tcpPort = 502;
    s.mqttPort = 1883;
    const uint8_t ip[] = {192, 168, 4, 1}, mask[] = {255, 255, 255, 0};
    memcpy(s.ip, ip, 4);
    memcpy(s.mask, mask, 4);
    strcpy(s.root, "tunnel/01/v1");
    return s;
}
static uint32_t ipNumber(const uint8_t *p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
static bool unicast(uint32_t v) {
    return (v >> 24) > 0 && (v >> 24) < 224 && (v >> 24) != 127;
}
static bool ascii(const char *p, size_t cap, bool host = false) {
    size_t n = 0;
    for (; n < cap && p[n]; n++) {
        unsigned char c = (unsigned char)p[n];
        if (c < 32 || c > 126)
            return false;
        if (host && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '.' || c == '-'))
            return false;
    }
    return n < cap;
}
const char *validate(const Settings &s) {
    if (s.dhcp > 1 || s.mqttEnabled > 1 || s.adminSet > 1)
        return "flags";
    if (s.unit < 1 || s.unit > 247)
        return "unit";
    if (s.parity > 2 || (s.stopBits != 1 && s.stopBits != 2))
        return "serial_format";
    switch (s.baud) {
    case 1200:
    case 2400:
    case 4800:
    case 9600:
    case 19200:
    case 38400:
    case 57600:
    case 115200:
        break;
    default:
        return "baud";
    }
    if (!s.tcpPort || s.tcpPort == 80)
        return "tcp_port";
    if (!s.mqttPort)
        return "mqtt_port";
    if (!s.dhcp) {
        uint32_t ip = ipNumber(s.ip), mask = ipNumber(s.mask), inverse = ~mask,
                 gw = ipNumber(s.gateway), dns = ipNumber(s.dns);
        if (!mask || inverse < 3 || (inverse & (inverse + 1)))
            return "mask";
        if (!unicast(ip) || !(ip & inverse) || (ip & inverse) == inverse)
            return "ip";
        if (gw && (!unicast(gw) || (gw & mask) != (ip & mask) || !(gw & inverse) ||
                   (gw & inverse) == inverse || gw == ip))
            return "gateway";
        if (dns && !unicast(dns))
            return "dns";
    }
    if (!ascii(s.host, sizeof(s.host), true))
        return "mqtt_host";
    size_t n = strlen(s.host);
    if (n && (s.host[0] == '.' || s.host[0] == '-' || s.host[n - 1] == '.' ||
              s.host[n - 1] == '-' || strstr(s.host, "..")))
        return "mqtt_host";
    if (s.mqttEnabled && !n)
        return "mqtt_host";
    if (!ascii(s.user, sizeof(s.user)) || !ascii(s.password, sizeof(s.password)))
        return "mqtt_credentials";
    if (!ascii(s.root, sizeof(s.root)) || !s.root[0])
        return "mqtt_root";
    n = strlen(s.root);
    if (s.root[0] == '/' || s.root[n - 1] == '/' || strstr(s.root, "//"))
        return "mqtt_root";
    for (size_t i = 0; i < n; i++) {
        char c = s.root[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '/' || c == '-' || c == '_' || c == '.'))
            return "mqtt_root";
    }
    return nullptr;
}
uint32_t checksum(const void *data, size_t length) {
    const auto *p = static_cast<const uint8_t *>(data);
    uint32_t c = 0xffffffff;
    for (size_t i = 0; i < length; i++) {
        c ^= p[i];
        for (unsigned b = 0; b < 8; b++)
            c = (c >> 1) ^ ((c & 1) ? 0xedb88320 : 0);
    }
    return ~c;
}
Record record(const Settings &s, uint32_t revision) {
    Record r = {};
    r.magic = 0x54434647;
    r.schema = 1;
    r.bytes = sizeof(Settings);
    r.revision = revision;
    r.settings = s;
    r.crc = checksum(&r, offsetof(Record, crc));
    return r;
}
bool valid(const Record &r) {
    return r.magic == 0x54434647 && r.schema == 1 && r.bytes == sizeof(Settings) &&
           r.crc == checksum(&r, offsetof(Record, crc)) && !validate(r.settings);
}
bool Store::load() {
    Record a = {}, b = {};
    bool va = io.read(0, a) && valid(a), vb = io.read(1, b) && valid(b);
    if (!va && !vb) {
        current = record(defaults(), 0);
        slot = 1;
        return false;
    }
    slot = vb && (!va || int32_t(b.revision - a.revision) > 0) ? 1 : 0;
    current = slot ? b : a;
    return true;
}
bool Store::save(const Settings &s) {
    if (validate(s))
        return false;
    Record next = record(s, current.revision + 1), verify = {};
    unsigned target = 1 - slot;
    io.write(target, next);
    if (!io.read(target, verify) || !valid(verify) || memcmp(&next, &verify, sizeof(next)))
        return false;
    current = next;
    slot = target;
    return true;
}
bool canApply(const Safety &s) {
    return !s.owner && !s.moving && !s.scanning && !s.readerPending;
}
static bool number(cJSON *p, double max, uint32_t &v) {
    if (!cJSON_IsNumber(p) || !isfinite(p->valuedouble) || p->valuedouble < 0 ||
        p->valuedouble > max || floor(p->valuedouble) != p->valuedouble)
        return false;
    v = uint32_t(p->valuedouble);
    return true;
}
static bool str(cJSON *p, char *dest, size_t cap) {
    if (!cJSON_IsString(p) || strlen(p->valuestring) >= cap)
        return false;
    strcpy(dest, p->valuestring);
    return true;
}
static bool ipv4(cJSON *p, uint8_t *dest) {
    if (!cJSON_IsString(p))
        return false;
    const char *s = p->valuestring;
    for (unsigned i = 0; i < 4; i++) {
        if (*s < '0' || *s > '9')
            return false;
        unsigned v = 0, digits = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10 + unsigned(*s++ - '0');
            if (++digits > 3 || v > 255)
                return false;
        }
        dest[i] = uint8_t(v);
        if (i < 3) {
            if (*s++ != '.')
                return false;
        } else if (*s)
            return false;
    }
    return true;
}
const char *parse(const char *text, size_t length, const Settings &base, Update &out) {
    if (!length || length > 2048 || memchr(text, 0, length))
        return "json";
    // Limit nesting before parsing; reject NUL escapes in C-string fields.
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < length; i++) {
        char c = text[i];
        if (quoted && escaped) {
            escaped = false;
            continue;
        }
        if (quoted && c == '\\') {
            if (i + 5 < length && text[i + 1] == 'u' && !strncmp(text + i + 2, "0000", 4))
                return "json";
            escaped = true;
            continue;
        }
        if (c == '"')
            quoted = !quoted;
        if (!quoted) {
            if (c == '{' || c == '[') {
                if (++depth > 1)
                    return "json";
            }
            if (c == '}' || c == ']') {
                if (!depth)
                    return "json";
                --depth;
            }
        }
    }
    if (depth || quoted)
        return "json";
    char buffer[2049];
    memcpy(buffer, text, length);
    buffer[length] = 0;
    cJSON *o = cJSON_ParseWithLengthOpts(buffer, length + 1, nullptr, true);
    if (!o)
        return "json";
    Update u = {};
    u.settings = base;
    const char *error = nullptr;
    bool gotRevision = false;
    if (!cJSON_IsObject(o))
        error = "json";
    for (cJSON *p = o->child; p && !error; p = p->next) {
        const char *k = p->string;
        if (!k) {
            error = "json";
            break;
        }
        for (cJSON *q = p->next; q; q = q->next)
            if (q->string && !strcmp(k, q->string))
                error = "duplicate_field";
        if (error)
            break;
        uint32_t n = 0;
        if (!strcmp(k, "revision")) {
            gotRevision = number(p, 4294967295.0, u.revision);
            if (!gotRevision)
                error = "revision";
        } else if (!strcmp(k, "dhcp")) {
            if (!cJSON_IsBool(p))
                error = "dhcp";
            else
                u.settings.dhcp = cJSON_IsTrue(p);
        } else if (!strcmp(k, "mqtt_enabled")) {
            if (!cJSON_IsBool(p))
                error = "mqtt_enabled";
            else
                u.settings.mqttEnabled = cJSON_IsTrue(p);
        } else if (!strcmp(k, "unit")) {
            if (!number(p, 247, n))
                error = "unit";
            else
                u.settings.unit = uint8_t(n);
        } else if (!strcmp(k, "baud")) {
            if (!number(p, 115200, n))
                error = "baud";
            else
                u.settings.baud = n;
        } else if (!strcmp(k, "parity")) {
            if (!number(p, 2, n))
                error = "parity";
            else
                u.settings.parity = uint8_t(n);
        } else if (!strcmp(k, "stop_bits")) {
            if (!number(p, 2, n))
                error = "stop_bits";
            else
                u.settings.stopBits = uint8_t(n);
        } else if (!strcmp(k, "tcp_port")) {
            if (!number(p, 65535, n))
                error = "tcp_port";
            else
                u.settings.tcpPort = uint16_t(n);
        } else if (!strcmp(k, "mqtt_port")) {
            if (!number(p, 65535, n))
                error = "mqtt_port";
            else
                u.settings.mqttPort = uint16_t(n);
        } else if (!strcmp(k, "ip")) {
            if (!ipv4(p, u.settings.ip))
                error = "ip";
        } else if (!strcmp(k, "mask")) {
            if (!ipv4(p, u.settings.mask))
                error = "mask";
        } else if (!strcmp(k, "gateway")) {
            if (!ipv4(p, u.settings.gateway))
                error = "gateway";
        } else if (!strcmp(k, "dns")) {
            if (!ipv4(p, u.settings.dns))
                error = "dns";
        } else if (!strcmp(k, "mqtt_host")) {
            if (!str(p, u.settings.host, sizeof(u.settings.host)))
                error = "mqtt_host";
        } else if (!strcmp(k, "mqtt_user")) {
            if (!str(p, u.settings.user, sizeof(u.settings.user)))
                error = "mqtt_user";
        } else if (!strcmp(k, "mqtt_password")) {
            if (!str(p, u.settings.password, sizeof(u.settings.password)))
                error = "mqtt_password";
        } else if (!strcmp(k, "mqtt_root")) {
            if (!str(p, u.settings.root, sizeof(u.settings.root)))
                error = "mqtt_root";
        } else if (!strcmp(k, "admin_password")) {
            if (!str(p, u.adminPassword, sizeof(u.adminPassword)) || strlen(u.adminPassword) < 10 ||
                !ascii(u.adminPassword, sizeof(u.adminPassword)))
                error = "admin_password";
        } else
            error = "unknown_field";
    }
    cJSON_Delete(o);
    if (!error && !gotRevision)
        error = "revision";
    if (!error && !base.adminSet && !u.adminPassword[0])
        error = "admin_password_required";
    if (!error)
        error = validate(u.settings);
    if (!error)
        out = u;
    memset(u.adminPassword, 0, sizeof(u.adminPassword));
    return error;
}
bool json(const Settings &s, uint32_t revision, char *out, size_t capacity) {
    cJSON *o = cJSON_CreateObject();
    if (!o)
        return false;
    cJSON_AddNumberToObject(o, "revision", revision);
    cJSON_AddBoolToObject(o, "dhcp", s.dhcp);
    cJSON_AddNumberToObject(o, "unit", s.unit);
    cJSON_AddNumberToObject(o, "baud", s.baud);
    cJSON_AddNumberToObject(o, "parity", s.parity);
    cJSON_AddNumberToObject(o, "stop_bits", s.stopBits);
    cJSON_AddNumberToObject(o, "tcp_port", s.tcpPort);
    cJSON_AddNumberToObject(o, "mqtt_port", s.mqttPort);
    cJSON_AddBoolToObject(o, "mqtt_enabled", s.mqttEnabled);
    cJSON_AddStringToObject(o, "mqtt_host", s.host);
    cJSON_AddStringToObject(o, "mqtt_user", s.user);
    cJSON_AddStringToObject(o, "mqtt_root", s.root);
    cJSON_AddBoolToObject(o, "mqtt_password_set", s.password[0] != 0);
    cJSON_AddBoolToObject(o, "admin_set", s.adminSet);
    const char *names[] = {"ip", "mask", "gateway", "dns"};
    const uint8_t *addresses[] = {s.ip, s.mask, s.gateway, s.dns};
    for (unsigned i = 0; i < 4; i++) {
        char ip[16];
        const uint8_t *p = addresses[i];
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
        cJSON_AddStringToObject(o, names[i], ip);
    }
    bool ok = cJSON_PrintPreallocated(o, out, int(capacity), false);
    cJSON_Delete(o);
    return ok;
}
} // namespace cfg
