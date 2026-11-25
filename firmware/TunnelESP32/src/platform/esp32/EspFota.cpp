#include "EspFota.h"
#include "EspMqtt.h"
#include "EspPlatform.h"
#include "../../update/FotaProtocol.h"
#include "../../update/FotaTrust.h"
#include "../../update/FotaImage.h"
#include "../../update/FotaHealth.h"
#include "../../common/Version.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <freertos/queue.h>
#include <atomic>
#include <algorithm>
#include <sys/time.h>
extern "C" {
#include "../../control/motor_pwm.h"
}

// Crypto, HTTP, flash and NVS run exclusively on this low-priority worker.
// Only the control loop grants a job, after checking machine/config ownership.
namespace {
enum Phase { IDLE, FENCING, READY, RUNNING, REBOOTING, VERIFY_BOOT, END_FENCING, INIT_FAILED };
std::atomic<int> phase(IDLE);
std::atomic<bool> cancelled(false), query(false);
std::atomic<uint32_t> alive(0), verifiedAt(0);
uint32_t bootCheckStarted = 0;
bool pendingBoot = false;
char signingKeyId[65] = {};
struct Wire {
    uint16_t length;
    bool retained;
    char text[fota::MAX_JSON + 1];
};
struct Checked {
    fota::Request q;
    bool signature;
};
struct Tx {
    bool ack;
    char data[1200];
};
QueueHandle_t inbound = nullptr, checked = nullptr, tasks = nullptr, tx = nullptr;
Preferences prefs;
std::atomic<bool> storage(false);
fota::Replay replay;
struct Record {
    uint32_t magic = 0x46544f31, version = 0, previous = 0, slot = 0;
    char job[33] = {}, state[24] = {}, error[32] = {};
    uint32_t crc = 0;
} record;
uint32_t bytes = 0, total = 0, lastProgress = 0;
// Volatile diagnostics for the latest transfer; do not change the NVS journal layout.
int httpError = 0, tlsError = 0, tlsCode = 0, tlsFlags = 0, socketError = 0;
uint32_t heapBefore = 0, heapLargest = 0;
const esp_partition_t *destination = nullptr;
bool provisioned() {
    return TUNNEL_FOTA_PUBLIC_KEY[0] && TUNNEL_FOTA_URL_PREFIX[0];
}
uint32_t checksum(const Record &r) {
    uint32_t c = 2166136261UL;
    auto p = reinterpret_cast<const uint8_t *>(&r);
    for (size_t i = 0; i < offsetof(Record, crc); i++)
        c = (c ^ p[i]) * 16777619UL;
    return c;
}
bool saveRecord(const char *state, const char *error = "") {
    strlcpy(record.state, state, sizeof(record.state));
    strlcpy(record.error, error, sizeof(record.error));
    record.crc = checksum(record);
    return storage && prefs.putBytes("job", &record, sizeof(record)) == sizeof(record);
}
void emit(bool ack, const char *data) {
    if (!tx)
        return;
    Tx t = {};
    t.ack = ack;
    strlcpy(t.data, data, sizeof(t.data));
    xQueueSend(tx, &t, 0);
}
void status() {
    char b[1200];
    snprintf(b, sizeof(b),
             "{\"api_version\":1,\"job\":\"%s\",\"state\":\"%s\",\"error\":\"%s\",\"bytes\":%lu,"
             "\"total\":%lu,\"percent\":%lu,\"running_version\":%u,\"target_version\":%lu,"
             "\"configured\":%s,\"storage_ready\":%s,\"ota_revision\":2,"
             "\"url_prefix\":\"%s\",\"signing_key_id\":\"%s\",\"image_schema\":%lu,"
             "\"http_error\":%d,\"tls_error\":%d,\"tls_code\":%d,\"tls_flags\":%d,"
             "\"socket_errno\":%d,\"heap_before\":%lu,\"heap_largest\":%lu}",
             record.job, record.state[0] ? record.state : "idle", record.error,
             (unsigned long)bytes, (unsigned long)total,
             (unsigned long)(total ? uint64_t(bytes) * 100 / total : 0), TUNNEL_FIRMWARE_REGISTER,
             (unsigned long)record.version, provisioned() ? "true" : "false",
             storage ? "true" : "false", TUNNEL_FOTA_URL_PREFIX, signingKeyId,
             (unsigned long)fota::imageIdentity.schema,
             httpError, tlsError, tlsCode, tlsFlags, socketError,
             (unsigned long)heapBefore, (unsigned long)heapLargest);
    emit(false, b);
}
void ack(const fota::Request &q, const char *state, const char *error = "") {
    char b[320];
    snprintf(b, sizeof(b),
             "{\"api_version\":1,\"job\":\"%s\",\"client\":\"%s\",\"id\":%lu,\"state\":\"%s\","
             "\"error\":\"%s\"}",
             q.job, q.client, (unsigned long)q.id, state, error);
    emit(true, b);
}
bool signatureValid(const fota::Request &q) {
    if (!provisioned())
        return false;
    char text[768];
    if (!fota::canonical(q, text, sizeof(text)))
        return false;
    uint8_t hash[32], signature[72];
    size_t n = strlen(q.signature) / 2;
    for (size_t i = 0; i < n; i++) {
        unsigned x;
        if (sscanf(q.signature + i * 2, "%2x", &x) != 1)
            return false;
        signature[i] = uint8_t(x);
    }
    mbedtls_sha256((const unsigned char *)text, strlen(text), hash, 0);
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    bool ok = mbedtls_pk_parse_public_key(&pk, (const unsigned char *)TUNNEL_FOTA_PUBLIC_KEY,
                                          strlen(TUNNEL_FOTA_PUBLIC_KEY) + 1) == 0 &&
              mbedtls_pk_can_do(&pk, MBEDTLS_PK_ECDSA) &&
              mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), signature, n) == 0;
    mbedtls_pk_free(&pk);
    return ok;
}
void fail(const char *error) {
    if (!saveRecord("failed", error)) {
        strlcpy(record.state, "failed", sizeof(record.state));
        strlcpy(record.error, "storage", sizeof(record.error));
    }
    status();
    phase = END_FENCING;
    EspRequestUpdateFence(); // Drop commands received during the job before unlocking.
}
void progress(const char *state) {
    strlcpy(record.state, state, sizeof(record.state));
    if (uint32_t(millis() - lastProgress) >= 1000) {
        status();
        lastProgress = millis();
    }
    if (query.exchange(false))
        status();
}
void download(const fota::Request &q) {
    phase = RUNNING;
    httpError = tlsError = tlsCode = tlsFlags = socketError = 0;
    heapBefore = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    heapLargest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    bytes = 0;
    total = q.size;
    record = Record{};
    strcpy(record.job, q.job);
    record.version = q.version;
    record.previous = TUNNEL_FIRMWARE_REGISTER;
    record.slot = destination->address;
    if (!saveRecord("downloading")) {
        fail("storage");
        return;
    }
    status();
    if (time(nullptr) < 1735689600) {
        timeval tv = {};
        tv.tv_sec = q.unix_time;
        settimeofday(&tv, nullptr);
    } // Signed, fresh-session bootstrap time.
    esp_http_client_config_t c = {};
    c.url = q.url;
    c.timeout_ms = 3000;
    c.disable_auto_redirect = true;
    c.buffer_size = 4096;
    c.keep_alive_enable = false;
    if (TUNNEL_FOTA_CA_PEM[0])
        c.cert_pem = TUNNEL_FOTA_CA_PEM;
    else
        c.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t http = esp_http_client_init(&c);
    esp_ota_handle_t ota = 0;
    bool begun = false;
    const char *error = nullptr;
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    if (!http)
        error = "http_init";
    else if ((httpError = esp_http_client_open(http, 0)) != ESP_OK) {
        socketError = esp_http_client_get_errno(http);
        tlsError = esp_http_client_get_and_clear_last_tls_error(http, &tlsCode, &tlsFlags);
        error = "https_connect";
    }
    else if (esp_http_client_fetch_headers(http) != (int64_t)q.size ||
             esp_http_client_is_chunked_response(http))
        error = "length";
    else if (esp_http_client_get_status_code(http) != 200)
        error = "http_status";
    else if (cancelled)
        error = "cancelled";
    else if (esp_ota_begin(destination, q.size, &ota) != ESP_OK)
        error = "flash_begin";
    else
        begun = true;
    uint8_t buffer[4096];
    fota::ImageScanner imageScanner;
    uint32_t start = millis();
    while (!error && bytes < q.size) {
        Wire pending;
        if (xQueueReceive(inbound, &pending, 0) == pdTRUE) {
            fota::Request other;
            if (fota::decode(pending.text, pending.length, other)) {
                char a[768], b[768];
                fota::canonical(q, a, sizeof(a));
                fota::canonical(other, b, sizeof(b));
                bool same = !strcmp(a, b) && !strcmp(q.signature, other.signature);
                ack(other,
                    pending.retained ? "rejected"
                    : same           ? "duplicate"
                                     : "rejected",
                    pending.retained ? "retained"
                    : same           ? ""
                                     : "busy");
            }
        }
        if (cancelled || EspEmergencyHeld() || MotorPwm_IsReleased() ||
            uint32_t(millis()-alive.load())>1000U) {
            error = "cancelled";
            break;
        }
        if (uint32_t(millis() - start) > 120000) {
            error = "timeout";
            break;
        }
        int n = esp_http_client_read(http, (char *)buffer,
                                     std::min(size_t(q.size - bytes), sizeof(buffer)));
        if (n <= 0) {
            error = "truncated_or_timeout";
            break;
        }
        // Image validator in esp_ota_write rejects wrong magic/chip/revision.
        if (esp_ota_write(ota, buffer, n) != ESP_OK) {
            error = "flash_write_or_image";
            break;
        }
        mbedtls_sha256_update(&sha, buffer, n);
        imageScanner.feed(buffer, n);
        bytes += n;
        progress("downloading");
        vTaskDelay(1);
    }
    if (http) {
        esp_http_client_close(http);
        esp_http_client_cleanup(http);
    }
    uint8_t hash[32];
    mbedtls_sha256_finish(&sha, hash);
    mbedtls_sha256_free(&sha);
    char digest[65];
    for (unsigned i = 0; i < 32; i++)
        snprintf(digest + i * 2, 3, "%02x", hash[i]);
    if (!error && strcmp(digest, q.sha256))
        error = "sha256";
    if (!error && !imageScanner.matches(q.version))
        error = "image_target_or_version";
    if (!error && cancelled)
        error = "cancelled";
    if (error) {
        if (begun)
            esp_ota_abort(ota);
        fail(error);
        return;
    }
    strlcpy(record.state, "verifying", sizeof(record.state));
    status();
    if (esp_ota_end(ota) != ESP_OK) {
        fail("image_validation");
        return;
    }
    if (cancelled) {
        fail("cancelled");
        return;
    }
    if (!saveRecord("rebooting")) {
        fail("storage");
        return;
    }
    if (cancelled) {
        fail("cancelled");
        return;
    }
    if (esp_ota_set_boot_partition(destination) != ESP_OK) {
        fail("boot_selection");
        return;
    }
    phase = REBOOTING;
    status();
    for (unsigned i = 0; i < 150; i++) {
        if (cancelled)
            break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (cancelled) {
        if (esp_ota_set_boot_partition(esp_ota_get_running_partition()) != ESP_OK) {
            strlcpy(record.error, "cancel_boot_restore", sizeof(record.error));
            status();
            return;
        } // Stay locked if restoration fails.
        fail("cancelled");
        return;
    }
    ESP.restart();
}
void recover() {
    storage = prefs.begin("tunnel-fota", false);
    size_t n = storage ? prefs.getBytesLength("job") : 0;
    if (n) {
        Record r;
        if (n != sizeof(r) || prefs.getBytes("job", &r, sizeof(r)) != sizeof(r) ||
            r.magic != record.magic || r.crc != checksum(r)) {
            storage = false;
            strlcpy(record.error, "journal_corrupt", sizeof(record.error));
        } else
            record = r;
    }
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    bool pending = esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
                   state == ESP_OTA_IMG_PENDING_VERIFY;
    if (pending) {
        bootCheckStarted = millis();
        phase = VERIFY_BOOT;
        strlcpy(record.state, "boot_verifying", sizeof(record.state));
    } else if (state == ESP_OTA_IMG_VALID && record.version == TUNNEL_FIRMWARE_REGISTER &&
               record.slot == esp_ota_get_running_partition()->address &&
               (!strcmp(record.state,"boot_verifying") || !strcmp(record.state,"rebooting"))) {
        // Reset after marking the slot valid but before persisting the result.
        if (!saveRecord("success")) {
            strlcpy(record.state,"failed",sizeof(record.state));
            strlcpy(record.error,"storage_after_verify",sizeof(record.error));
        }
    } else if (!strcmp(record.state, "downloading") || !strcmp(record.state, "rebooting") ||
               !strcmp(record.state,"boot_verifying")) {
        esp_partition_iterator_t it =
            esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr);
        bool rolled = false;
        while (it) {
            const auto *part = esp_partition_get(it);
            if (part->address == record.slot) {
                esp_ota_img_states_t v;
                if (esp_ota_get_state_partition(part, &v) == ESP_OK)
                    rolled = v == ESP_OTA_IMG_ABORTED || v == ESP_OTA_IMG_INVALID;
            }
            it = esp_partition_next(it);
        }
        saveRecord(rolled ? "rolled_back" : "interrupted",
                   rolled ? "boot_health" : "restart_before_complete");
    }
}
void worker(void *) {
    recover();
    for (;;) {
        if (phase == VERIFY_BOOT) {
            const auto decision=fota::bootDecision(millis(),bootCheckStarted,alive.load(),
                verifiedAt.load(),storage,record.version==TUNNEL_FIRMWARE_REGISTER);
            if (decision==fota::CONFIRM_BOOT) {
                if (saveRecord("boot_verifying") && esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
                    if (!saveRecord("success")) {
                        strlcpy(record.state,"failed",sizeof(record.state));
                        strlcpy(record.error,"storage_after_verify",sizeof(record.error));
                    }
                    phase = END_FENCING;
                    EspRequestUpdateFence();
                    status();
                }
            } else if (decision==fota::ROLLBACK_BOOT) {
                saveRecord("rolled_back", "boot_health");
                status();
                esp_ota_mark_app_invalid_rollback_and_reboot();
            }
        }
        fota::Request task;
        if (tasks && xQueueReceive(tasks, &task, 0) == pdTRUE)
            download(task);
        Wire wire;
        if (inbound && xQueueReceive(inbound, &wire, 0) == pdTRUE) {
            Checked result = {};
            if (fota::decode(wire.text, wire.length, result.q)) {
                if (wire.retained)
                    ack(result.q, "rejected", "retained");
                else {
                    result.signature = signatureValid(result.q);
                    if (xQueueSend(checked, &result, 0) != pdTRUE)
                        ack(result.q, "rejected", "queue_full");
                }
            }
        }
        if (query.exchange(false))
            status();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
fota::Request granted;

char currentSession[17] = {};
uint32_t currentEpoch = 0;
} // namespace
// Prevent Arduino's default verifyOta()==true from confirming before app health.
extern "C" bool verifyRollbackLater() {
    return true;
}
void EspFotaBegin() {
    uint8_t digest[32];
    mbedtls_sha256((const unsigned char *)TUNNEL_FOTA_PUBLIC_KEY,strlen(TUNNEL_FOTA_PUBLIC_KEY),digest,0);
    for (unsigned i=0;i<32;i++) snprintf(signingKeyId+i*2,3,"%02x",digest[i]);
    inbound = xQueueCreate(2, sizeof(Wire));
    checked = xQueueCreate(2, sizeof(Checked));
    tasks = xQueueCreate(1, sizeof(fota::Request));
    tx = xQueueCreate(6, sizeof(Tx));
    esp_ota_img_states_t state;
    pendingBoot=esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY;
    if (pendingBoot)
        phase = VERIFY_BOOT;
    if (!inbound || !checked || !tasks || !tx ||
        xTaskCreate(worker, "fota", 16384, nullptr, 1, nullptr) != pdPASS) {
        phase = INIT_FAILED;
        strlcpy(record.state,"failed",sizeof(record.state));
        strlcpy(record.error,"initialization",sizeof(record.error));
        EspRequestSafetyStop(STOP_INITIALIZATION);
    }
}
void EspFotaReceive(const char *data, size_t n, bool retained) {
    if (!inbound || !n || n > fota::MAX_JSON)
        return;
    Wire w = {};
    w.length = n;
    w.retained = retained;
    memcpy(w.text, data, n);
    xQueueSend(inbound, &w, 0);
}
void EspFotaGet() {
    query = true;
}
void EspFotaHealth(bool healthy) {
    if (healthy) {
        const uint32_t now=millis();
        if (!verifiedAt || uint32_t(now-alive.load())>=500U)
            verifiedAt = now;
        alive = now;
    } else
        verifiedAt = 0;
}
extern "C" uint8_t EspFotaBusy(void) {
    return phase != IDLE;
}
extern "C" void EspFotaCancel(void) {
    if (phase!=IDLE) cancelled=true;
}
extern "C" void EspFotaFence(void) {
    if (phase == END_FENCING) {
        phase = IDLE;
        return;
    }
    if (phase == FENCING) {
        if (!fota::fenceMayStart(cancelled,EspEmergencyHeld(),MotorPwm_IsReleased()))
            cancelled = true;
        phase = READY;
        return;
    }
    if (phase != IDLE)
        cancelled = true;
}
void EspFotaPoll(bool safe, const char *session, uint32_t epoch) {
    if (phase==INIT_FAILED) {
        // No worker exists to run rollback health checks. A reset of a pending
        // app lets the bootloader roll back; an ordinary boot remains stopped.
        if (pendingBoot) ESP.restart();
        if (query.exchange(false)) status();
        return;
    }
    if (strcmp(currentSession, session) || currentEpoch != epoch) {
        strlcpy(currentSession, session, sizeof(currentSession));
        currentEpoch = epoch;
        replay.reset();
    }
    if (phase == READY) {
        if (cancelled) {
            phase = END_FENCING;
            EspRequestUpdateFence();
            ack(granted, "rejected", "cancelled");
            return;
        }
        phase = RUNNING;
        if (xQueueSend(tasks, &granted, 0) != pdTRUE) {
            phase = END_FENCING;
            EspRequestUpdateFence();
            ack(granted, "rejected", "worker_busy");
        } else
            ack(granted, "accepted");
    }
    Checked c;
    if (!checked || xQueueReceive(checked, &c, 0) != pdTRUE)
        return;
    auto &q = c.q;
    if (!c.signature) {
        ack(q, "rejected", provisioned() ? "signature" : "not_provisioned");
        return;
    }
    if (strcmp(q.session, session) || q.epoch != epoch) {
        ack(q, "rejected", "session");
        return;
    }
    int repeat = replay.check(q);
    if (repeat == 1) {
        ack(q, "duplicate");
        query = true;
        return;
    }
    if (repeat < 0) {
        ack(q, "rejected", "replay_or_clients_full");
        return;
    }
    const auto *slot = esp_ota_get_next_update_partition(nullptr);
    const char *error = fota::validate(q, session, epoch, millis(), TUNNEL_FIRMWARE_REGISTER,
                                       slot ? slot->size : 0, TUNNEL_FOTA_URL_PREFIX);
    // Do not remember BUSY/rejected as accepted; a reused ID still cannot start twice.
    if (error) {
        ack(q, "rejected", error);
        return;
    }
    if (!storage) {
        ack(q, "rejected", "storage");
        return;
    }
    if (!safe || phase != IDLE) {
        ack(q, "rejected", "busy");
        return;
    }
    replay.remember(q);
    destination = slot;
    granted = q;
    cancelled = false;
    phase = FENCING;
    EspRequestUpdateFence();
}
void EspFotaTransmit() {
    Tx t;
    if (tx && xQueueReceive(tx, &t, 0) == pdTRUE)
        EspMqttFotaPublish(t.ack, t.data);
}
