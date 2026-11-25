#include "FotaProtocol.h"
#include "FotaImage.h"
#include "FotaHealth.h"
#include "ModbusCore.h"
#include <cassert>
#include <cstring>
#include <string>
#include <iostream>
using namespace fota;
static unsigned commands = 0, stops = 0;
static uint16_t read(uint16_t) {
    return 0;
}
static uint8_t command(uint8_t, uint16_t) {
    ++commands;
    return 0;
}
static bool reader(uint8_t, const uint8_t *, uint8_t) {
    return false;
}
static bool reply(uint8_t *, uint8_t *, uint8_t *) {
    return false;
}
static void heartbeat() {}
static void stop(bool) {
    ++stops;
}
int main() {
    assert(fenceMayStart(false,false,false)); // Our own HOLD fence proceeds.
    assert(!fenceMayStart(true,false,false)); // External STOP cancels.
    assert(!fenceMayStart(false,true,false));
    assert(!fenceMayStart(false,false,true));
    ImageIdentity identity=imageIdentity;
    identity.version=1161;
    for (unsigned split=0;split<=sizeof(identity);++split) {
        ImageScanner scan;
        auto data=reinterpret_cast<const uint8_t *>(&identity);
        scan.feed(data,split);scan.feed(data+split,sizeof(identity)-split);
        assert(scan.matches(1161));assert(!scan.matches(1162));
        scan.feed(data,sizeof(identity));assert(!scan.matches(1161));
    }
    identity.target[0]='X';
    ImageScanner wrong;
    wrong.feed(reinterpret_cast<const uint8_t *>(&identity),sizeof(identity));
    assert(!wrong.matches(1161));
    assert(bootDecision(11000,1000,11000,2000,true,true)==CONFIRM_BOOT);
    assert(bootDecision(11000,1000,10000,2000,true,true)==WAIT_HEALTH);
    assert(bootDecision(11000,1000,11000,2000,false,true)==WAIT_HEALTH);
    assert(bootDecision(11000,1000,11000,2000,true,false)==WAIT_HEALTH);
    assert(bootDecision(31000,1000,31000,2000,true,true)==ROLLBACK_BOOT);
    const uint32_t start=0xfffff000U,now=start+10000U;
    assert(bootDecision(now,start,now,start+1000U,true,true)==CONFIRM_BOOT);
    std::string payload = "{\"job\":\"" + std::string(32, 'a') + "\",\"session\":\"" +
                          std::string(16, 'b') + "\",\"client\":\"" + std::string(16, 'c') +
                          "\",\"url\":\"https://192.168.1.18:8443/firmware/"
                          "v.bin\",\"target\":\"tunnel-esp32s3-v1\",\"sha256\":\"" +
                          std::string(64, 'd') + "\",\"signature\":\"" + std::string(140, 'e') +
                          "\",\"epoch\":3,\"id\":1,\"seen_ms\":100,\"version\":1131,\"size\":"
                          "1000000,\"unix_time\":1794340000}";
    Request q;
    assert(decode(payload.data(), payload.size(), q));
    auto validateQ = [&](Request x, uint32_t now = 200) {
        return validate(x, "bbbbbbbbbbbbbbbb", 3, now, 1130, 0x330000,
                        "https://192.168.1.18:8443/firmware/");
    };
    assert(!validateQ(q));
    assert(!validateQ(q, 5100));
    assert(!strcmp(validateQ(q, 5101), "stale"));
    Request x = q;
    x.epoch = 4;
    assert(!strcmp(validateQ(x), "session"));
    x = q;
    x.version = 1130;
    assert(!validateQ(x)); // Same version: reinstall is allowed.
    x.version = 1129;
    assert(!validateQ(x)); // Older version: signed downgrade is allowed.
    x.version = 0;
    assert(!strcmp(validateQ(x), "version"));
    x = q;
    x.size = 0x330001;
    assert(!strcmp(validateQ(x), "size"));
    x = q;
    strcpy(x.url, "http://192.168.1.18:8443/firmware/v.bin");
    assert(!strcmp(validateQ(x), "url"));
    x = q;
    strcpy(x.url, "https://192.168.1.18:8443/firmware/../v.bin");
    assert(!strcmp(validateQ(x), "url"));
    x = q;
    strcpy(x.url, "https://192.168.1.18.evil/firmware/v.bin");
    assert(!strcmp(validateQ(x), "url"));
    for (const char *leaf : {"%2e%2e/v.bin", "sub/v.bin", "v.bin?redirect=x", ""}) {
        x=q;strcpy(x.url,"https://192.168.1.18:8443/firmware/");strcat(x.url,leaf);
        assert(!strcmp(validateQ(x),"url"));
    }
    x = q;
    strcpy(x.target, "other");
    assert(!strcmp(validateQ(x), "target"));
    x = q;
    x.unix_time = 0;
    assert(!strcmp(validateQ(x), "time"));
    assert(!decode(payload.data(), payload.size() - 1, x));
    std::string bad = payload;
    bad.insert(1, "\"id\":2,");
    assert(!decode(bad.data(), bad.size(), x));
    bad = payload + "garbage";
    assert(!decode(bad.data(), bad.size(), x));
    bad = payload;
    bad.replace(bad.find("1000000"), 7, "1.5");
    assert(!decode(bad.data(), bad.size(), x));
    bad = "{\"x\":" + std::string(1000, '[');
    assert(!decode(bad.data(), bad.size(), x));
    bad = payload;
    bad.insert(5, "\\u0000");
    assert(!decode(bad.data(), bad.size(), x));
    bad = std::string(1537, ' ');
    assert(!decode(bad.data(), bad.size(), x));
    char text[768];
    assert(canonical(q, text, sizeof(text)));
    assert(strstr(text, "TUNNEL-FOTA-1\n") == text);
    assert(!canonical(q, text, 10));
    Replay cache;
    assert(cache.check(q) == 0);
    cache.remember(q);
    assert(cache.check(q) == 1);
    x = q;
    x.size++;
    assert(cache.check(x) == -1);
    x = q;
    x.id = 2;
    assert(cache.check(x) == 0);
    cache.remember(x);
    assert(cache.check(q) == -1);
    cache.reset();
    assert(cache.check(q) == 0);
    for (unsigned i = 0; i < 8; i++) {
        x = q;
        x.client[0] = '0' + i;
        cache.remember(x);
    }
    x = q;
    x.client[0] = 'f';
    assert(cache.check(x) == -2);
    mb::Backend backend = {read, command, reader, reply, heartbeat, stop};
    mb::Core core(backend);
    core.inhibit(true);
    uint8_t out[260], claim[] = {6, 1, 0, 0, 1}, emergency[] = {6, 1, 1, 0, 1},
                      normalStop[] = {6, 1, 2, 0, 1}, get[] = {4, 0, 0, 0, 1};
    assert(core.pdu(mb::MQTT, claim, sizeof(claim), out) == 2 && out[1] == mb::EX_BUSY);
    assert(commands == 0);
    assert(core.pdu(mb::MQTT, get, sizeof(get), out) == 4);
    assert(core.pdu(mb::MQTT, emergency, sizeof(emergency), out) == 5 && stops == 1);
    assert(core.pdu(mb::RTU, normalStop, sizeof(normalStop), out) == 5 && stops == 2);
    core.inhibit(false);
    assert(core.pdu(mb::MQTT, claim, sizeof(claim), out) == 5 && commands == 1);
    std::cout << "PASS FOTA bounded parser, canonical envelope, token/target/size/URL/version "
                 "guards, replay cache and shared write inhibit with STOP/read access\n";
}
