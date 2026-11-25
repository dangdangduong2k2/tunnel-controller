#pragma once
#include <stddef.h>
#include <stdint.h>

namespace dwinpatch {
enum Result : uint8_t {
    NOT_RUN, ALREADY_CURRENT, UPDATED, NO_REPLY, WRONG_PANEL, WRONG_LAYOUT,
    IO_ERROR, ABORTED, JOURNAL_ERROR, RAM_MISMATCH, FLASH_MISMATCH
};
struct Fingerprint { uint8_t slot; uint16_t crc; };
struct Image {
    const uint8_t *block; // exactly 32768 bytes: slot 13, block 0
    uint16_t oldCrc, newCrc;
    const Fingerprint *rest;
    size_t restCount;
    uint32_t journalTag;
};
class Port {
  public:
    virtual uint32_t now() = 0;
    virtual bool send(const uint8_t *data, size_t size) = 0;
    virtual int receive() = 0;
    virtual void pause(uint32_t ms) = 0; // platform keeps the motor watchdog alive
    virtual bool aborted() = 0;
    virtual void emergency() = 0;
    virtual ~Port() {}
};
class Journal {
  public:
    virtual bool pending(uint32_t tag) = 0;
    virtual bool arm(uint32_t tag) = 0; // durable/read-back before flash trigger
    virtual bool clear() = 0;
    virtual ~Journal() {}
};
// Boot-only exclusive UART owner. No application commands, reader or normal
// HMI publisher may run concurrently. No automatic flash retry in this run.
class Updater {
  public:
    Updater(Port &p, Journal &j, const Image &i) : port(p), journal(j), image(i) {}
    Result run();
    const char *stage() const { return phase; }
    bool wroteFlash() const { return flashTriggered; }
  private:
    bool read(uint16_t vp, uint8_t words, uint8_t *out);
    bool write(uint16_t vp, const uint8_t *data, size_t size);
    bool wait(uint8_t cmd, uint16_t vp, uint8_t words, uint8_t *out);
    bool operation(uint16_t vp, const uint8_t *data, uint8_t size, uint8_t *out);
    bool crc(uint8_t slot, uint16_t &value);
    bool restartHome();
    Result failed();
    Port &port;
    Journal &journal;
    const Image &image;
    const char *phase = "not_run";
    bool movedToBoot = false, flashTriggered = false;
};
const char *name(Result result);
} // namespace dwinpatch
