#include "../../common/Version.h"
#include <Arduino.h>
#include <Preferences.h>
#include <ETH.h>
#include <esp_http_server.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>
#include <atomic>
#include <cJSON.h>
#include "EspConfig.h"
#include "EspPlatform.h"
#include "EspMqtt.h"
#include "EspModbus.h"
#include "EspDwinUpdate.h"
#include "../../web/ConfigPage.h"

static Preferences prefs;
static bool storageReady = false;
static bool readRecord(unsigned slot, cfg::Record &r) {
    const char *key = slot ? "slot1" : "slot0";
    return storageReady && prefs.getBytesLength(key) == sizeof(r) &&
           prefs.getBytes(key, &r, sizeof(r)) == sizeof(r);
}
static bool writeRecord(unsigned slot, const cfg::Record &r) {
    return storageReady && prefs.putBytes(slot ? "slot1" : "slot0", &r, sizeof(r)) == sizeof(r);
}
static cfg::Store store({readRecord, writeRecord});
static cfg::Settings active, pending;
static uint32_t revision = 0;
enum Phase {
    IDLE,
    RESERVED,
    REQUESTED,
    FENCING,
    PERMITTED,
    WRITING,
    SAVED,
    DENIED,
    WRITE_FAILED,
    FAIL_FENCING,
    FAILED
};
static std::atomic<int> phase(IDLE);
static std::atomic<uint32_t> job(0), savedAt(0);
static std::atomic<bool> safe(false), fallback(false);
static bool physicalJob = false;
static uint32_t buttonAt = 0;
static bool buttonArmed = true;
static char token[65] = {}; // accessed only by the single HTTP server task
static uint32_t tokenAt = 0, loginFailedAt = 0;
static bool loginWaiting = false;
static unsigned loginFailures = 0;

