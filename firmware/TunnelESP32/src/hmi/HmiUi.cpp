#include "HmiUi.h"
#include <stdio.h>
#include <string.h>

namespace hmiui {
namespace {
enum Kind : uint8_t { K_NONE, K_IP, K_NUMBER, K_TEXT, K_PASSWORD, K_OPTIONS };
struct Field {
    Kind kind;
    const char *title;
    uint32_t low, high; // number range; high = maximum length for text
    const char *const *options;
    unsigned optionCount;
};
const char *const BAUDS[] = {"1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200"};
// Editable fields of this firmware (titles/ranges of the reference simulator).
// Antenna power is editable; remaining Reader pages are not implemented here.
Field describe(uint8_t field) {
    switch (field) {
    case HMIL_FIELD_power1:
    case HMIL_FIELD_power2:
    case HMIL_FIELD_power3:
    case HMIL_FIELD_power4:
        return {K_NUMBER, "Antenna power (dBm)", 0, 30, nullptr, 0};
    case HMIL_FIELD_ip:
        return {K_IP, "IP address", 0, 0, nullptr, 0};
    case HMIL_FIELD_mask:
        return {K_IP, "Subnet mask", 0, 0, nullptr, 0};
    case HMIL_FIELD_gateway:
        return {K_IP, "Gateway", 0, 0, nullptr, 0};
    case HMIL_FIELD_dns:
        return {K_IP, "DNS server", 0, 0, nullptr, 0};
    case HMIL_FIELD_unit:
        return {K_NUMBER, "Device / Unit ID", 1, 247, nullptr, 0};
    case HMIL_FIELD_tcp_port:
        return {K_NUMBER, "Modbus TCP port", 1, 65535, nullptr, 0};
    case HMIL_FIELD_baud:
        return {K_OPTIONS, "RS485 baudrate", 0, 0, BAUDS, 8};
    case HMIL_FIELD_mqtt_port:
        return {K_NUMBER, "Broker port", 1, 65535, nullptr, 0};
    case HMIL_FIELD_mqtt_host:
        return {K_TEXT, "Broker IP / hostname", 0, 127, nullptr, 0};
    case HMIL_FIELD_mqtt_user:
        return {K_TEXT, "Broker username", 0, 64, nullptr, 0};
    case HMIL_FIELD_mqtt_password:
        return {K_PASSWORD, "Broker password", 0, 128, nullptr, 0};
    case HMIL_FIELD_mqtt_root:
        return {K_TEXT, "Topic root / Device ID", 0, 64, nullptr, 0};
    default:
        return {K_NONE, "", 0, 0, nullptr, 0};
    }
}
bool phaseTwo(uint8_t field) {
    return field >= HMIL_FIELD_profile && field <= HMIL_FIELD_maintenance;
}
// Entry line of the editor pages: face and visible width per keyboard (reference draw_entry).
const unsigned ENTRY_ROOM_TEXT = 690, ENTRY_ROOM_PAD = 390;
uint8_t entryFace(Kind kind) {
    return kind == K_IP ? HMIL_FACE_B41 : kind == K_NUMBER ? HMIL_FACE_B76 : HMIL_FACE_SB26;
}
const char NOT_AVAILABLE[] = "Not available in this firmware yet.";
const char BUSY[] = "BUSY - wait until the machine is idle.";
const char BUSY_REMOTE[] = "BUSY - release remote control first.";

uint8_t section(uint16_t page) {
    switch (page) {
    case HMIL_PAGE_ethernet:
        return 1;
    case HMIL_PAGE_modbus:
        return 2;
    case HMIL_PAGE_mqtt:
        return 3;
    case HMIL_PAGE_antennas:
        return 4;
    case HMIL_PAGE_reader_0:
    case HMIL_PAGE_reader_1:
    case HMIL_PAGE_reader_2:
    case HMIL_PAGE_reader_3:
        return 5;
    case HMIL_PAGE_machine:
        return 6;
    default:
        return 0;
    }
}
void dotted(const uint8_t *a, char *out, size_t capacity) {
    snprintf(out, capacity, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
}
bool digits(const char *s) {
    if (!*s)
        return false;
    for (; *s; ++s)
        if (*s < '0' || *s > '9')
            return false;
    return true;
}
// Exactly (\d{1,3}\.){3}\d{1,3} with every number <= 255.
bool parseAddress(const char *s, uint8_t *out) {
    for (unsigned i = 0; i < 4; i++) {
        unsigned value = 0, count = 0;
        while (*s >= '0' && *s <= '9') {
            value = value * 10 + unsigned(*s++ - '0');
            if (++count > 3)
                return false;
        }
        if (!count || value > 255)
            return false;
        out[i] = uint8_t(value);
        if (i < 3) {
            if (*s++ != '.')
                return false;
        } else if (*s)
            return false;
    }
    return true;
}
bool sameNetwork(const cfg::Settings &a, const cfg::Settings &b) {
    return a.dhcp == b.dhcp && a.unit == b.unit && a.parity == b.parity &&
           a.stopBits == b.stopBits && a.mqttEnabled == b.mqttEnabled && a.baud == b.baud &&
           a.tcpPort == b.tcpPort && a.mqttPort == b.mqttPort && !memcmp(a.ip, b.ip, 4) &&
           !memcmp(a.mask, b.mask, 4) && !memcmp(a.gateway, b.gateway, 4) &&
           !memcmp(a.dns, b.dns, 4) && !strcmp(a.host, b.host) && !strcmp(a.user, b.user) &&
           !strcmp(a.password, b.password) && !strcmp(a.root, b.root);
}
const char *explain(const char *error) {
    if (!strcmp(error, "mask"))
        return "This subnet mask cannot be used.";
    if (!strcmp(error, "ip"))
        return "The IP address is not valid for this subnet.";
    if (!strcmp(error, "gateway"))
        return "The gateway must be in the same subnet as the IP.";
    if (!strcmp(error, "dns"))
        return "The DNS server address is not valid.";
    if (!strcmp(error, "mqtt_host"))
        return "Enter a valid broker hostname first.";
    if (!strcmp(error, "mqtt_root"))
        return "The topic root is not valid.";
    if (!strcmp(error, "tcp_port"))
        return "Port 80 is reserved for the web page.";
    return "These settings are not valid.";
}
} // namespace

uint8_t pill(const Machine &m) {
    return m.stopped   ? STOPPED
           : m.remote  ? REMOTE
           : m.verify  ? VERIFY
           : m.running ? RUNNING
                       : READY;
}
uint8_t lockNote(uint8_t p) {
    return p == READY       ? LOCK_NONE
           : p == REMOTE    ? LOCK_REMOTE
           : p == LINK_LOST ? LOCK_LINK
                            : LOCK_WAIT_IDLE;
}
unsigned textWidth(uint8_t face, const char *text) {
    if (face >= HMIL_FACE_COUNT)
        return 0;
    const HmilFont &f = HMIL_FONTS[face];
    unsigned width = 0;
    for (; *text; ++text) {
        unsigned char c = (unsigned char)*text;
        uint8_t glyph = (c >= 33 && c <= 126) ? f.width[c - 33] : 0;
        width += glyph ? unsigned(glyph) + f.gap : f.space;
    }
    return width;
}
void fitText(uint8_t face, char *text, size_t capacity, unsigned room) {
    if (textWidth(face, text) <= room)
        return;
    size_t n = strlen(text);
    if (n + 4 > capacity)
        n = capacity > 4 ? capacity - 4 : 0;
    for (;;) {
        memcpy(text + n, "...", 4);
        if (!n || textWidth(face, text) <= room)
            return;
        --n;
    }
}
uint16_t alignX(unsigned item, const char *text) {
    const HmilVar &v = HMIL_ITEMS[item];
    int left = v.box_left, right = v.box_right, width = int(textWidth(v.face, text));
    int x = v.align == HMIL_ALIGN_RIGHT ? right - width : (left + right - width) / 2;
    return uint16_t(x < left ? left : x);
}
const char *validateField(uint8_t field, const char *v, char *message) {
    const Field f = describe(field);
    message[0] = 0;
    if (f.kind == K_NUMBER) {
        unsigned long long n = 0;
        bool ok = digits(v) && strlen(v) <= 12;
        for (const char *p = v; ok && *p; ++p)
            n = n * 10 + unsigned(*p - '0');
        if (!ok || n < f.low || n > f.high) {
            snprintf(message, 96, "Enter a whole number from %lu to %lu.", (unsigned long)f.low,
                     (unsigned long)f.high);
            return message;
        }
        if (field == HMIL_FIELD_tcp_port && n == 80)
            return strcpy(message, "Port 80 is reserved for the web page.");
    }
    if (f.kind == K_IP) {
        uint8_t a[4];
        if (!parseAddress(v, a))
            return strcpy(message, "Enter four numbers from 0 to 255, separated by dots.");
        if (field == HMIL_FIELD_mask) {
            uint32_t m = (uint32_t(a[0]) << 24) | (uint32_t(a[1]) << 16) | (uint32_t(a[2]) << 8) |
                         a[3],
                     inverse = ~m;
            if (!m || (inverse & (inverse + 1)))
                return strcpy(message, "Enter a contiguous subnet mask.");
        }
    }
    size_t n = strlen(v);
    if (field == HMIL_FIELD_mqtt_host) {
        // Reference rule (no blank, '/', ':') plus the character set cfg::validate accepts.
        bool ok = n && v[0] != '.' && v[0] != '-' && v[n - 1] != '.' && v[n - 1] != '-' &&
                  !strstr(v, "..");
        for (const char *p = v; ok && *p; ++p)
            ok = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
                 *p == '.' || *p == '-';
        if (!ok)
            return strcpy(message, "Enter a hostname or IPv4 address, without a prefix or path.");
    }
    if (field == HMIL_FIELD_mqtt_root) {
        bool ok = n && v[0] != '/' && v[n - 1] != '/' && !strstr(v, "//");
        for (const char *p = v; ok && *p; ++p)
            ok = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
                 *p == '/' || *p == '-' || *p == '_' || *p == '.';
        if (!ok)
            return strcpy(message, "Use a topic root without spaces, +, #, or leading/trailing /.");
    }
    if (f.kind == K_TEXT || f.kind == K_PASSWORD) {
        bool ok = n <= f.high;
        for (const char *p = v; ok && *p; ++p)
            ok = *p >= 32 && *p <= 126;
        if (!ok) {
            snprintf(message, 96, "Use up to %lu ASCII characters.", (unsigned long)f.high);
            return message;
        }
    }
    return nullptr;
}
void formatTime(uint32_t unixTime, char *out, size_t capacity) {
    if (!unixTime) {
        snprintf(out, capacity, "--/--/---- --:--:--");
        return;
    }
    uint64_t local = uint64_t(unixTime) + 7 * 3600; // UTC+7, no daylight saving
    unsigned seconds = unsigned(local % 86400);
    // Civil date from days since 1970-01-01 (Gregorian, proleptic).
    int64_t z = int64_t(local / 86400) + 719468;
    int64_t era = z / 146097;
    unsigned doe = unsigned(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned day = doy - (153 * mp + 2) / 5 + 1, month = mp < 10 ? mp + 3 : mp - 9;
    unsigned year = unsigned(era * 400 + yoe) + (month <= 2 ? 1 : 0);
    snprintf(out, capacity, "%02u/%02u/%04u %02u:%02u:%02u", day, month, year, seconds / 3600,
             seconds / 60 % 60, seconds % 60);
}

void Ui::touch(unsigned item) {
    if (item < HMIL_ITEM_COUNT)
        stale[item / 32] |= 1u << (item % 32);
}
void Ui::touchAll() {
    for (unsigned i = 0; i < HMIL_ITEM_COUNT; i++)
        touch(i);
}
void Ui::touchFields() {
    for (unsigned f = 1; f <= HMIL_FIELD_COUNT; f++) {
        touch(HMIL_FIELDS[f].text_item);
        touch(HMIL_FIELDS[f].sel_item);
    }
    touch(HMIL_ITEM_speed);
    touch(HMIL_ITEM_apply_en);
}
void Ui::touchAntenna() {
    for (unsigned f = HMIL_FIELD_ant1; f <= HMIL_FIELD_save_reader; ++f) {
        touch(HMIL_FIELDS[f].text_item);
        touch(HMIL_FIELDS[f].sel_item);
    }
    touch(HMIL_ITEM_sum4);
    touch(HMIL_ITEM_apply_en);
}
bool Ui::antennaDirty() const {
    return antennaValid && (!antenna::same(draftAntenna, appliedAntenna) ||
                            antennaSave != appliedAntennaSave);
}
bool Ui::antennaEditable() {
    if (page != HMIL_PAGE_antennas || edit.active || dialog.action != NO_ACTION) return false;
    if (locked()) return false;
    if (host.antennaState().busy) { toast("Reader busy. Please wait."); return false; }
    if (!antennaValid) { toast("Reopen Antennas to read the reader first."); return false; }
    return true;
}
void Ui::loadAntenna() {
    antennaValid = false;
    touchAntenna();
    if (locked()) return;
    antennaRevision = host.antennaState().revision;
    if (!host.requestAntenna(nullptr, true)) {
        toast("Reader not ready. Reopen Antennas when idle.");
        return;
    }
    watchAntenna = true;
    toast("Reading antenna settings...");
}
void Ui::touchHistory() {
    for (unsigned i = 0; i < HISTORY_ROWS; i++) {
        touch(HMIL_ITEM_hist_id1 + i);
        touch(HMIL_ITEM_hist_time1 + i);
        touch(HMIL_ITEM_hist_mode1 + i);
        touch(HMIL_ITEM_hist_count1 + i);
    }
    touch(HMIL_ITEM_hist_rows);
    touch(HMIL_ITEM_hist_saved);
    touch(HMIL_ITEM_hist_range);
}
void Ui::touchEditor() {
    touch(HMIL_ITEM_crumb);
    touch(HMIL_ITEM_edit_title);
    touch(HMIL_ITEM_edit_error);
    touch(HMIL_ITEM_edit_pw);
    if (describe(edit.field).kind == K_OPTIONS) {
        for (unsigned i = 0; i < OPTION_SLOTS; i++) {
            touch(HMIL_ITEM_opt1 + i);
            touch(HMIL_ITEM_optsel1 + i);
        }
        touch(HMIL_ITEM_opt_pager);
        touch(HMIL_ITEM_opt_page);
        touch(HMIL_ITEM_opt_sel);
    } else {
        touch(HMIL_ITEM_edit_hint);
        touch(HMIL_ITEM_edit_entry);
    }
}
void Ui::show(uint16_t target) {
    page = target;
    pendingPage = target; // sent after the variables it displays
}
void Ui::toast(const char *message) {
    const bool replacing = toastOn;
    snprintf(toastText, sizeof(toastText), "%s", message);
    toastOn = true;
    toastAt = clock;
    touch(HMIL_ITEM_toast);
    touch(HMIL_ITEM_toast_show);
    // Centered text changes X when its width changes. Repaint after updating
    // both variables so old transparent glyphs cannot remain outside the new X.
    if (replacing) pendingPage = page;
}
uint8_t Ui::settingsLock() const {
    return st.settingsLock <= LOCK_LINK ? st.settingsLock : lockNote(st.pill);
}
bool Ui::locked() {
    uint8_t note = settingsLock();
    if (note == LOCK_NONE)
        return false;
    toast(note == LOCK_REMOTE ? BUSY_REMOTE : BUSY);
    return true;
}
bool Ui::unapplied() const {
    return !sameNetwork(draft, host.settings()) || draftSpeed != st.speed || antennaDirty();
}
bool Ui::idle() const {
    for (uint32_t bits : stale)
        if (bits)
            return false;
    return !pendingPage;
}

void Ui::tick(uint32_t now, const Status &s) {
    clock = now;
    if (!started) {
        started = true;
        st = s;
        draft = host.settings();
        draftSpeed = s.speed;
        seenForced = s.forcedPages;
        seenHistory = host.history().revision();
        refreshAt = now;
        touchAll();
        return;
    }
    if (s.panelSeen && !st.panelSeen)
        touchAll(); // frames sent before the panel answered may never have arrived
    if (s.pill != st.pill || s.settingsLock != st.settingsLock) {
        touch(HMIL_ITEM_state);
        touch(HMIL_ITEM_lock);
    }
    if (s.link != st.link || s.hasIp != st.hasIp || memcmp(s.ip, st.ip, 4) ||
        memcmp(s.mask, st.mask, 4) || memcmp(s.gateway, st.gateway, 4) ||
        memcmp(s.dns, st.dns, 4)) {
        touch(HMIL_ITEM_ip);
        touch(HMIL_ITEM_mask);
        touch(HMIL_ITEM_gateway);
        touch(HMIL_ITEM_dns);
    }
    if (s.speed != st.speed) {
        if (draftSpeed == st.speed)
            draftSpeed = s.speed; // not being edited: follow a remote change
        touch(HMIL_ITEM_speed);
        touch(HMIL_ITEM_sum6);
        touch(HMIL_ITEM_apply_en);
    }
    st = s;
    const antenna::State &ant = host.antennaState();
    if (watchAntenna && !ant.busy && ant.revision != antennaRevision) {
        antennaRevision = ant.revision;
        watchAntenna = false;
        antennaValid = ant.valid;
        if (ant.valid) {
            draftAntenna = appliedAntenna = ant.values;
            appliedAntennaSave = antennaSave;
            toast(ant.result == antenna::SAVED ? "Antenna settings applied and verified."
                                               : "Antenna settings loaded.");
        } else {
            toast(ant.result == antenna::MISMATCH ? "Readback differs. Reopen Antennas to check."
                  : ant.result == antenna::CANCELLED ? "Reader operation cancelled. Reopen Antennas."
                  : ant.result == antenna::TIMEOUT ? "Reader timeout. Reopen Antennas to check."
                  : "Reader operation failed. Reopen Antennas.");
        }
        touchAntenna();
    }
    if (s.forcedPages != seenForced) {
        // The machine code switched the panel (control/result page). Follow it and
        // drop whatever editor/dialog was on screen; the draft is kept.
        seenForced = s.forcedPages;
        page = s.forcedPage;
        pendingPage = 0;
        edit.active = false;
        dialog.action = NO_ACTION;
    }
    const hist::Log &log = host.history();
    if (log.revision() != seenHistory) {
        seenHistory = log.revision();
        unsigned last = log.size() ? (log.size() - 1) / HISTORY_ROWS : 0;
        if (historyPage > last)
            historyPage = last;
        touchHistory();
        hist::Record newest;
        if (log.get(0, newest) && newest.mode == hist::MODE_SEMI)
            toast("Finalized EPC count saved to History.");
    }
    if (toastOn && uint32_t(now - toastAt) >= TOAST_MS) {
        toastOn = false;
        toastText[0] = 0;
        touch(HMIL_ITEM_toast);
        touch(HMIL_ITEM_toast_show);
        // The text widget is independent of the toast background icon. Clear
        // its VP too, then reload the SAME page after all dirty fields are sent.
        pendingPage = page;
    }
    if (watchApply) {
        uint8_t state = host.applyState();
        if (state == APPLY_DENIED || state == APPLY_FAILED) {
            watchApply = false;
            toast(state == APPLY_DENIED ? BUSY : "The settings could not be saved.");
        } else if (state == APPLY_IDLE)
            watchApply = false;
    }
    // Slow background refresh restores a panel that was power-cycled on its own.
    if (idle() && uint32_t(now - refreshAt) >= REFRESH_MS) {
        refreshAt = now;
        touch(refreshItem);
        refreshItem = (refreshItem + 1) % HMIL_ITEM_COUNT;
    }
}

void Ui::event(uint16_t code) {
    if (!started)
        return;
    const uint8_t high = uint8_t(code >> 8), low = uint8_t(code);
    const Kind kind = edit.active ? describe(edit.field).kind : K_NONE;
    if (high == 1)
        navigate(low);
    else if (high == 2)
        fieldTapped(low);
    else if ((code & 0xF000) == 0x2000)
        segment(uint8_t((code >> 4) & 0xFF), uint8_t(code & 15));
    else if (high == 9) {
        if (low < 8 && antennaEditable()) {
            uint8_t &power = draftAntenna.power[low >> 1];
            if ((low & 1) && power < 30) ++power;
            else if (!(low & 1) && power > 0) --power;
            touchAntenna();
        }
    } else if (high == 4) {
        if (edit.active)
            key(char(low));
    } else if (code == HMIL_EV_EDITOR_CLEAR) {
        if (edit.active && kind != K_OPTIONS) {
            edit.value[0] = 0;
            touch(HMIL_ITEM_edit_entry);
        }
    } else if (code == HMIL_EV_EDITOR_BACKSPACE) {
        size_t n = strlen(edit.value);
        if (edit.active && kind != K_OPTIONS && n) {
            edit.value[n - 1] = 0;
            touch(HMIL_ITEM_edit_entry);
        }
    } else if (code == HMIL_EV_EDITOR_DONE) {
        if (edit.active)
            closeEditor(true);
    } else if (code == HMIL_EV_EDITOR_CANCEL) {
        if (edit.active)
            closeEditor(false);
    } else if (code == HMIL_EV_EDITOR_OPTION_PREV || code == HMIL_EV_EDITOR_OPTION_NEXT) {
        if (edit.active && kind == K_OPTIONS) {
            unsigned pages = (describe(edit.field).optionCount + OPTION_SLOTS - 1) / OPTION_SLOTS;
            if (code == HMIL_EV_EDITOR_OPTION_NEXT && edit.optionPage + 1u < pages)
                ++edit.optionPage;
            else if (code == HMIL_EV_EDITOR_OPTION_PREV && edit.optionPage)
                --edit.optionPage;
            touchEditor();
        }
    } else if (code == HMIL_EV_PASSWORD_EDITOR_SHOW_HIDE) {
        if (edit.active && kind == K_PASSWORD) {
            edit.reveal = !edit.reveal;
            touch(HMIL_ITEM_edit_pw);
            touch(HMIL_ITEM_edit_entry);
        }
    } else if (high == 6) {
        if (edit.active && kind == K_OPTIONS && low >= 1) {
            const Field f = describe(edit.field);
            unsigned index = edit.optionPage * OPTION_SLOTS + low - 1u;
            if (low <= OPTION_SLOTS && index < f.optionCount) {
                snprintf(edit.value, sizeof(edit.value), "%s", f.options[index]);
                closeEditor(true);
            }
        }
    } else if (code == HMIL_EV_APPLY_CHANGES_PRESSED)
        apply();
    else if (code == HMIL_EV_DIALOG_CONFIRM_BUTTON || code == HMIL_EV_DIALOG_CANCEL) {
        if (dialog.action != NO_ACTION)
            confirm(code == HMIL_EV_DIALOG_CONFIRM_BUTTON);
    } else if (code == HMIL_EV_READER_MEASURE_RETURN_LOSS ||
               code == HMIL_EV_READER_READ_INFORMATION)
        toast(NOT_AVAILABLE);
    else if (code == HMIL_EV_HISTORY_PREV_PAGE)
        historyStep(-1);
    else if (code == HMIL_EV_HISTORY_NEXT_PAGE)
        historyStep(1);
    else if (high == 8) {
        if (low >= 1 && low <= 5 && !locked()) {
            draftSpeed = low;
            touch(HMIL_ITEM_speed);
            touch(HMIL_ITEM_apply_en);
        }
    }
}

void Ui::navigate(uint8_t target) {
    if (target < 1 || target > HMIL_PAGE_LAST)
        return;
    const uint16_t previous = page;
    page = target; // the panel has already switched by itself
    pendingPage = 0;
    edit.active = false;
    dialog.action = NO_ACTION;
    const bool sameReader = section(previous) == 5 && section(target) == 5;
    if (unapplied() && section(previous) && !sameReader) {
        ask("Discard changes?", "Your changes have not been applied.", 1, DISCARD, previous);
        dialog.target = target;
    } else if (target == HMIL_PAGE_history)
        touchHistory();
    else if (target == HMIL_PAGE_antennas)
        loadAntenna();
}
void Ui::fieldTapped(uint8_t field) {
    if (field < 1 || field > HMIL_FIELD_COUNT)
        return;
    if (phaseTwo(field)) {
        toast(NOT_AVAILABLE);
        return;
    }
    if (field >= HMIL_FIELD_power1 && field <= HMIL_FIELD_power4 && !antennaEditable()) return;
    const Field f = describe(field);
    if (f.kind == K_NONE || locked())
        return;
    if (f.kind == K_IP && draft.dhcp) {
        toast("Select Static IP to edit this address.");
        return;
    }
    openEditor(field);
}
void Ui::segment(uint8_t field, uint8_t option) {
    if (field < 1 || field > HMIL_FIELD_COUNT || option < 1 || option > HMIL_FIELDS[field].options)
        return;
    if (phaseTwo(field)) {
        toast(NOT_AVAILABLE);
        return;
    }
    if (field >= HMIL_FIELD_ant1 && field <= HMIL_FIELD_save_reader) {
        if (!antennaEditable()) return;
        if (field <= HMIL_FIELD_ant4) {
            const uint8_t bit = uint8_t(1U << (field - HMIL_FIELD_ant1));
            if (option == 1) draftAntenna.mask |= bit;
            else draftAntenna.mask &= uint8_t(~bit);
        } else if (field == HMIL_FIELD_save_reader) antennaSave = option == 1;
        touchAntenna();
        return;
    }
    if (locked())
        return;
    switch (field) {
    case HMIL_FIELD_dhcp:
        draft.dhcp = option == 1;
        touch(HMIL_ITEM_ip);
        touch(HMIL_ITEM_mask);
        touch(HMIL_ITEM_gateway);
        touch(HMIL_ITEM_dns);
        break;
    case HMIL_FIELD_parity:
        draft.parity = uint8_t(option - 1); // None, Even, Odd = 0, 1, 2
        break;
    case HMIL_FIELD_stop_bits:
        draft.stopBits = option;
        break;
    case HMIL_FIELD_mqtt_enabled:
        draft.mqttEnabled = option == 1;
        break;
    default:
        return;
    }
    touch(HMIL_FIELDS[field].sel_item);
    touch(HMIL_ITEM_apply_en);
}
void Ui::key(char c) {
    const Field f = describe(edit.field);
    const size_t n = strlen(edit.value);
    const size_t limit = f.kind == K_IP ? 15 : f.kind == K_NUMBER ? 12 : f.high;
    if (f.kind == K_OPTIONS || n >= limit || n + 1 >= sizeof(edit.value) || c < 32 || c > 126)
        return;
    const bool digit = c >= '0' && c <= '9';
    if ((f.kind == K_NUMBER && !digit) || (f.kind == K_IP && !digit && c != '.'))
        return;
    edit.value[n] = c;
    edit.value[n + 1] = 0;
    touch(HMIL_ITEM_edit_entry);
}
void Ui::fieldText(uint8_t field, char *out, size_t capacity) const {
    if (field >= HMIL_FIELD_power1 && field <= HMIL_FIELD_power4) {
        if (antennaValid) snprintf(out, capacity, "%u", draftAntenna.power[field - HMIL_FIELD_power1]);
        else snprintf(out, capacity, "--");
        return;
    }
    switch (field) {
    case HMIL_FIELD_ip:
        dotted(draft.ip, out, capacity);
        break;
    case HMIL_FIELD_mask:
        dotted(draft.mask, out, capacity);
        break;
    case HMIL_FIELD_gateway:
        dotted(draft.gateway, out, capacity);
        break;
    case HMIL_FIELD_dns:
        dotted(draft.dns, out, capacity);
        break;
    case HMIL_FIELD_unit:
        snprintf(out, capacity, "%u", draft.unit);
        break;
    case HMIL_FIELD_tcp_port:
        snprintf(out, capacity, "%u", draft.tcpPort);
        break;
    case HMIL_FIELD_baud:
        snprintf(out, capacity, "%lu", (unsigned long)draft.baud);
        break;
    case HMIL_FIELD_mqtt_port:
        snprintf(out, capacity, "%u", draft.mqttPort);
        break;
    case HMIL_FIELD_mqtt_host:
        snprintf(out, capacity, "%s", draft.host);
        break;
    case HMIL_FIELD_mqtt_user:
        snprintf(out, capacity, "%s", draft.user);
        break;
    case HMIL_FIELD_mqtt_password:
        snprintf(out, capacity, "%s", draft.password);
        break;
    case HMIL_FIELD_mqtt_root:
        snprintf(out, capacity, "%s", draft.root);
        break;
    default:
        snprintf(out, capacity, "--");
        break;
    }
}
void Ui::openEditor(uint8_t field) {
    const Field f = describe(field);
    edit.active = true;
    edit.reveal = false;
    edit.field = field;
    edit.back = page;
    edit.optionPage = 0;
    edit.error[0] = 0;
    fieldText(field, edit.value, sizeof(edit.value));
    for (unsigned i = 0; i < f.optionCount; i++)
        if (!strcmp(f.options[i], edit.value))
            edit.optionPage = uint8_t(i / OPTION_SLOTS);
    touchEditor();
    show(f.kind == K_OPTIONS  ? HMIL_PAGE_edit_options
         : f.kind == K_IP     ? HMIL_PAGE_edit_ip
         : f.kind == K_NUMBER ? HMIL_PAGE_edit_number
                              : HMIL_PAGE_edit_lower);
}
void Ui::closeEditor(bool commit) {
    if (commit && edit.field >= HMIL_FIELD_power1 && edit.field <= HMIL_FIELD_power4) {
        if (locked() || !antennaValid || host.antennaState().busy) return;
    }
    if (commit) {
        const Field f = describe(edit.field);
        const char *v = edit.value;
        if (f.kind != K_OPTIONS && validateField(edit.field, v, edit.error)) {
            touch(HMIL_ITEM_edit_error);
            return;
        }
        unsigned long number = 0;
        for (const char *p = v; (f.kind == K_NUMBER || f.kind == K_OPTIONS) && *p; ++p)
            number = number * 10 + unsigned(*p - '0');
        switch (edit.field) {
        case HMIL_FIELD_power1:
        case HMIL_FIELD_power2:
        case HMIL_FIELD_power3:
        case HMIL_FIELD_power4:
            draftAntenna.power[edit.field - HMIL_FIELD_power1] = uint8_t(number);
            break;
        case HMIL_FIELD_ip:
            parseAddress(v, draft.ip);
            break;
        case HMIL_FIELD_mask:
            parseAddress(v, draft.mask);
            break;
        case HMIL_FIELD_gateway:
            parseAddress(v, draft.gateway);
            break;
        case HMIL_FIELD_dns:
            parseAddress(v, draft.dns);
            break;
        case HMIL_FIELD_unit:
            draft.unit = uint8_t(number);
            break;
        case HMIL_FIELD_tcp_port:
            draft.tcpPort = uint16_t(number);
            break;
        case HMIL_FIELD_baud:
            draft.baud = uint32_t(number);
            break;
        case HMIL_FIELD_mqtt_port:
            draft.mqttPort = uint16_t(number);
            break;
        case HMIL_FIELD_mqtt_host:
            snprintf(draft.host, sizeof(draft.host), "%s", v);
            break;
        case HMIL_FIELD_mqtt_user:
            snprintf(draft.user, sizeof(draft.user), "%s", v);
            break;
        case HMIL_FIELD_mqtt_password:
            snprintf(draft.password, sizeof(draft.password), "%s", v);
            break;
        case HMIL_FIELD_mqtt_root:
            snprintf(draft.root, sizeof(draft.root), "%s", v);
            break;
        default:
            break;
        }
        touch(HMIL_FIELDS[edit.field].text_item);
        touch(HMIL_ITEM_apply_en);
    }
    edit.active = false;
    memset(edit.value, 0, sizeof(edit.value)); // may hold a password
    touch(HMIL_ITEM_edit_entry);
    show(edit.back);
}
void Ui::ask(const char *title, const char *body, uint16_t button, Action action, uint16_t back) {
    dialog.action = action;
    dialog.back = back;
    dialog.target = back;
    dialog.button = button;
    dialog.title = title;
    dialog.body = body;
    touch(HMIL_ITEM_dlg_title);
    touch(HMIL_ITEM_dlg_body);
    touch(HMIL_ITEM_dlg_btn);
    show(HMIL_PAGE_dialog);
}
void Ui::apply() {
    const uint8_t where = section(page);
    if (where == 4) {
        if (!antennaEditable()) return;
        if (!antenna::valid(draftAntenna)) { toast("Enable at least one antenna."); return; }
        if (!antennaDirty()) { toast("No antenna changes to apply."); return; }
        ask("Apply antenna settings?", antennaSave ? "Save in reader and verify all four ports."
                                                  : "Apply until reader restart, then verify.",
            2, APPLY_ANTENNA, page);
        return;
    }
    if (where == 5) {
        toast(NOT_AVAILABLE);
        return;
    }
    if (where >= 1 && where <= 3) {
        if (sameNetwork(draft, host.settings()) || locked())
            return;
        const char *error = cfg::validate(draft);
        if (error) {
            toast(explain(error));
            return;
        }
        ask("Apply and restart?", "The board will restart and reconnect with these settings.", 3,
            APPLY_NETWORK, page);
    } else if (where == 6) {
        if (draftSpeed == st.speed || locked())
            return;
        ask("Apply changes?", "The new settings will take effect while the machine is idle.", 2,
            APPLY_SPEED, page);
    }
}
void Ui::confirm(bool ok) {
    const uint8_t action = dialog.action;
    dialog.action = NO_ACTION;
    if (!ok) {
        show(dialog.back);
        return;
    }
    if (action == DISCARD) {
        draft = host.settings();
        draftSpeed = st.speed;
        draftAntenna = appliedAntenna;
        antennaSave = appliedAntennaSave;
        touchAntenna();
        touchFields();
        if (dialog.target == HMIL_PAGE_history)
            touchHistory();
        show(dialog.target);
        if (dialog.target == HMIL_PAGE_antennas) loadAntenna();
        return;
    }
    show(dialog.back);
    if (locked()) // the machine may have started while the dialog was open
        return;
    if (action == APPLY_ANTENNA) {
        if (!antennaValid || !antenna::valid(draftAntenna)) {
            toast("Read antenna settings again before applying.");
            return;
        }
        antennaRevision = host.antennaState().revision;
        if (!host.requestAntenna(&draftAntenna, antennaSave)) {
            toast("Reader busy or not ready. Nothing applied.");
            return;
        }
        watchAntenna = true;
        antennaValid = false;
        touchAntenna();
        toast("Applying antenna settings and verifying...");
    } else if (action == APPLY_NETWORK) {
        const char *error = cfg::validate(draft);
        const uint8_t result = error ? uint8_t(SUBMIT_INVALID) : host.submitSettings(draft);
        if (result == SUBMIT_OK) {
            watchApply = true;
            toast("Saving. The board will restart.");
        } else
            toast(result == SUBMIT_INVALID       ? explain(error ? error : "")
                  : result == SUBMIT_IN_PROGRESS ? "Another settings change is in progress."
                                                 : "Settings storage is not available.");
    } else if (action == APPLY_SPEED) {
        const uint8_t result = host.setSpeed(draftSpeed);
        toast(result == SPEED_OK     ? "Conveyor speed saved."
              : result == SPEED_BUSY ? "Not saved - machine busy or reader not ready."
                                     : "The speed level could not be saved.");
    }
}
void Ui::historyStep(int direction) {
    const unsigned size = host.history().size();
    const unsigned last = size ? (size - 1) / HISTORY_ROWS : 0;
    if (direction > 0 && historyPage < last)
        ++historyPage;
    else if (direction < 0 && historyPage)
        --historyPage;
    touchHistory();
}

void Ui::liveAddress(const uint8_t *address, bool primary, char *out, size_t capacity) const {
    if (st.link && st.hasIp)
        dotted(address, out, capacity);
    else
        snprintf(out, capacity, "%s", !primary ? "--" : st.link ? "WAIT IP" : "NO LINK");
}
void Ui::render(unsigned item, char *out, size_t capacity) const {
    const cfg::Settings &applied = host.settings();
    out[0] = 0;
    if (item >= HMIL_ITEM_power1 && item <= HMIL_ITEM_power4) {
        fieldText(uint8_t(HMIL_FIELD_power1 + item - HMIL_ITEM_power1), out, capacity);
        return;
    }
    if (item == HMIL_ITEM_sum4) {
        unsigned count = 0;
        for (unsigned bit = 0; bit < 4; ++bit) count += (draftAntenna.mask >> bit) & 1U;
        if (antennaValid) snprintf(out, capacity, "%u of 4 on", count);
        else snprintf(out, capacity, "%s", host.antennaState().busy ? "Reading..." : "--");
        return;
    }
    if (item >= HMIL_ITEM_hist_id1 && item < HMIL_ITEM_hist_id1 + 4 * HISTORY_ROWS) {
        const unsigned column = (item - HMIL_ITEM_hist_id1) / HISTORY_ROWS;
        const unsigned row = (item - HMIL_ITEM_hist_id1) % HISTORY_ROWS;
        hist::Record r;
        if (!host.history().get(historyPage * HISTORY_ROWS + row, r))
            return;
        if (column == 0)
            snprintf(out, capacity, "#%lu", (unsigned long)r.sequence);
        else if (column == 1)
            formatTime(r.time, out, capacity);
        else if (column == 2)
            snprintf(out, capacity, "%s", r.mode == hist::MODE_SEMI ? "Semi Auto" : "Auto");
        else
            snprintf(out, capacity, "%u", r.count);
        return;
    }
    if ((item >= HMIL_ITEM_opt1 && item < HMIL_ITEM_opt1 + OPTION_SLOTS) ||
        (item >= HMIL_ITEM_optsel1 && item < HMIL_ITEM_optsel1 + OPTION_SLOTS)) {
        const bool selectedSlot = item >= HMIL_ITEM_optsel1;
        const unsigned slot = item - (selectedSlot ? HMIL_ITEM_optsel1 : HMIL_ITEM_opt1);
        const Field f = describe(edit.field);
        const unsigned index = edit.optionPage * OPTION_SLOTS + slot;
        if (!edit.active || index >= f.optionCount)
            return;
        // The chosen option is drawn by its optsel text (with the check mark) instead.
        if (!strcmp(f.options[index], edit.value) == selectedSlot)
            snprintf(out, capacity, "%s", f.options[index]);
        return;
    }
    switch (item) {
    case HMIL_ITEM_ip:
        if (draft.dhcp)
            liveAddress(st.ip, true, out, capacity);
        else
            dotted(draft.ip, out, capacity);
        break;
    case HMIL_ITEM_mask:
        if (draft.dhcp)
            liveAddress(st.mask, false, out, capacity);
        else
            dotted(draft.mask, out, capacity);
        break;
    case HMIL_ITEM_gateway:
        if (draft.dhcp)
            liveAddress(st.gateway, false, out, capacity);
        else
            dotted(draft.gateway, out, capacity);
        break;
    case HMIL_ITEM_dns:
        if (draft.dhcp)
            liveAddress(st.dns, false, out, capacity);
        else
            dotted(draft.dns, out, capacity);
        break;
    case HMIL_ITEM_unit:
        fieldText(HMIL_FIELD_unit, out, capacity);
        break;
    case HMIL_ITEM_tcp_port:
        fieldText(HMIL_FIELD_tcp_port, out, capacity);
        break;
    case HMIL_ITEM_baud:
        fieldText(HMIL_FIELD_baud, out, capacity);
        break;
    case HMIL_ITEM_mqtt_port:
        fieldText(HMIL_FIELD_mqtt_port, out, capacity);
        break;
    case HMIL_ITEM_mqtt_host:
        snprintf(out, capacity, "%s", draft.host[0] ? draft.host : "Not set");
        break;
    case HMIL_ITEM_mqtt_user:
        snprintf(out, capacity, "%s", draft.user[0] ? draft.user : "Not set");
        break;
    case HMIL_ITEM_mqtt_password: {
        // Never the password itself: at most 12 stars.
        size_t n = strlen(draft.password);
        snprintf(out, capacity, "%s", n ? "************" + (12 - (n < 12 ? n : 12)) : "Not set");
        break;
    }
    case HMIL_ITEM_mqtt_root:
        snprintf(out, capacity, "%s", draft.root[0] ? draft.root : "Not set");
        break;
    case HMIL_ITEM_sum1:
        if (applied.dhcp)
            snprintf(out, capacity, "DHCP");
        else
            snprintf(out, capacity, "Static - %u.%u.%u.%u", applied.ip[0], applied.ip[1],
                     applied.ip[2], applied.ip[3]);
        break;
    case HMIL_ITEM_sum2:
        snprintf(out, capacity, "Unit %u - %lu", applied.unit, (unsigned long)applied.baud);
        break;
    case HMIL_ITEM_sum3:
        snprintf(out, capacity, "%s",
                 !applied.mqttEnabled ? "Disabled"
                 : applied.host[0]    ? applied.host
                                      : "Not set");
        break;
    case HMIL_ITEM_sum6:
        snprintf(out, capacity, "Speed level %u", st.speed);
        break;
    case HMIL_ITEM_hist_saved:
        snprintf(out, capacity, "%u / %u saved", host.history().size(), hist::CAPACITY);
        break;
    case HMIL_ITEM_hist_range: {
        const unsigned size = host.history().size(), first = historyPage * HISTORY_ROWS;
        if (first < size)
            snprintf(out, capacity, "%u-%u of %u", first + 1,
                     first + HISTORY_ROWS < size ? first + HISTORY_ROWS : size, size);
        break;
    }
    case HMIL_ITEM_toast:
        snprintf(out, capacity, "%s", toastText);
        break;
    case HMIL_ITEM_edit_title:
        snprintf(out, capacity, "%s", describe(edit.field).title);
        break;
    case HMIL_ITEM_edit_hint: {
        const Field f = describe(edit.field);
        if (f.kind == K_IP)
            snprintf(out, capacity, "Four numbers from 0 to 255");
        else if (f.kind == K_NUMBER)
            snprintf(out, capacity, "Range %lu - %lu", (unsigned long)f.low, (unsigned long)f.high);
        break;
    }
    case HMIL_ITEM_edit_entry: {
        if (!edit.active)
            break;
        const Kind kind = describe(edit.field).kind;
        const size_t n = strlen(edit.value);
        if (n >= capacity)
            break;
        if (kind == K_PASSWORD && !edit.reveal) {
            memset(out, '*', n);
            out[n] = 0;
        } else
            memcpy(out, edit.value, n + 1);
        // Keep the tail visible while typing.
        const unsigned room =
            kind == K_TEXT || kind == K_PASSWORD ? ENTRY_ROOM_TEXT : ENTRY_ROOM_PAD;
        size_t skip = n > MAX_TEXT_BYTES - 2 ? n - (MAX_TEXT_BYTES - 2) : 0;
        while (out[skip] && textWidth(entryFace(kind), out + skip) > room)
            ++skip;
        memmove(out, out + skip, n - skip + 1);
        break;
    }
    case HMIL_ITEM_edit_error:
        snprintf(out, capacity, "%s", edit.error);
        break;
    case HMIL_ITEM_opt_page: {
        const unsigned pages = (describe(edit.field).optionCount + OPTION_SLOTS - 1) / OPTION_SLOTS;
        if (edit.active && pages > 1)
            snprintf(out, capacity, "%u / %u", edit.optionPage + 1u, pages);
        break;
    }
    case HMIL_ITEM_dlg_title:
        snprintf(out, capacity, "%s", dialog.title ? dialog.title : "");
        break;
    case HMIL_ITEM_dlg_body:
        snprintf(out, capacity, "%s", dialog.body ? dialog.body : "");
        break;
    default:
        // Antenna/reader values and their summaries: phase 2, not read from the reader yet.
        snprintf(out, capacity, "--");
        break;
    }
}
uint16_t Ui::word(unsigned item) const {
    switch (item) {
    case HMIL_ITEM_sel_dhcp:
        return draft.dhcp ? 1 : 2;
    case HMIL_ITEM_sel_parity:
        return uint16_t(draft.parity + 1);
    case HMIL_ITEM_sel_stop_bits:
        return draft.stopBits;
    case HMIL_ITEM_sel_mqtt_enabled:
        return draft.mqttEnabled ? 1 : 2;
    case HMIL_ITEM_speed:
        return draftSpeed;
    case HMIL_ITEM_hist_rows: {
        const unsigned size = host.history().size(), first = historyPage * HISTORY_ROWS;
        return uint16_t(first >= size                 ? 8
                        : size - first < HISTORY_ROWS ? size - first
                                                      : HISTORY_ROWS);
    }
    case HMIL_ITEM_state:
        return st.pill;
    case HMIL_ITEM_lock:
        return settingsLock();
    case HMIL_ITEM_toast_show:
        return toastOn;
    case HMIL_ITEM_opt_sel: {
        const Field f = describe(edit.field);
        for (unsigned i = 0; edit.active && i < OPTION_SLOTS; i++) {
            const unsigned index = edit.optionPage * OPTION_SLOTS + i;
            if (index < f.optionCount && !strcmp(f.options[index], edit.value))
                return uint16_t(i + 1);
        }
        return 0;
    }
    case HMIL_ITEM_sel_ant1:
    case HMIL_ITEM_sel_ant2:
    case HMIL_ITEM_sel_ant3:
    case HMIL_ITEM_sel_ant4:
        return antennaValid ? ((draftAntenna.mask & (1U << (item - HMIL_ITEM_sel_ant1))) ? 1 : 2) : 0;
    case HMIL_ITEM_sel_save_reader:
        return antennaSave ? 1 : 2;
    case HMIL_ITEM_apply_en:
        return unapplied();
    case HMIL_ITEM_opt_pager:
        return edit.active && describe(edit.field).optionCount > OPTION_SLOTS;
    case HMIL_ITEM_dlg_btn:
        return dialog.button;
    case HMIL_ITEM_crumb:
        return section(edit.back) ? section(edit.back) : 1;
    case HMIL_ITEM_edit_pw:
        return edit.active && describe(edit.field).kind == K_PASSWORD ? (edit.reveal ? 2 : 1) : 0;
    default:
        return 0; // phase 2 segmented controls: nothing selected
    }
}
bool Ui::send(unsigned item) {
    const HmilVar &v = HMIL_ITEMS[item];
    if (!v.text)
        return host.sendWord(v.vp, word(item));
    char text[200];
    render(item, text, sizeof(text) - 4);
    if (v.align != HMIL_ALIGN_LEFT) {
        // Position first, then the string, as the reference does.
        fitText(v.face, text, sizeof(text), unsigned(v.box_right - v.box_left));
        if (!host.sendWord(v.x_register, alignX(item, text)))
            return false;
    }
    // Fields are written whole (zero padded) up to MAX_TEXT_BYTES; the three larger
    // ones get their first MAX_TEXT_BYTES bytes, which always include the FF FF end.
    return host.sendText(v.vp, text, v.bytes > MAX_TEXT_BYTES ? uint16_t(MAX_TEXT_BYTES) : v.bytes);
}
void Ui::flush() {
    if (!started)
        return;
    unsigned frames = 0;
    for (unsigned item = 0; item < HMIL_ITEM_COUNT; item++) {
        if (!(stale[item / 32] & (1u << (item % 32))))
            continue;
        const HmilVar &v = HMIL_ITEMS[item];
        const unsigned cost = v.text && v.align != HMIL_ALIGN_LEFT ? 2 : 1;
        if (frames + cost > FRAMES_PER_FLUSH)
            return;
        if (!send(item))
            return; // transport full: keep it stale, try again next time
        stale[item / 32] &= ~(1u << (item % 32));
        frames += cost;
    }
    if (pendingPage && frames < FRAMES_PER_FLUSH && host.sendPage(pendingPage))
        pendingPage = 0;
}
} // namespace hmiui
