#include <cassert>
#include <string>
#include <iostream>
uint32_t testClock = 11000;
int testButton = 1, testRestarts = 0;
static unsigned testStops = 0;
const char *EspDwinUpdateStatus() { return "already_current"; }
const char *EspDwinUpdateStage() { return "current"; }
extern "C" uint8_t EspDwinUpdateHold() { return 0; }
#include "../firmware/TunnelESP32/src/platform/esp32/EspConfig.cpp"
extern "C" void EspRequestSafetyStop(uint8_t) {
    ++testStops;
}
bool EspMqttConnected() {
    return false;
}
extern "C" uint8_t EspModbusRtuReady() {
    return 1;
}
static cfg::Safety safetyState = {false, false, false, false};
static std::string sessionToken;
static void boot() {
    phase = IDLE;
    token[0] = 0;
    buttonAt = 0;
    buttonArmed = true;
    testButton = 1;
    EspConfigBegin();
    ETH.address = IPAddress(active.ip);
    EspConfigWebBegin();
}
static httpd_req_t request(const char *path, const char *payload = nullptr, bool auth = true) {
    httpd_req_t r;
    if (payload) {
        r.body = payload;
        r.content_len = r.body.size();
    }
    if (auth)
        r.auth = "Bearer " + sessionToken;
    if (!strcmp(path, "login"))
        login(&r);
    else if (!strcmp(path, "config")) {
        if (payload)
            save(&r);
        else
            getConfig(&r);
    } else if (!strcmp(path, "status"))
        status(&r);
    return r;
}
static bool signIn(const char *password) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "password", password);
    char *text = cJSON_PrintUnformatted(o);
    auto r = request("login", text, false);
    cJSON_free(text);
    cJSON_Delete(o);
    o = cJSON_Parse(r.response.c_str());
    cJSON *p = cJSON_GetObjectItem(o, "token");
    if (cJSON_IsString(p))
        sessionToken = p->valuestring;
    cJSON_Delete(o);
    return r.status == "200 OK";
}
static void runWriter() {
    try {
        writer(nullptr);
    } catch (const TestYield &) {
    }
}
static void step() {
    EspConfigPoll(safetyState);
    if (phase == FENCING || phase == FAIL_FENCING)
        EspConfigFenced();
    runWriter();
}
static void tests() {
    boot();
    assert(request("config", nullptr, false).status == "401 Unauthorized");
    assert(!signIn("wrong"));
    assert(signIn("admin"));
    auto r = request("config");
    assert(r.response.find("adminHash") == std::string::npos);
    assert(request("config", "{\"revision\":0,\"mqtt_host\":\"test.local\"}").status ==
           "400 Bad Request");
    assert(request("config",
                   "{\"revision\":0,\"admin_password\":\"long-password-123\",\"mqtt_enabled\":true,"
                   "\"mqtt_host\":\"test.local\",\"mqtt_password\":\"secret-123\"}")
               .status == "202 Accepted");
    safetyState.owner = true;
    step();
    assert(phase == DENIED && testNvs.empty() && !EspConfigBusy());
    safetyState.owner = false;
    assert(request("config",
                   "{\"revision\":0,\"admin_password\":\"long-password-123\",\"mqtt_enabled\":true,"
                   "\"mqtt_host\":\"test.local\",\"mqtt_password\":\"secret-123\"}")
               .status == "202 Accepted");
    EspConfigPoll(safetyState);
    assert(phase == FENCING && EspConfigBusy() && testNvs.empty());
    runWriter();
    assert(testNvs.empty());
    EspConfigFenced();
    runWriter();
    assert(phase == SAVED && testRestarts == 0);
    testClock += 1600;
    EspConfigPoll(safetyState);
    assert(testRestarts == 1);
    boot();
    assert(active.mqttEnabled && !strcmp(active.host, "test.local") &&
           !strcmp(active.password, "secret-123") && revision == 1);
    assert(!signIn("admin") && signIn("long-password-123"));
    r = request("config");
    assert(r.response.find("secret-123") == std::string::npos);
    assert(request("config", "{\"revision\":0,\"mqtt_host\":\"new.local\"}").status ==
           "409 Conflict");
    assert(request("config", "{\"revision\":1,\"mqtt_host\":\"new.local\"}").status ==
           "202 Accepted");
    testNvsFailure = true;
    step();
    assert(phase == WRITE_FAILED && EspConfigBusy());
    step();
    assert(phase == FAILED && !EspConfigBusy());
    testNvsFailure = false;
    boot();
    assert(!strcmp(active.host, "test.local"));
    testButton = 0;
    testClock += 100;
    EspConfigPoll(safetyState);
    testClock += 5001;
    step();
    assert(phase == SAVED);
    testClock += 2000;
    int old = testRestarts;
    EspConfigPoll(safetyState);
    assert(testRestarts == old); // wait for BOOT release
    testButton = 1;
    EspConfigPoll(safetyState);
    assert(testRestarts == old + 1);
    boot();
    assert(!active.dhcp && !active.mqttEnabled && !active.password[0] && active.ip[3] == 1 &&
           signIn("admin"));
    testClock = 0x80001000;
    assert(signIn("admin")); // long uptime must not create a false retry delay
    testClock = 0xfffffff0;
    for (unsigned i = 0; i < 5; i++)
        assert(!signIn("wrong"));
    assert(request("login", "{\"password\":\"admin\"}", false).status == "429 Too Many Requests");
    testClock += 10000;
    assert(signIn("admin")); // rate limit expires across millis wrap
    // Local DWIN panel apply: the same fence -> NVS -> restart job as the web
    // handler, no admin password needed, admin account never changed.
    boot();
    assert(signIn("admin"));
    char body[120];
    snprintf(body, sizeof(body), "{\"revision\":%lu,\"admin_password\":\"panel-admin-123\"}",
             (unsigned long)revision);
    assert(request("config", body).status == "202 Accepted");
    step();
    assert(phase == SAVED);
    testClock += 1600;
    EspConfigPoll(safetyState);
    boot();
    assert(active.adminSet && signIn("panel-admin-123") && EspConfigApplyState() == 0);
    uint8_t salt[16], hash[32];
    memcpy(salt, active.adminSalt, sizeof(salt));
    memcpy(hash, active.adminHash, sizeof(hash));
    cfg::Settings local = EspConfigGet();
    local.unit = 0;
    assert(EspConfigSubmitLocal(local) == 1 && phase == IDLE); // validated before queuing
    local.unit = 9;
    local.mqttEnabled = 1;
    strcpy(local.host, "hmi.local");
    strcpy(local.password, "panel-secret");
    local.adminSet = 0; // whatever the caller holds here is ignored
    memset(local.adminSalt, 0xee, sizeof(local.adminSalt));
    memset(local.adminHash, 0xee, sizeof(local.adminHash));
    auto stored = testNvs;
    unsigned stops = testStops;
    safetyState.owner = true;
    assert(EspConfigSubmitLocal(local) == 0 && EspConfigApplyState() == 1);
    assert(EspConfigSubmitLocal(local) == 3); // one job at a time
    step();
    assert(phase == DENIED && EspConfigApplyState() == 2 && testNvs == stored &&
           testStops == stops);
    safetyState.owner = false;
    assert(EspConfigSubmitLocal(local) == 0);
    EspConfigPoll(safetyState);
    assert(phase == FENCING && testStops == stops + 1 && testNvs == stored); // STOP before NVS
    EspConfigFenced();
    runWriter();
    assert(phase == SAVED && EspConfigApplyState() == 4 && testNvs != stored);
    old = testRestarts;
    testClock += 1600;
    EspConfigPoll(safetyState);
    assert(testRestarts == old + 1);
    boot();
    assert(active.unit == 9 && active.mqttEnabled && !strcmp(active.host, "hmi.local") &&
           !strcmp(active.password, "panel-secret"));
    assert(active.adminSet && !memcmp(active.adminSalt, salt, sizeof(salt)) &&
           !memcmp(active.adminHash, hash, sizeof(hash)));
    assert(!signIn("admin") && signIn("panel-admin-123"));
    local = EspConfigGet();
    local.unit = 10;
    testNvsFailure = true;
    assert(EspConfigSubmitLocal(local) == 0);
    step();
    step();
    assert(phase == FAILED && EspConfigApplyState() == 3);
    testNvsFailure = false;
    boot();
    assert(active.unit == 9);
    puts("PASS config service: real HTTP handlers/auth, first-use password, stale revision, owner "
         "rejection, STOP fence before NVS, write failure, persisted reboot, physical recovery "
         "waits for BOOT release, long-uptime login and rollover rate limit, local DWIN apply "
         "without touching the admin account");
}
int main(int argc, char **) {
    if (argc == 1) {
        tests();
        return 0;
    }
    boot();
    std::string line;
    while (std::getline(std::cin, line)) {
        cJSON *o = cJSON_Parse(line.c_str());
        if (!o)
            return 2;
        auto get = [&](const char *k) { return cJSON_GetObjectItem(o, k); };
        cJSON *advance = get("advance");
        testClock += cJSON_IsNumber(advance) ? uint32_t(advance->valuedouble) : 25;
        cJSON *busy = get("busy");
        if (cJSON_IsBool(busy))
            safetyState.owner = cJSON_IsTrue(busy);
        if (cJSON_IsTrue(get("cycle"))) {
            testClock += 2000;
            int before = testRestarts;
            step();
            if (testRestarts != before)
                boot();
        }
        httpd_req_t r;
        cJSON *p = get("body");
        if (cJSON_IsString(p)) {
            r.body = p->valuestring;
            r.content_len = r.body.size();
        }
        p = get("authorization");
        if (cJSON_IsString(p))
            r.auth = p->valuestring;
        p = get("path");
        const char *path = cJSON_IsString(p) ? p->valuestring : "";
        if (!strcmp(path, "/"))
            index(&r);
        else if (!strcmp(path, "/api/login"))
            login(&r);
        else if (!strcmp(path, "/api/logout"))
            logout(&r);
        else if (!strcmp(path, "/api/status"))
            status(&r);
        else if (!strcmp(path, "/api/config")) {
            if (get("body"))
                save(&r);
            else
                getConfig(&r);
        } else {
            r.status = "404 Not Found";
            r.response = "{}";
        }
        // Advance the same production maintenance logic after handling the request.
        step();
        cJSON_Delete(o);
        o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "status", atoi(r.status.c_str()));
        cJSON_AddStringToObject(o, "body", r.response.c_str());
        char *out = cJSON_PrintUnformatted(o);
        std::cout << out << std::endl;
        cJSON_free(out);
        cJSON_Delete(o);
    }
}
