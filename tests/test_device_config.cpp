#include "DeviceConfig.h"
#include "ModbusCore.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
static cfg::Record slots[2];
static bool exists[2] = {};
static int fault = 0;
static unsigned writes = 0;
static bool readSlot(unsigned n, cfg::Record &r) {
    if (!exists[n])
        return false;
    r = slots[n];
    return true;
}
static bool writeSlot(unsigned n, const cfg::Record &r) {
    ++writes;
    if (fault == 1)
        return false;
    slots[n] = r;
    exists[n] = true;
    if (fault == 2)
        slots[n].crc ^= 1;
    return fault == 0;
}
static cfg::Storage storage = {readSlot, writeSlot};
int main() {
    cfg::Store store(storage);
    assert(!store.load());
    auto s = store.get();
    assert(s.dhcp && s.baud == 19200 && s.unit == 1 && !s.mqttEnabled && !cfg::validate(s));
    assert(store.save(s) && store.revision() == 1);
    s.mqttEnabled = 1;
    strcpy(s.host, "mqtt.example.com");
    strcpy(s.password, "private-secret");
    assert(store.save(s) && store.revision() == 2);
    cfg::Store reboot(storage);
    assert(reboot.load() && !strcmp(reboot.get().password, "private-secret"));
    auto candidate = s;
    candidate.unit = 27;
    fault = 2;
    assert(!store.save(candidate));
    cfg::Store torn(storage);
    assert(torn.load() && torn.get().unit == 1 && torn.revision() == 2);
    fault = 1;
    assert(!store.save(candidate));
    fault = 0;
    assert(store.save(candidate));
    cfg::Store saved(storage);
    assert(saved.load() && saved.get().unit == 27);
    auto corrupted = cfg::record(candidate, 100);
    corrupted.schema = 2;
    assert(!cfg::valid(corrupted));
    auto check = [&](std::string text, const cfg::Settings &base) {
        cfg::Update u = {};
        return cfg::parse(text.c_str(), text.size(), base, u);
    };
    assert(!strcmp(check("{\"revision\":3}", s), "admin_password_required"));
    s.adminSet = 1;
    assert(!check("{\"revision\":3,\"mqtt_host\":\"new-broker.local\"}", s));
    cfg::Update u = {};
    std::string json = "{\"revision\":3,\"mqtt_host\":\"new-broker.local\"}";
    assert(!cfg::parse(json.c_str(), json.size(), s, u) &&
           !strcmp(u.settings.password, s.password));
    json = "{\"revision\":3,\"mqtt_password\":\"\"}";
    assert(!cfg::parse(json.c_str(), json.size(), s, u) && !u.settings.password[0]);
    assert(check("{\"revision\":3,\"unit\":0}", s));
    assert(check("{\"revision\":3,\"unit\":248}", s));
    assert(check("{\"revision\":3,\"unit\":1.5}", s));
    assert(check("{\"revision\":3,\"tcp_port\":80}", s));
    assert(check("{\"revision\":3,\"baud\":12345}", s));
    assert(!check("{\"revision\":3,\"baud\":115200,\"parity\":2,\"stop_bits\":2}", s));
    assert(check("{\"revision\":3,\"mqtt_root\":\"bad/#\"}", s));
    assert(check("{\"revision\":3,\"mqtt_host\":\"mqtt://server\"}", s));
    assert(check("{\"revision\":3,\"mqtt_enabled\":true,\"mqtt_host\":\"\"}", s));
    assert(check("{\"revision\":3,\"unit\":1,\"unit\":2}", s));
    assert(check("{\"revision\":3,\"unused\":0}", s));
    assert(check("{\"revision\":3,\"mqtt_host\":\"abc\\u0000ignored\"}", s));
    assert(check("{\"a\":[[[[[0]]]]]}", s));
    assert(check(std::string(2049, ' '), s));
    assert(check("{\"revision\":3} trailing", s));
    auto net = cfg::defaults(true);
    net.mask[1] = 0;
    assert(cfg::validate(net));
    net = cfg::defaults(true);
    net.ip[3] = 255;
    assert(cfg::validate(net));
    net = cfg::defaults(true);
    net.gateway[0] = 10;
    assert(cfg::validate(net));
    net = cfg::defaults(true);
    net.ip[0] = 127;
    assert(cfg::validate(net));
    char out[1024];
    assert(cfg::json(s, 3, out, sizeof(out)));
    assert(!strstr(out, "private-secret") && !strstr(out, "adminHash") &&
           strstr(out, "mqtt_password_set"));
    for (unsigned bits = 0; bits < 16; bits++) {
        cfg::Safety safety = {bool(bits & 1), bool(bits & 2), bool(bits & 4), bool(bits & 8)};
        assert(cfg::canApply(safety) == (bits == 0));
    }
    unsigned before = writes;
    candidate.unit = 0;
    assert(!store.save(candidate) && writes == before);
    // Recovery does not retain broker credentials and has a known reachable IP.
    auto recovery = cfg::defaults(true);
    assert(!recovery.dhcp && recovery.ip[3] == 1 && !recovery.password[0] && !recovery.mqttEnabled);
    puts("PASS config: defaults, dual-slot persistence/reboot/torn writes, strict partial updates, "
         "secret redaction, IP/serial validation, all 16 apply safety states");
}
