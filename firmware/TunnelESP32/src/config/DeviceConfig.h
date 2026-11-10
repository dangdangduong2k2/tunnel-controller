#pragma once
#include <stdint.h>
#include <stddef.h>
namespace cfg {
#pragma pack(push, 1)
struct Settings {
    uint8_t dhcp, unit, parity, stopBits, mqttEnabled, adminSet;
    uint32_t baud;
    uint16_t tcpPort, mqttPort;
    uint8_t ip[4], mask[4], gateway[4], dns[4];
    char host[128], user[65], password[129], root[65];
    uint8_t adminSalt[16], adminHash[32];
};
struct Record {
    uint32_t magic;
    uint16_t schema, bytes;
    uint32_t revision;
    Settings settings;
    uint32_t crc;
};
#pragma pack(pop)
Settings defaults(bool recovery = false);
const char *validate(const Settings &s);
uint32_t checksum(const void *data, size_t length);
Record record(const Settings &s, uint32_t revision);
bool valid(const Record &r);
// Persistence is two independent, CRC-protected NVS blobs. No erase-before-save.
struct Storage {
    bool (*read)(unsigned slot, Record &out);
    bool (*write)(unsigned slot, const Record &value);
};
class Store {
  public:
    explicit Store(Storage io) : io(io) {}
    bool load();
    bool save(const Settings &s);
    const Settings &get() const {
        return current.settings;
    }
    uint32_t revision() const {
        return current.revision;
    }

  private:
    Storage io;
    Record current = record(defaults(), 0);
    unsigned slot = 1;
};
struct Update {
    Settings settings;
    char adminPassword[65] = {};
    uint32_t revision = 0;
};
// Partial JSON update; missing secrets are preserved, empty MQTT password clears.
const char *parse(const char *text, size_t length, const Settings &base, Update &out);
// Public config contains flags for secrets, never password/hash/salt.
bool json(const Settings &s, uint32_t revision, char *out, size_t capacity);
// Pure maintenance gate shared by the firmware and tests.
struct Safety {
    bool owner, moving, scanning, readerPending;
};
bool canApply(const Safety &s);
} // namespace cfg
