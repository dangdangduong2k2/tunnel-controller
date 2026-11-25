#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include <time.h>
#include "EspHistory.h"
extern "C" {
#include "../../control/tunnel.h"
}

// NVS namespace "tunnel-hist": keys "h00".."h24", one 244-byte blob each
// (20 records x 12 bytes + CRC-32). No index blob: see app/History.h.
static Preferences store;
static bool storageReady = false, sntpStarted = false, wasQuiet = false, retrying = false;
static hist::Log records;
// One chunk handed from the control task to the writer task. The control task
// fills it only in state IDLE; the writer reads it only in state WRITE.
enum { IDLE, WRITE, DONE, FAILED };
static std::atomic<int> job(IDLE);
static hist::Chunk outgoing;
static unsigned outgoingChunk = 0;
static uint32_t quietSince = 0, failedAt = 0;
static const uint32_t SETTLE_MS = 300, RETRY_MS = 5000;
static const time_t CLOCK_VALID_FROM =
    1700000000; // 2023-11-14; an unsynchronized clock starts at 0

static void keyOf(unsigned chunk, char *out) {
    snprintf(out, 8, "h%02u", chunk);
}
static bool readChunk(unsigned chunk, hist::Chunk &out) {
    char key[8];
    keyOf(chunk, key);
    return storageReady && store.getBytesLength(key) == sizeof(out) &&
           store.getBytes(key, &out, sizeof(out)) == sizeof(out);
}
// Flash work never runs on the control or motor-safety task: an NVS page erase
// can take longer than the 100 ms control-loop watchdog allows.
static void writer(void *) {
    static hist::Chunk verify;
    for (;;) {
        if (job.load() == WRITE) {
            char key[8];
            keyOf(outgoingChunk, key);
            bool ok = store.putBytes(key, &outgoing, sizeof(outgoing)) == sizeof(outgoing) &&
                      store.getBytes(key, &verify, sizeof(verify)) == sizeof(verify) &&
                      !memcmp(&verify, &outgoing, sizeof(verify));
            job = ok ? DONE : FAILED;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
// Control task, inside Tunnel_Process: RAM only, returns immediately.
static void finalized(uint8_t semi, uint16_t quantity, uint32_t cycleToken) {
    records.append(cycleToken, EspTimeNow(), semi ? hist::MODE_SEMI : hist::MODE_AUTO, quantity);
}
uint32_t EspTimeNow() {
    time_t now = time(nullptr);
    return now >= CLOCK_VALID_FROM ? uint32_t(now) : 0;
}
const hist::Log &EspHistoryLog() {
    return records;
}
bool EspHistoryWriting() {
    return job.load() != IDLE;
}
void EspHistoryBegin() {
    storageReady = store.begin("tunnel-hist", false);
    const hist::Storage io = {readChunk, nullptr};
    records.load(io);
    if (storageReady && xTaskCreate(writer, "history-store", 4096, nullptr, 1, nullptr) != pdPASS)
        storageReady = false; // history then lives in RAM until the next restart
    Tunnel_SetFinalizeHook(finalized);
}
void EspHistoryPoll(bool quiet, bool hasIp) {
    const uint32_t now = millis();
    const int state = job.load();
    if (state == DONE)
        job = IDLE;
    else if (state == FAILED) {
        records.markDirty(outgoingChunk); // keep it pending, try again later
        retrying = true;
        failedAt = now;
        job = IDLE;
    }
    if (!quiet) {
        wasQuiet = false;
        return;
    }
    if (!wasQuiet) {
        wasQuiet = true;
        quietSince = now;
    }
    if (uint32_t(now - quietSince) < SETTLE_MS)
        return;
    // UTC+7 without daylight saving. SNTP keeps polling in the lwIP task; the clock
    // counts as known only once it has actually been set (see EspTimeNow).
    if (hasIp && !sntpStarted) {
        sntpStarted = true;
        configTzTime("<+07>-7", "pool.ntp.org", "time.google.com");
        return;
    }
    if (retrying && uint32_t(now - failedAt) < RETRY_MS)
        return;
    retrying = false;
    if (storageReady && job.load() == IDLE && records.takeDirty(outgoingChunk, outgoing))
        job = WRITE;
}