static void randomHex(char *out, unsigned bytes) {
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < bytes; i++) {
        uint8_t b = uint8_t(esp_random());
        out[i * 2] = hex[b >> 4];
        out[i * 2 + 1] = hex[b & 15];
    }
    out[bytes * 2] = 0;
}
static void hashPassword(const char *password, const uint8_t *salt, uint8_t *out) {
    mbedtls_sha256_context c;
    mbedtls_sha256_init(&c);
    mbedtls_sha256_starts(&c, 0);
    mbedtls_sha256_update(&c, salt, 16);
    mbedtls_sha256_update(&c, (const unsigned char *)password, strlen(password));
    mbedtls_sha256_finish(&c, out);
    mbedtls_sha256_free(&c);
}
static void setPassword(cfg::Settings &s, const char *password, bool configured) {
    esp_fill_random(s.adminSalt, sizeof(s.adminSalt));
    hashPassword(password, s.adminSalt, s.adminHash);
    s.adminSet = configured;
}
static bool same(const void *a, const void *b, size_t length) {
    const auto *x = (const uint8_t *)a, *y = (const uint8_t *)b;
    uint8_t different = 0;
    for (size_t i = 0; i < length; i++)
        different |= x[i] ^ y[i];
    return different == 0;
}
bool EspConfigStorageReady() { return storageReady; }
const cfg::Settings &EspConfigGet() {
    return active;
}
void EspConfigSetFallback(bool v) {
    fallback = v;
}
static bool submit(const cfg::Settings &s, bool physical) {
    int current = phase.load();
    if (current != IDLE && current != DENIED && current != FAILED)
        return false;
    if (!phase.compare_exchange_strong(current, RESERVED))
        return false;
    pending = s;
    physicalJob = physical;
    ++job;
    phase = REQUESTED;
    return true;
}
uint8_t EspConfigSubmitLocal(const cfg::Settings &next) {
    if (!storageReady)
        return 2;
    cfg::Settings s = next;
    s.adminSet = active.adminSet;
    memcpy(s.adminSalt, active.adminSalt, sizeof(s.adminSalt));
    memcpy(s.adminHash, active.adminHash, sizeof(s.adminHash));
    uint8_t result = cfg::validate(s) ? 1 : submit(s, false) ? 0 : 3;
    memset(&s, 0, sizeof(s));
    return result;
}
uint8_t EspConfigApplyState() {
    switch (phase.load()) {
    case IDLE:
        return 0;
    case DENIED:
        return 2;
    case WRITE_FAILED:
    case FAIL_FENCING:
    case FAILED:
        return 3;
    case SAVED:
        return 4;
    default:
        return 1;
    }
}
extern "C" uint8_t EspConfigBusy(void) {
    int p = phase.load();
    return p == FENCING || p == PERMITTED || p == WRITING || p == SAVED || p == WRITE_FAILED ||
           p == FAIL_FENCING;
}
extern "C" void EspConfigFenced(void) {
    if (phase == FENCING)
        phase = PERMITTED;
    else if (phase == FAIL_FENCING)
        phase = FAILED;
}
void EspConfigPoll(const cfg::Safety &safety) {
    bool idle = cfg::canApply(safety);
    safe = idle;
    uint32_t now = millis();
    // BOOT/GPIO0 is sampled only after startup. Hold >=5 s while idle, then
    // release. Never restart with GPIO0 held low (that enters ROM download).
    if (now > 10000) {
        if (digitalRead(0) == LOW) {
            if (!buttonAt)
                buttonAt = now;
            if (buttonArmed && uint32_t(now - buttonAt) >= 5000) {
                buttonArmed = false;
                if (idle && storageReady)
                    submit(cfg::defaults(true), true);
            }
        } else {
            buttonAt = 0;
            buttonArmed = true;
        }
    }
    if (phase == REQUESTED) {
        if (!idle)
            phase = DENIED;
        else {
            phase = FENCING;
            EspRequestSafetyStop(STOP_PC_DISCONNECT);
        }
    } else if (phase == WRITE_FAILED) {
        phase = FAIL_FENCING;
        EspRequestSafetyStop(STOP_PC_DISCONNECT);
    } else if (phase == SAVED && uint32_t(now - savedAt.load()) >= 1500 && digitalRead(0) != LOW) {
        // Control remains fenced until reset. No resume command is generated.
        ESP.restart();
    }
}
static void writer(void *) {
    for (;;) {
        if (phase == PERMITTED) {
            phase = WRITING;
            if (physicalJob)
                setPassword(pending, "admin", false);
            bool ok = store.save(pending);
            memset(pending.password, 0, sizeof(pending.password));
            if (ok) {
                savedAt = millis();
                phase = SAVED;
            } else
                phase = WRITE_FAILED;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
void EspConfigBegin() {
    pinMode(0, INPUT_PULLUP);
    storageReady = prefs.begin("tunnel-comm", false);
    store.load();
    active = store.get();
    revision = store.revision();
    if (!active.adminSet)
        setPassword(active, "admin", false);
    if (xTaskCreate(writer, "config-store", 8192, nullptr, 1, nullptr) != pdPASS)
        storageReady = false;
}
static esp_err_t respond(httpd_req_t *r, const char *status, const char *body) {
    httpd_resp_set_status(r, status);
    httpd_resp_set_type(r, "application/json; charset=utf-8");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
    return httpd_resp_send(r, body, HTTPD_RESP_USE_STRLEN);
}
static bool authorized(httpd_req_t *r) {
    char header[80];
    if (!token[0] || uint32_t(millis() - tokenAt) >= 1800000 ||
        httpd_req_get_hdr_value_str(r, "Authorization", header, sizeof(header)) != ESP_OK ||
        strlen(header) != 71 || strncmp(header, "Bearer ", 7) || !same(header + 7, token, 64)) {
        respond(r, "401 Unauthorized", "{\"error\":\"unauthorized\"}");
        return false;
    }
    tokenAt = millis();
    return true;
}
static bool body(httpd_req_t *r, char *out, size_t capacity) {
    if (!r->content_len || r->content_len >= capacity) {
        respond(r, "413 Payload Too Large", "{\"error\":\"body_size\"}");
        return false;
    }
    size_t used = 0;
    while (used < r->content_len) {
        int n = httpd_req_recv(r, out + used, r->content_len - used);
        if (n <= 0) {
            respond(r, "400 Bad Request", "{\"error\":\"incomplete_body\"}");
            return false;
        }
        used += size_t(n);
    }
    out[used] = 0;
    return true;
}
static esp_err_t login(httpd_req_t *r) {
    if (loginWaiting && uint32_t(millis() - loginFailedAt) < 10000)
        return respond(r, "429 Too Many Requests", "{\"error\":\"login_wait\"}");
    loginWaiting = false;
    char data[256];
    if (!body(r, data, sizeof(data)))
        return ESP_OK;
    // Bound nesting without treating braces inside the password as structure.
    bool quote = false, escape = false;
    unsigned depth = 0;
    for (size_t i = 0; i < r->content_len; i++) {
        char ch = data[i];
        if (quote && escape) {
            escape = false;
            continue;
        }
        if (quote && ch == '\\') {
            if (i + 5 < r->content_len && data[i + 1] == 'u' && !strncmp(data + i + 2, "0000", 4))
                return respond(r, "400 Bad Request", "{}");
            escape = true;
            continue;
        }
        if (ch == '"')
            quote = !quote;
        if (!quote) {
            if (ch == '{' || ch == '[') {
                if (++depth > 1)
                    return respond(r, "400 Bad Request", "{}");
            }
            if (ch == '}' || ch == ']') {
                if (!depth)
                    return respond(r, "400 Bad Request", "{}");
                --depth;
            }
        }
    }
    if (quote || depth || memchr(data, 0, r->content_len))
        return respond(r, "400 Bad Request", "{}");
    cJSON *o = cJSON_ParseWithLengthOpts(data, r->content_len + 1, nullptr, true);
    cJSON *p = o ? cJSON_GetObjectItemCaseSensitive(o, "password") : nullptr;
    bool shape = cJSON_IsObject(o) && o->child && !o->child->next && cJSON_IsString(p) &&
                 strlen(p->valuestring) <= 64;
    uint8_t digest[32] = {};
    if (shape)
        hashPassword(p->valuestring, active.adminSalt, digest);
    bool ok = shape && same(digest, active.adminHash, 32);
    cJSON_Delete(o);
    memset(data, 0, sizeof(data));
    if (!ok) {
        if (++loginFailures >= 5) {
            loginFailedAt = millis();
            loginWaiting = true;
            loginFailures = 0;
        }
        return respond(r, "401 Unauthorized", "{\"error\":\"wrong_password\"}");
    }
    loginFailures = 0;
    randomHex(token, 32);
    tokenAt = millis();
    char answer[100];
    snprintf(answer, sizeof(answer), "{\"token\":\"%s\"}", token);
    return respond(r, "200 OK", answer);
}
static esp_err_t getConfig(httpd_req_t *r) {
    if (!authorized(r))
        return ESP_OK;
    char result[1024];
    if (!cfg::json(active, revision, result, sizeof(result)))
        return respond(r, "500 Internal Server Error", "{}");
    return respond(r, "200 OK", result);
}
static esp_err_t logout(httpd_req_t *r) {
    if (!authorized(r))
        return ESP_OK;
    token[0] = 0;
    return respond(r, "200 OK", "{\"ok\":true}");
}
static const char *phaseName(int p) {
    switch (p) {
    case IDLE:
        return "idle";
    case RESERVED:
    case REQUESTED:
        return "requested";
    case FENCING:
    case PERMITTED:
    case WRITING:
        return "saving";
    case SAVED:
        return "restarting";
    case DENIED:
        return "busy";
    case WRITE_FAILED:
    case FAIL_FENCING:
    case FAILED:
        return "save_failed";
    default:
        return "unknown";
    }
}
static esp_err_t status(httpd_req_t *r) {
    if (!authorized(r))
        return ESP_OK;
    char answer[640];
    IPAddress ip = ETH.localIP();
    snprintf(answer, sizeof(answer),
             "{\"firmware\":\"" TUNNEL_VERSION
             "\",\"ip\":\"%u.%u.%u.%u\",\"link\":%s,\"fallback\":%s,\"storage_ready\":%s,\"can_"
             "apply\":%s,\"phase\":\"%s\",\"job\":%lu,\"mqtt_enabled\":%s,\"mqtt_connected\":%s,"
             "\"rtu_ready\":%s,\"dwin_update\":\"%s\",\"dwin_stage\":\"%s\",\"dwin_hold\":%s}",
             ip[0], ip[1], ip[2], ip[3], ETH.linkUp() ? "true" : "false",
             fallback ? "true" : "false", storageReady ? "true" : "false", safe ? "true" : "false",
             phaseName(phase.load()), (unsigned long)job.load(),
             active.mqttEnabled ? "true" : "false", EspMqttConnected() ? "true" : "false",
             EspModbusRtuReady() ? "true" : "false", EspDwinUpdateStatus(), EspDwinUpdateStage(),
             EspDwinUpdateHold() ? "true" : "false");
    return respond(r, "200 OK", answer);
}
static esp_err_t save(httpd_req_t *r) {
    if (!authorized(r))
        return ESP_OK;
    if (!storageReady)
        return respond(r, "503 Service Unavailable", "{\"error\":\"storage_unavailable\"}");
    char data[2049];
    if (!body(r, data, sizeof(data)))
        return ESP_OK;
    cfg::Update update = {};
    const char *error = cfg::parse(data, r->content_len, active, update);
    memset(data, 0, sizeof(data));
    if (error) {
        char answer[100];
        snprintf(answer, sizeof(answer), "{\"error\":\"%s\"}", error);
        return respond(r, "400 Bad Request", answer);
    }
    if (update.revision != revision)
        return respond(r, "409 Conflict", "{\"error\":\"revision_changed\"}");
    if (update.adminPassword[0])
        setPassword(update.settings, update.adminPassword, true);
    bool accepted = submit(update.settings, false);
    memset(&update, 0, sizeof(update));
    if (!accepted)
        return respond(r, "409 Conflict", "{\"error\":\"apply_in_progress\"}");
    char answer[80];
    snprintf(answer, sizeof(answer), "{\"accepted\":true,\"job\":%lu}", (unsigned long)job.load());
    return respond(r, "202 Accepted", answer);
}
static esp_err_t index(httpd_req_t *r) {
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_set_hdr(r, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(r, "Content-Security-Policy",
                       "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
                       "connect-src 'self'; frame-ancestors 'none'");
    return httpd_resp_send(r, CONFIG_PAGE, HTTPD_RESP_USE_STRLEN);
}
void EspConfigWebBegin() {
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.server_port = 80;
    c.stack_size = 12288;
    c.max_open_sockets = 3;
    c.lru_purge_enable = true;
    c.max_req_hdr_len = 2048;
    c.max_uri_len = 128;
    c.recv_wait_timeout = 2;
    c.send_wait_timeout = 2;
    httpd_handle_t server = nullptr;
    if (httpd_start(&server, &c) != ESP_OK)
        return;
    const httpd_uri_t routes[] = {{"/", HTTP_GET, index, nullptr},
                                  {"/api/login", HTTP_POST, login, nullptr},
                                  {"/api/config", HTTP_GET, getConfig, nullptr},
                                  {"/api/config", HTTP_POST, save, nullptr},
                                  {"/api/status", HTTP_GET, status, nullptr},
                                  {"/api/logout", HTTP_POST, logout, nullptr}};
    for (const auto &route : routes)
        httpd_register_uri_handler(server, &route);
}
