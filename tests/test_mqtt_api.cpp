#include "MqttApi.h"
#include "OperationCatalog.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
static unsigned commands = 0, beats = 0, stops = 0, emergencies = 0, readerWrites = 0;
static uint8_t readerCommand = 0, readerData[5] = {};
static bool ready = false;
static uint16_t read(uint16_t a) {
    return a;
}
static uint8_t command(uint8_t, uint16_t) {
    ++commands;
    return 0;
}
static bool reader(uint8_t c, const uint8_t *p, uint8_t n) {
    ++readerWrites;
    readerCommand = c;
    memcpy(readerData, p, n);
    return true;
}
static bool reply(uint8_t *p, uint8_t *n, uint8_t *s) {
    if (!ready)
        return false;
    ready = false;
    *n = 2;
    *s = 0;
    p[0] = readerCommand;
    p[1] = 0;
    return true;
}
static void beat() {
    ++beats;
}
static void stop(bool e) {
    ++stops;
    emergencies += e;
}
static mb::Backend backend = {read, command, reader, reply, beat, stop};
static uint8_t single(mb::Core &c, uint8_t peer, uint16_t a, uint16_t v) {
    uint8_t q[] = {6, uint8_t(a >> 8), uint8_t(a), uint8_t(v >> 8), uint8_t(v)}, r[260];
    return c.pdu(peer, q, 5, r) == 5 ? 0 : r[1];
}
static tunnelmqtt::Request request(tunnelmqtt::Api &api, unsigned id, tunnelmqtt::Action action) {
    tunnelmqtt::Request q;
    strcpy(q.session, api.session());
    strcpy(q.client, "1111111111111111");
    q.epoch = api.generation();
    q.id = id;
    q.action = action;
    q.seen = 100;
    return q;
}
static void arbitration() {
    mb::Core c(backend);
    tunnelmqtt::Api api(c);
    api.connect("0123456789abcdef");
    c.tick(100);
    auto claim = request(api, 1, tunnelmqtt::CLAIM);
    assert(api.process(claim, true, 100).code == 4 && !c.active());
    auto bad = claim;
    bad.session[0] = 'f';
    assert(api.process(bad, false, 100).code == 2);
    assert(api.process(claim, false, 1601).code == 3 && !c.active());
    assert(api.process(claim, false, 100).code == 0 && c.owner() == mb::MQTT);
    assert(single(c, mb::RTU, 256, 1) == 6 && single(c, 2, 256, 1) == 6 && !c.legacyAllow(0x18));
    unsigned n = commands, b = beats;
    assert(api.process(claim, false, 100).code == 0 && commands == n && beats == b);
    auto foreign = request(api, 1, tunnelmqtt::CLAIM);
    foreign.client[0] = '2';
    assert(api.process(foreign, false, 100).code == 5);
    auto op = request(api, 2, tunnelmqtt::EXECUTE);
    op.op = 8;
    op.args[0] = 3;
    auto result = api.process(op, false, 100);
    assert(result.code == 0 && result.result == 0 && result.sequence == 1);
    n = commands;
    assert(api.process(op, false, 100).code == 0 && commands == n);
    op.args[0] = 4;
    assert(api.process(op, false, 100).code == 7 && commands == n);
    op = request(api, 3, tunnelmqtt::EXECUTE);
    op.op = 22;
    op.args[0] = 30;
    op.args[1] = 29;
    op.args[2] = 28;
    op.args[3] = 27;
    op.args[4] = 1;
    assert(api.process(op, false, 100).result == 0 && readerWrites == 1 && readerCommand == 0x2F &&
           readerData[0] == 158);
    auto next = request(api, 4, tunnelmqtt::EXECUTE);
    next.op = 21;
    assert(api.process(next, false, 100).exception == 6);
    ready = true;
    c.tick(200);
    uint16_t regs[96];
    api.snapshot(regs);
    assert(regs[20] == 2 && regs[32] == 0x2F);
    // Expired owner cannot revive with a delayed heartbeat, even if state is fresh.
    c.tick(3100);
    assert(!c.active());
    auto heartbeat = request(api, 5, tunnelmqtt::HEARTBEAT);
    heartbeat.seen = 3100;
    assert(api.process(heartbeat, false, 3100).code == 5);
    assert(single(c, 2, 256, 1) == 0);
    api.disconnect();
    assert(c.owner() == 2); // broker failure must not stop TCP owner
    auto newClaim = request(api, 1, tunnelmqtt::CLAIM);
    newClaim.seen = 3100;
    assert(api.process(newClaim, false, 3100).code == 5);
    assert(single(c, mb::RTU, 258, 1) == 0 && !c.active()); // STOP from non-owner
    api.fence();
    assert(api.process(newClaim, false, 3100).code == 2);
    c.tick(3200);
    newClaim = request(api, 1, tunnelmqtt::CLAIM);
    newClaim.seen = 3200;
    assert(api.process(newClaim, false, 3200).code == 0);
    api.fence();
    assert(api.process(newClaim, false, 3200).code == 2); // queued pre-STOP claim rejected
    auto emergency = request(api, 1, tunnelmqtt::EMERGENCY);
    emergency.client[0] = 'f';
    emergency.seen = 3200;
    assert(api.process(emergency, false, 3200).code == 0 && emergencies == 1);
    api.disconnect();
    assert(!c.active());
    api.connect("fedcba9876543210");
    assert(api.process(emergency, false, 3200).code == 2);
}
static void json() {
    const std::string base = "{\"session\":\"0123456789abcdef\",\"client\":\"1111111111111111\","
                             "\"epoch\":1,\"id\":1,\"seen_ms\":100,\"action\":\"claim\"}";
    tunnelmqtt::Request q;
    assert(tunnelmqtt::decode(base.c_str(), base.size(), q) && q.action == tunnelmqtt::CLAIM);
    auto check = [&](std::string s) { return tunnelmqtt::decode(s.c_str(), s.size(), q); };
    assert(!check(base + "x"));
    assert(!check(base.substr(0, base.size() - 1) + ",\"id\":2}"));
    assert(!check(base.substr(0, base.size() - 1) + ",\"extra\":0}"));
    auto bad = base;
    bad.replace(bad.find("\"id\":1"), 6, "\"id\":-1");
    assert(!check(bad));
    bad = base;
    bad.replace(bad.find("\"id\":1"), 6, "\"id\":1.5");
    assert(!check(bad));
    bad = base;
    bad.replace(bad.find("\"id\":1"), 6, "\"id\":4294967296");
    assert(!check(bad));
    assert(!check("{\"a\":[[[[[[0]]]]]]}"));
    assert(!check(std::string(513, ' ')));
    bad = base;
    bad.replace(bad.find("claim"), 5, "claim\\u0000bad");
    assert(!check(bad));
    bad = base;
    bad.replace(bad.find("claim"), 5, "execute");
    bad.pop_back();
    bad += ",\"op\":22,\"args\":[30,29,28,27,1,0]}";
    assert(check(bad) && q.op == 22 && q.args[4] == 1);
    bad.replace(bad.find("30,29"), 5, "65536,29");
    assert(!check(bad));
}
static void namedJson() {
    const std::string prefix = "{\"session\":\"0123456789abcdef\",\"client\":\"1111111111111111\","
                               "\"epoch\":1,\"id\":1,\"seen_ms\":100,";
    tunnelmqtt::Request q;
    auto check = [&](std::string s) { return tunnelmqtt::decode(s.c_str(), s.size(), q); };
    for (const auto &op : tunnelcontract::OPERATIONS) {
        std::string params = "{";
        for (unsigned i = 0; i < op.count; i++)
            params += (i ? "," : "") + std::string("\"") + op.params[i].name +
                      "\":" + std::to_string(op.params[i].maximum);
        params += "}";
        auto raw = prefix + "\"method\":\"" + op.method + "\",\"params\":" + params + "}";
        assert(check(raw) && q.action == tunnelmqtt::EXECUTE && q.op == op.op);
        for (unsigned i = 0; i < 6; i++)
            assert(q.args[i] == (i < op.count ? op.params[i].maximum : 0));
        assert(!check(raw.substr(0, raw.size() - 1) + ",\"action\":\"execute\"}"));
    }
    const std::string speed = prefix + "\"method\":\"speed\",\"params\":";
    for (auto p : {"{}", "{\"level\":0}", "{\"level\":6}", "{\"level\":true}", "{\"level\":2.5}",
                   "{\"level\":3,\"level\":3}", "{\"level\":3,\"extra\":0}", "[]", "null"})
        assert(!check(speed + p + "}"));
    assert(!check(prefix + "\"method\":\"missing\",\"params\":{}}"));
    assert(!tunnelmqtt::decode(nullptr, 3, q));
    mb::Core c(backend);
    tunnelmqtt::Api api(c);
    api.connect("0123456789abcdef");
    tunnelmqtt::ReadRequest read;
    strcpy(read.client, "1111111111111111");
    read.id = 1;
    char state[1400];
    assert(tunnelmqtt::encodeState(api, read, 100, state, sizeof(state)));
    assert(strstr(state, "\"epc_count\":10") && strstr(state, "\"machine_state\":2"));
    char tiny[32];
    assert(!tunnelmqtt::encodeState(api, read, 100, tiny, sizeof(tiny)));
}
int main() {
    arbitration();
    json();
    namedJson();
    puts("PASS MQTT: strict numeric/named JSON, all catalog methods, correlation fields, bounds, "
         "arbitration, lease, replay, fence, reconnect, antenna and reader results");
}
