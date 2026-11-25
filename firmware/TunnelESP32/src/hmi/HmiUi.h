#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../config/DeviceConfig.h"
#include "../app/History.h"
#include "HmiLayout.h"
#include "AntennaSettings.h"
// DWIN HMI v2 settings/history UI. Platform independent: every output and every
// machine/config access goes through Host. Port of the panel package's reference
// simulate_esp32.py. Never blocks: variables are marked stale and written a few
// frames per flush(); a refused frame is simply retried on a later flush().
namespace hmiui {
enum Pill : uint8_t { READY = 1, RUNNING = 2, REMOTE = 3, LINK_LOST = 4, VERIFY = 5, STOPPED = 6 };
enum Lock : uint8_t { LOCK_NONE = 0, LOCK_WAIT_IDLE = 1, LOCK_REMOTE = 2, LOCK_LINK = 3 };
// Facts read from the machine by the platform layer; nothing is invented here.
struct Machine {
    bool stopped; // stop latched (Emergency/safety stop) or the cycle is in FAULT
    bool remote;  // Modbus/MQTT/PC owner, DWIN control lock or PC maintenance
    bool verify;  // TUNNEL_WAIT_CONFIRM
    bool running; // a motor or scan is active, or the state machine is mid-cycle
};
uint8_t pill(const Machine &m);
uint8_t lockNote(uint8_t pill);

struct Status {
    uint8_t pill = READY;
    // Platform permission, checked again on Apply/Confirm. 0xff keeps legacy callers conservative.
    uint8_t settingsLock = 0xff;
    bool panelSeen = false; // the panel has answered at least once
    bool link = false, hasIp = false;
    uint8_t ip[4] = {}, mask[4] = {}, gateway[4] = {}, dns[4] = {};
    uint8_t speed = 1;        // conveyor speed level in use, 1..5
    uint32_t forcedPages = 0; // page writes made by the machine code, not by this UI
    uint16_t forcedPage = 0;  // the page of the latest such write
};
enum Submit : uint8_t { SUBMIT_OK, SUBMIT_INVALID, SUBMIT_STORAGE, SUBMIT_IN_PROGRESS };
enum Apply : uint8_t { APPLY_IDLE, APPLY_PENDING, APPLY_DENIED, APPLY_FAILED, APPLY_RESTARTING };
enum : uint8_t { SPEED_OK = 0, SPEED_BUSY = 2, SPEED_STORE = 8 };

class Host {
  public:
    // Transport. False = not sent now (UART full); the UI retries later.
    // bytes is even and at most Ui::MAX_TEXT_BYTES.
    virtual bool sendText(uint16_t vp, const char *text, uint16_t bytes) = 0;
    virtual bool sendWord(uint16_t vp, uint16_t value) = 0;
    virtual bool sendPage(uint16_t page) = 0;
    // Stored communication settings and the save-and-restart path.
    virtual const cfg::Settings &settings() = 0;
    virtual uint8_t submitSettings(const cfg::Settings &next) = 0; // Submit
    virtual uint8_t applyState() = 0;                              // Apply
    virtual uint8_t setSpeed(uint8_t level) = 0;                   // SPEED_*
    virtual const antenna::State &antennaState() {
        static const antenna::State unavailable;
        return unavailable;
    }
    virtual bool requestAntenna(const antenna::Values *, bool) { return false; }
    virtual const hist::Log &history() = 0;
    virtual ~Host() {}
};

// Pixel width of a string on the panel and the reference fit()/X computations.
unsigned textWidth(uint8_t face, const char *text);
// Shortens text in place with "..." until it fits; capacity includes the NUL.
void fitText(uint8_t face, char *text, size_t capacity, unsigned room);
// X register value of an aligned item for an already fitted text.
uint16_t alignX(unsigned item, const char *text);
// Field checks of the reference validate(); nullptr = accepted. message has 96 bytes.
const char *validateField(uint8_t field, const char *value, char *message);
// "dd/mm/yyyy hh:mm:ss" in UTC+7, or the unknown-time placeholder for 0.
void formatTime(uint32_t unixTime, char *out, size_t capacity);

class Ui {
  public:
    static constexpr unsigned FRAMES_PER_FLUSH = 6, HISTORY_ROWS = 7, OPTION_SLOTS = 20;
    // Largest text write: 108 characters + FF FF. Keeps every frame (header and CRC
    // included) under 120 bytes, the largest single item the 256-byte UART queue takes.
    static constexpr unsigned MAX_TEXT_BYTES = HMIL_MAX_TEXT_BYTES;
    static constexpr uint32_t TOAST_MS = 3500, REFRESH_MS = 80;
    explicit Ui(Host &host) : host(host) {}
    // Call in this order on the task that owns the panel UART:
    void tick(uint32_t now, const Status &status); // follow machine/network/time
    void event(uint16_t code);                     // one value received on VP 0x1090
    void flush();                                  // send a bounded number of frames
    void notice(const char *message) { toast(message); }

    uint16_t currentPage() const {
        return page;
    }
    bool editing() const {
        return edit.active;
    }
    bool dialogOpen() const {
        return dialog.action != 0;
    }
    bool unapplied() const;
    bool idle() const; // nothing left to send
    const cfg::Settings &draftSettings() const {
        return draft;
    }
    uint8_t draftSpeedLevel() const {
        return draftSpeed;
    }

  private:
    enum Action : uint8_t { NO_ACTION, DISCARD, APPLY_NETWORK, APPLY_SPEED, APPLY_ANTENNA };
    void loadAntenna();
    void touchAntenna();
    bool antennaEditable();
    bool antennaDirty() const;
    void touch(unsigned item);
    void touchAll();
    void touchFields();
    void touchHistory();
    void touchEditor();
    void show(uint16_t target);
    void toast(const char *message);
    bool locked();
    uint8_t settingsLock() const;
    void navigate(uint8_t target);
    void fieldTapped(uint8_t field);
    void segment(uint8_t field, uint8_t option);
    void key(char c);
    void openEditor(uint8_t field);
    void closeEditor(bool commit);
    void ask(const char *title, const char *body, uint16_t button, Action action, uint16_t back);
    void apply();
    void confirm(bool ok);
    void historyStep(int direction);
    void fieldText(uint8_t field, char *out, size_t capacity) const;
    void liveAddress(const uint8_t *address, bool primary, char *out, size_t capacity) const;
    void render(unsigned item, char *out, size_t capacity) const;
    uint16_t word(unsigned item) const;
    bool send(unsigned item);

    Host &host;
    Status st;
    cfg::Settings draft = {};
    uint8_t draftSpeed = 1;
    antenna::Values draftAntenna, appliedAntenna;
    bool antennaValid = false, antennaSave = true, appliedAntennaSave = true;
    bool watchAntenna = false;
    uint32_t antennaRevision = 0;
    bool started = false, toastOn = false, watchApply = false;
    uint16_t page = HMIL_PAGE_home, pendingPage = 0;
    uint32_t clock = 0, toastAt = 0, refreshAt = 0, seenForced = 0, seenHistory = 0;
    unsigned refreshItem = 0, historyPage = 0;
    uint32_t stale[(HMIL_ITEM_COUNT + 31) / 32] = {};
    struct {
        bool active, reveal;
        uint8_t field, optionPage;
        uint16_t back;
        char value[130];
        char error[96];
    } edit = {};
    struct {
        uint8_t action;
        uint16_t back, target, button;
        const char *title, *body;
    } dialog = {};
    char toastText[96] = {};
};
} // namespace hmiui
