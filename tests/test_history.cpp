#include "History.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
using namespace hist;
// Fake NVS: 25 blobs, each present or not.
static Chunk blobs[CHUNKS];
static bool present[CHUNKS];
static unsigned writes, lastWritten;
static bool failWrites;
static bool readChunk(unsigned c, Chunk &out) {
    if (c >= CHUNKS || !present[c])
        return false;
    out = blobs[c];
    return true;
}
static bool writeChunk(unsigned c, const Chunk &value) {
    ++writes;
    lastWritten = c;
    if (failWrites)
        return false;
    blobs[c] = value;
    present[c] = true;
    return true;
}
static const Storage io = {readChunk, writeChunk};
// What the platform layer does: one chunk per pass, retry on failure.
static unsigned persist(Log &log) {
    unsigned chunk, done = 0;
    Chunk data;
    while (log.takeDirty(chunk, data)) {
        if (!io.write(chunk, data)) {
            log.markDirty(chunk);
            break;
        }
        ++done;
    }
    return done;
}
static void expectRange(const Log &log, uint32_t newest, unsigned count) {
    CHECK(log.size() == count);
    Record r;
    for (unsigned i = 0; i < count; i++) {
        CHECK(log.get(i, r) && r.sequence == newest - i);
        CHECK(r.count == uint16_t(r.sequence * 3) && r.mode == r.sequence % 2);
        CHECK(r.time == (r.sequence % 5 ? 1700000000u + r.sequence : 0u));
    }
    CHECK(!log.get(count, r));
}
static bool add(Log &log, uint32_t sequence) {
    return log.append(0x1000 + sequence * 7, sequence % 5 ? 1700000000u + sequence : 0,
                      sequence % 2, uint16_t(sequence * 3));
}
int main() {
    CHECK(sizeof(Record) == 12 && sizeof(Chunk) == 244 && CHUNKS == 25);
    static Log log;
    CHECK(log.load(io) == 0 && log.size() == 0 && !log.dirty());
    Record r;
    CHECK(!log.get(0, r));
    uint32_t revision = log.revision();
    CHECK(add(log, 1) && add(log, 2) && add(log, 3));
    CHECK(log.revision() != revision && log.dirty());
    expectRange(log, 3, 3);
    // One finalization reported twice is one record.
    revision = log.revision();
    CHECK(!add(log, 3) && !add(log, 3) && log.size() == 3 && log.revision() == revision);
    CHECK(log.get(0, r) && r.sequence == 3);
    CHECK(persist(log) == 1 && writes == 1 && lastWritten == 0 && !log.dirty());
    // The duplicate guard is about the latest report only, and survives nothing else.
    CHECK(add(log, 4));
    CHECK(persist(log) == 1 && writes == 2); // one small chunk per new record

    // Wraparound: 1203 results, the newest 500 remain, every write is a single chunk.
    for (uint32_t n = 5; n <= 1203; n++) {
        CHECK(add(log, n));
        CHECK(persist(log) == 1 && lastWritten == ((n - 1) % CAPACITY) / CHUNK_RECORDS);
    }
    CHECK(writes == 1201); // 1..3 went out together, then one write per record
    expectRange(log, 1203, 500);
    // Boot: order is recovered from the sequence numbers alone.
    static Log boot;
    CHECK(boot.load(io) == 500 && !boot.dirty());
    expectRange(boot, 1203, 500);
    CHECK(add(boot, 1204)); // tokens do not persist; a new result is always stored
    expectRange(boot, 1204, 500);
    CHECK(persist(boot) == 1 && lastWritten == 10);

    // A corrupt chunk loses only its own 20 records (sequence 981..1000 live in chunk 24).
    blobs[24].records[3].count ^= 1;
    static Log damaged;
    CHECK(damaged.load(io) == 480);
    for (unsigned i = 0; i < 480; i++) {
        CHECK(damaged.get(i, r));
        uint32_t expected = i < 204 ? 1204 - i : 1204 - i - 20;
        CHECK(r.sequence == expected && r.count == uint16_t(expected * 3));
    }
    CHECK(!damaged.get(480, r));
    // 1205 replaces 705 in its slot: still 480 records.
    CHECK(add(damaged, 1205) && damaged.size() == 480 && damaged.get(0, r) && r.sequence == 1205);
    blobs[24].records[3].count ^= 1;
    // A missing chunk (never written / erased) behaves the same.
    present[0] = false; // holds 1001..1020
    static Log missing;
    CHECK(missing.load(io) == 480 && missing.get(0, r) && r.sequence == 1204);
    CHECK(missing.get(183, r) && r.sequence == 1021 && missing.get(184, r) && r.sequence == 1000);
    present[0] = true;
    // A bad CRC field itself, and an all-zero blob, are rejected or empty.
    blobs[5].crc ^= 0x80000000u;
    static Log crc;
    CHECK(crc.load(io) == 480);
    blobs[5].crc ^= 0x80000000u;

    // A chunk left behind by failed writes is older than the newest 500: ignored.
    Chunk saved = blobs[3];
    for (unsigned i = 0; i < CHUNK_RECORDS; i++) {
        blobs[3].records[i] = Record{};
        blobs[3].records[i].sequence = 61 + i; // what slot 60..79 held one lap earlier
        blobs[3].records[i].count = 7;
    }
    blobs[3].crc = checksum(blobs[3].records, sizeof(blobs[3].records));
    static Log stale;
    CHECK(stale.load(io) == 480);
    for (unsigned i = 0; i < 480; i++)
        CHECK(stale.get(i, r) && r.count != 7);
    // A record that does not belong to its slot is not trusted even with a good CRC.
    blobs[3] = saved;
    blobs[3].records[0].sequence += 1;
    blobs[3].crc = checksum(blobs[3].records, sizeof(blobs[3].records));
    static Log misplaced;
    CHECK(misplaced.load(io) == 499);
    blobs[3] = saved;

    // Write failure: the chunk stays pending and is retried; RAM content is unaffected.
    static Log retry;
    CHECK(retry.load(io) == 500);
    CHECK(add(retry, 1205));
    failWrites = true;
    unsigned before = writes;
    CHECK(persist(retry) == 0 && retry.dirty() && writes == before + 1);
    CHECK(persist(retry) == 0 && retry.dirty());
    failWrites = false;
    CHECK(persist(retry) == 1 && !retry.dirty());
    static Log after;
    CHECK(after.load(io) == 500 && after.get(0, r) && r.sequence == 1205 &&
          r.count == uint16_t(1205 * 3));

    // First boot with empty storage, then exactly 500 and 501 records.
    memset(present, 0, sizeof(present));
    static Log fresh;
    CHECK(fresh.load(io) == 0);
    for (uint32_t n = 1; n <= 500; n++)
        CHECK(add(fresh, n));
    expectRange(fresh, 500, 500);
    CHECK(persist(fresh) == 25);
    CHECK(add(fresh, 501));
    expectRange(fresh, 501, 500);
    // Same token but the other mode is a different result (cannot be the same cycle).
    CHECK(fresh.append(42, 0, MODE_SEMI, 1) && fresh.append(42, 0, MODE_AUTO, 1));
    CHECK(!fresh.append(42, 0, MODE_AUTO, 1));
    printf("History passed: 500-record wraparound, boot order from sequence numbers, corrupt/"
           "missing/stale chunks, duplicate finalization, single-chunk writes, write retry\n");
    return 0;
}
