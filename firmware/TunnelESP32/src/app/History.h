#pragma once
#include <stdint.h>
#include <stddef.h>
// Finalized scan results: the 500 most recent, platform independent.
// The whole log lives in RAM; persistence is 25 fixed chunks of 20 records,
// each with its own CRC and no separate index. A record's slot is derived from
// its sequence number, so order is recovered from the records themselves and a
// missing or corrupt chunk only loses the records it held.
namespace hist {
constexpr unsigned CAPACITY = 500, CHUNK_RECORDS = 20, CHUNKS = CAPACITY / CHUNK_RECORDS;
enum : uint8_t { MODE_AUTO = 0, MODE_SEMI = 1 };
#pragma pack(push, 1)
struct Record {
    uint32_t sequence; // 1.., increasing; 0 = empty slot
    uint32_t time;     // Unix seconds (UTC); 0 = clock was not synchronized
    uint16_t count;    // unique EPC count shown to the operator
    uint8_t mode;      // MODE_AUTO / MODE_SEMI
    uint8_t reserved;
};
struct Chunk {
    Record records[CHUNK_RECORDS];
    uint32_t crc; // cfg::checksum-compatible CRC-32 of records
};
#pragma pack(pop)
uint32_t checksum(const void *data, size_t length);
// Same style as cfg::Storage: injected, may fail.
struct Storage {
    bool (*read)(unsigned chunk, Chunk &out);
    bool (*write)(unsigned chunk, const Chunk &value);
};
class Log {
  public:
    // Reads every chunk; returns the number of records recovered.
    unsigned load(const Storage &io);
    // token identifies the finalization being reported (see tunnel.h). A repeat
    // of the previous token and mode is the same result and is not stored.
    bool append(uint32_t token, uint32_t time, uint8_t mode, uint16_t count);
    unsigned size() const {
        return stored;
    }
    // index 0 = newest. False past the end.
    bool get(unsigned index, Record &out) const;
    // Changes whenever the visible content changes.
    uint32_t revision() const {
        return changes;
    }
    // Persistence, one chunk at a time. takeDirty copies a chunk that differs
    // from storage and clears its flag; call markDirty again if the write failed.
    bool takeDirty(unsigned &chunk, Chunk &out);
    void markDirty(unsigned chunk);
    bool dirty() const {
        return pending != 0;
    }

  private:
    bool valid(unsigned slot) const;
    Record records[CAPACITY] = {};
    uint32_t last = 0, changes = 0, pending = 0, lastToken = 0;
    unsigned stored = 0;
    uint8_t lastMode = 0;
    bool hasToken = false;
};
} // namespace hist
