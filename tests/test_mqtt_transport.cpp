#include "Version.h"
#include "mqtt_transport_stubs.h"
#include "EspMqtt.h"
#include "EspConfig.h"
#include "MqttApi.h"
#include <cJSON.h>
#include <cassert>
#include <iostream>
#include <sstream>
static unsigned commandCount = 0, beatCount = 0;
static bool fence = false, readerPending = false;
static uint8_t readerCmd = 0;
static uint32_t readerReadyAt = 0;
static bool readerReject = false;
static uint16_t read(uint16_t a) {
    if (a == 0)
        return 3;
    if (a == 1)
        return TUNNEL_FIRMWARE_REGISTER;
    if (a == 10)
        return 7;
    if (a == 15)
        return 1;
    if (a == 24)
        return 7;
    if (a == 29)
        return 1;
    return 0;
}
static uint8_t command(uint8_t, uint16_t) {
    ++commandCount;
    return 0;
}
static bool reader(uint8_t cmd, const uint8_t *, uint8_t) {
    ++commandCount;
    readerPending = true;
    readerCmd = cmd;
    readerReadyAt = fakeTime + 800;
    return true;
}
static bool reply(uint8_t *p, uint8_t *n, uint8_t *s) {
    if (!readerPending || int32_t(fakeTime - readerReadyAt) < 0)
        return false;
    readerPending = false;
    *s = 0;
    *n = readerCmd == 0x94 ? 6 : 2;
    p[0] = readerCmd;
    p[1] = readerReject ? 1 : 0;
    for (int i = 2; i < *n; i++)
        p[i] = 30;
    return true;
}
static void beat() {
    ++beatCount;
}
static void stop(bool) {
    fence = true;
}
static mb::Backend backend = {read, command, reader, reply, beat, stop};
static mb::Core core(backend);
const cfg::Settings &EspConfigGet() {
    static cfg::Settings s = {};
    s.mqttEnabled = 1;
    s.mqttPort = 1883;
    strcpy(s.root, "test/tunnel");
    strcpy(s.host, "localhost");
    return s;
}
static void cycle() {
    core.tick(fakeTime);
    EspMqttPoll();
    if (fence) {
        fence = false;
        EspMqttFence();
    }
    EspMqttTestDrain();
}
static void inject(const std::string &suffix, const std::string &data, bool retained = false,
                   int offset = 0, int extra = 0) {
    std::string topic = "test/tunnel/" + suffix;
    MqttEvent e;
    e.topic = topic.c_str();
    e.topic_len = int(topic.size());
    e.data = data.c_str();
    e.data_len = int(data.size());
    e.total_data_len = e.data_len + extra;
    e.current_data_offset = offset;
    e.retain = retained;
    fakeHandler(nullptr, nullptr, MQTT_EVENT_DATA, &e);
}
static void reset() {
    fakeHandler(nullptr, nullptr, MQTT_EVENT_DISCONNECTED, nullptr);
    cycle();
    fakeHandler(nullptr, nullptr, MQTT_EVENT_CONNECTED, nullptr);
    cycle();
    published.clear();
}
static void tests() {
    const std::string get = "{\"client\":\"1111111111111111\",\"id\":1}";
    assert(subscriptions.size() == 2 && subscriptions[1] == "test/tunnel/get");
    published.clear();
    for (int i = 0; i < 20; i++) {
        fakeTime += 200;
        cycle();
    }
    assert(published.empty());
    unsigned n = commandCount, b = beatCount;
    inject("get", get);
    cycle();
    assert(published.size() == 1 && published[0].qos == 1 && published[0].retain == 0);
    assert(published[0].data.find("\"request_id\":1") != std::string::npos &&
           published[0].data.find("\"request_client\":\"1111111111111111\"") != std::string::npos);
    assert(commandCount == n && beatCount == b && !core.active());
    published.clear();
    for (int i = 0; i < 10; i++)
        cycle();
    assert(published.empty());
    for (auto bad : {"{}", "[]", "{\"client\":\"1111111111111111\",\"id\":0}",
                     "{\"client\":\"1111111111111111\",\"id\":1,\"id\":2}",
                     "{\"client\":\"1111111111111111\",\"id\":1,\"extra\":1}",
                     "{\"client\":\"1111111111111111\",\"id\":1.5}",
                     "{\"client\":\"1111111111111111\",\"id\":4294967296}",
                     "{\"client\":\"1111111111111111\",\"id\":true}",
                     "{\"client\":\"1111111111111111\",\"id\":-1}", "{\"client\":{}}",
                     "{\"client\":\"1111111111111111\",\"id\":1}x"}) {
        inject("get", bad);
        cycle();
        assert(published.empty());
    }
    inject("get", get, true);
    inject("get", get, false, 1);
    inject("get", get, false, 0, 1);
    inject("get", std::string(129, ' '));
    inject("unknown", get);
    cycle();
    assert(published.empty());
    inject("get", get);
    reset();
    cycle();
    assert(published.empty()); // old connection requests discarded
    // Read overload is isolated from ownership and the command queue.
    for (int i = 0; i < 20; i++)
        inject("get", get);
    for (int i = 0; i < 20; i++)
        cycle();
    assert(published.size() == 4 && !core.active());
    published.clear();
    // Claim using a freshly requested token; reads must not extend the lease.
    inject("get", get);
    cycle();
    auto *state = cJSON_Parse(published.back().data.c_str());
    char cmd[320];
    snprintf(cmd, sizeof(cmd),
             "{\"session\":\"%s\",\"epoch\":%.0f,\"client\":\"1111111111111111\",\"id\":1,\"seen_"
             "ms\":%u,\"action\":\"claim\"}",
             cJSON_GetObjectItem(state, "session")->valuestring,
             cJSON_GetObjectItem(state, "epoch")->valuedouble, fakeTime);
    cJSON_Delete(state);
    inject("cmd", cmd);
    cycle();
    assert(core.active());
    b = beatCount;
    published.clear();
    for (int i = 0; i < 16; i++) {
        fakeTime += 200;
        inject("get", get);
        cycle();
    }
    assert(!core.active() && beatCount == b);
    published.clear();
    fakeOutbox = 4096;
    inject("get", get);
    cycle();
    assert(published.empty());
    fakeOutbox = 0;
    // Getter bounds and truncation are checked by production code.
    tunnelmqtt::ReadRequest q;
    assert(tunnelmqtt::decodeGet(get.c_str(), get.size(), q));
    auto max = get;
    max.replace(max.find("\"id\":1"), 6, "\"id\":4294967295");
    assert(tunnelmqtt::decodeGet(max.c_str(), max.size(), q));
    assert(!tunnelmqtt::decodeGet((get + std::string(1, '\0')).data(), get.size() + 1, q));
    tunnelmqtt::Api api(core);
    api.connect("0123456789abcdef");
    char small[20];
    assert(!tunnelmqtt::encodeState(api, q, 0, small, sizeof(small)));
    puts("PASS MQTT transport: no unsolicited state, strict /get, correlation, retained/fragment "
         "rejection, reconnect, flood isolation, no lease renewal");
}
int main(int argc, char **) {
    EspMqttBegin(core);
    fakeHandler(nullptr, nullptr, MQTT_EVENT_CONNECTED, nullptr);
    cycle();
    if (argc == 1) {
        tests();
        return 0;
    }
    published.clear();
    std::string line;
    while (std::getline(std::cin, line)) {
        char op = line.empty() ? ' ' : line[0];
        if (op == 'T') {
            fakeTime = uint32_t(std::stoul(line.substr(2)));
            cycle();
        } else if (op == 'G') {
            inject("get", line.substr(2));
            cycle();
        } else if (op == 'C') {
            inject("cmd", line.substr(2));
            cycle();
        } else if (op == 'R') {
            inject("get", line.substr(2), true);
            cycle();
        } else if (op == 'X') {
            reset();
        } else if (op == 'E') {
            readerReject = line.substr(2) == "1";
        } else if (op == 'N') {
            std::cout << commandCount << std::endl;
            continue;
        } else
            return 2;
        std::cout << "[";
        bool first = true;
        for (const auto &p : published) {
            if (!first)
                std::cout << ",";
            first = false;
            std::cout << "{\"topic\":\"" << p.topic << "\",\"qos\":" << p.qos
                      << ",\"retain\":" << p.retain << ",\"payload\":" << p.data << "}";
        }
        std::cout << "]" << std::endl;
        published.clear();
    }
}
