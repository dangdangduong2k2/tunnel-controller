#pragma once
#include <stddef.h>
#include <stdint.h>

// Portable protocol/application boundary. All callbacks run on the control loop.
namespace mb {
enum { RTU = 1, TCP_FIRST = 2, TCP_LAST = 4, LEGACY = 5, MQTT = 6, PEERS = 7 };
enum { EX_FUNCTION = 1, EX_ADDRESS = 2, EX_VALUE = 3, EX_FAILURE = 4, EX_BUSY = 6 };
struct Backend {
    uint16_t (*read)(uint16_t address);
    uint8_t (*command)(uint8_t command, uint16_t value);
    bool (*readerBegin)(uint8_t command, const uint8_t *data, uint8_t length);
    bool (*readerReply)(uint8_t *data, uint8_t *length, uint8_t *status);
    void (*heartbeat)();
    void (*stop)(bool emergency);
};
class Core {
  public:
    explicit Core(const Backend &backend) : io(backend) {}
    void tick(uint32_t now);
    void disconnect(uint8_t peer);
    void resetPeer(uint8_t peer);
    bool legacyAllow(uint8_t command);
    bool owns(uint8_t peer) const {
        return controller == peer;
    }
    bool readerBusy() const {
        return readerState == 1;
    }
    bool active() const {
        return controller != 0;
    }
    uint32_t controlLeaseMs() const;
    uint8_t owner() const {
        return controller;
    }
    // PDU excludes unit id/MBAP/CRC. Returns PDU length, including exceptions.
    size_t pdu(uint8_t peer, const uint8_t *request, size_t length, uint8_t *reply);
    static uint16_t crc(const uint8_t *data, size_t length);
    size_t rtu(uint8_t unit, const uint8_t *request, size_t length, uint8_t *reply);
    size_t tcp(uint8_t peer, const uint8_t *request, size_t length, uint8_t *reply,
               uint8_t unit = 1);

  private:
    struct History {
        bool valid = false;
        uint16_t words[8] = {};
        uint16_t result = 0;
    } history[PEERS];
    Backend io;
    uint8_t controller = 0, readerState = 0, readerStatus = 0, readerLength = 0;
    uint8_t readerData[64] = {};
    uint16_t readerSequence = 0, lastSequence = 0, lastResult = 0;
    uint32_t clock = 0, leaseAt = 0;
    uint16_t read(uint8_t peer, uint16_t address);
    uint8_t write(uint8_t peer, uint16_t address, const uint16_t *values, uint16_t count);
    uint8_t execute(const uint16_t *values);
    void touch();
};
} // namespace mb
