#include "History.h"
#include <string.h>
namespace hist {
static_assert(sizeof(Record) == 12 && sizeof(Chunk) == 244, "stored layout");
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
static unsigned slotOf(uint32_t sequence) {
    return unsigned((sequence - 1) % CAPACITY);
}
bool Log::valid(unsigned slot) const {
    const Record &r = records[slot];
    return r.sequence && slotOf(r.sequence) == slot && uint32_t(last - r.sequence) < CAPACITY;
}
unsigned Log::load(const Storage &io) {
    memset(records, 0, sizeof(records));
    last = 0;
    pending = 0;
    hasToken = false;
    for (unsigned c = 0; c < CHUNKS; c++) {
        Chunk chunk = {};
        if (!io.read || !io.read(c, chunk) ||
            chunk.crc != checksum(chunk.records, sizeof(chunk.records)))
            continue;
        for (unsigned i = 0; i < CHUNK_RECORDS; i++) {
            const Record &r = chunk.records[i];
            unsigned slot = c * CHUNK_RECORDS + i;
            // A record in the wrong slot is not ours, whatever its CRC says.
            if (!r.sequence || slotOf(r.sequence) != slot || r.mode > MODE_SEMI)
                continue;
            records[slot] = r;
            if (r.sequence > last)
                last = r.sequence;
        }
    }
    stored = 0;
    for (unsigned slot = 0; slot < CAPACITY; slot++) {
        if (valid(slot))
            ++stored;
        else
            records[slot] = Record{}; // older than the newest 500: superseded
    }
    ++changes;
    return stored;
}
bool Log::append(uint32_t token, uint32_t time, uint8_t mode, uint16_t count) {
    mode = mode ? MODE_SEMI : MODE_AUTO;
    if (hasToken && token == lastToken && mode == lastMode)
        return false;
    if (last == 0xffffffffu)
        return false; // sequence space exhausted; never reuse a number
    hasToken = true;
    lastToken = token;
    lastMode = mode;
    Record r = {};
    r.sequence = last + 1;
    r.time = time;
    r.count = count;
    r.mode = mode;
    unsigned slot = slotOf(r.sequence);
    last = r.sequence;
    records[slot] = r;
    // Recount: advancing 'last' can also age out a record left by a lost chunk.
    stored = 0;
    for (unsigned i = 0; i < CAPACITY; i++)
        if (valid(i))
            ++stored;
    pending |= 1u << (slot / CHUNK_RECORDS);
    ++changes;
    return true;
}
bool Log::get(unsigned index, Record &out) const {
    if (index >= stored)
        return false;
    unsigned slot = slotOf(last);
    for (unsigned n = 0; n < CAPACITY; n++) {
        if (valid(slot)) {
            if (!index--) {
                out = records[slot];
                return true;
            }
        }
        slot = slot ? slot - 1 : CAPACITY - 1;
    }
    return false;
}
bool Log::takeDirty(unsigned &chunk, Chunk &out) {
    for (unsigned c = 0; c < CHUNKS; c++) {
        if (!(pending & (1u << c)))
            continue;
        pending &= ~(1u << c);
        memset(&out, 0, sizeof(out));
        for (unsigned i = 0; i < CHUNK_RECORDS; i++)
            if (valid(c * CHUNK_RECORDS + i))
                out.records[i] = records[c * CHUNK_RECORDS + i];
        out.crc = checksum(out.records, sizeof(out.records));
        chunk = c;
        return true;
    }
    return false;
}
void Log::markDirty(unsigned chunk) {
    if (chunk < CHUNKS)
        pending |= 1u << chunk;
}
} // namespace hist
