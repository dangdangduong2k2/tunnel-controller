#include "HmiUi.h"
#include "HmiSettingsGate.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
using namespace hmiui;

// Fake panel + machine. Records every accepted frame; 'room' limits how many
// frames the UART accepts before it reports full.
struct Frame {
    char kind; // 'T' text, 'W' word, 'P' page
    uint16_t vp, value;
    std::string text;
};
struct Fake : Host {
    std::vector<Frame> frames;
    std::map<uint16_t, std::string> texts;
    std::map<uint16_t, uint16_t> words;
    std::vector<uint16_t> pages;
    int room = -1; // -1 = unlimited
    unsigned refused = 0;
    cfg::Settings stored = cfg::defaults();
    cfg::Settings submitted = {};
    unsigned submits = 0, speedCalls = 0;
    uint8_t submitResult = SUBMIT_OK, apply = APPLY_IDLE, speedResult = SPEED_OK, speedSet = 0;
    hist::Log log;
    antenna::State ant;
    antenna::Values antSubmitted;
    bool antAccept=false, antPersist=true;
    unsigned antReads=0, antWrites=0;
    const antenna::State &antennaState() override { return ant; }
    bool requestAntenna(const antenna::Values *v, bool save) override {
        if (!antAccept || ant.busy) return false;
        if (v) { ++antWrites; antSubmitted=*v; antPersist=save; }
        else ++antReads;
        ant.busy=true; ant.valid=false; ant.result=antenna::NONE; return true;
    }
    void antComplete(antenna::Result result, const antenna::Values &values) {
        ant.busy=false; ant.result=result; ant.values=values;
        ant.valid=result==antenna::LOADED || result==antenna::SAVED; ++ant.revision;
    }
    bool take() {
        if (room == 0) {
            ++refused;
            return false;
        }
        if (room > 0)
            --room;
        return true;
    }
    bool sendText(uint16_t vp, const char *text, uint16_t bytes) override {
        CHECK(bytes >= 2 && bytes <= Ui::MAX_TEXT_BYTES && !(bytes & 1));
        if (!take())
            return false;
        std::string s(text);
        if (s.size() > size_t(bytes - 2))
            s.resize(bytes - 2); // what Hmi_SendText puts on the wire
        frames.push_back({'T', vp, 0, s});
        texts[vp] = s;
        return true;
    }
    bool sendWord(uint16_t vp, uint16_t value) override {
        if (!take())
            return false;
        frames.push_back({'W', vp, value, ""});
        words[vp] = value;
        return true;
    }
    bool sendPage(uint16_t page) override {
        if (!take())
            return false;
        frames.push_back({'P', 0x84, page, ""});
        pages.push_back(page);
        return true;
    }
    const cfg::Settings &settings() override {
        return stored;
    }
    uint8_t submitSettings(const cfg::Settings &next) override {
        ++submits;
        submitted = next;
        return submitResult;
    }
    uint8_t applyState() override {
        return apply;
    }
    uint8_t setSpeed(uint8_t level) override {
        ++speedCalls;
        speedSet = level;
        return speedResult;
    }
    const hist::Log &history() override {
        return log;
    }
};
static Fake fake;
static Status status;
static uint32_t now = 1000;
static const std::string &text(unsigned item) {
    return fake.texts[HMIL_ITEMS[item].vp];
}
static uint16_t word(unsigned item) {
    return fake.words[HMIL_ITEMS[item].vp];
}
static uint16_t xOf(unsigned item) {
    return fake.words[HMIL_ITEMS[item].x_register];
}
// Run until everything pending is on the wire (bounded: never spins).
static void settle(Ui &ui) {
    for (unsigned i = 0; i < 400; i++) {
        ui.tick(now, status);
        ui.flush();
        if (ui.idle())
            return;
        ++now;
    }
    CHECK(!"UI did not settle");
}
static void press(Ui &ui, uint16_t code) {
    ui.tick(now, status);
    ui.event(code);
    settle(ui);
}
static void type(Ui &ui, const char *keys) {
    for (; *keys; ++keys)
        press(ui, uint16_t(0x0400 | (unsigned char)*keys));
}
static bool anyFrameHas(size_t from, const char *needle) {
    for (size_t i = from; i < fake.frames.size(); i++)
        if (fake.frames[i].text.find(needle) != std::string::npos)
            return true;
    return false;
}
static void checkPure() {
    CHECK(pill({false, false, false, false}) == READY && lockNote(READY) == LOCK_NONE);
    CHECK(pill({false, false, false, true}) == RUNNING && lockNote(RUNNING) == LOCK_WAIT_IDLE);
    CHECK(pill({false, false, true, true}) == VERIFY && lockNote(VERIFY) == LOCK_WAIT_IDLE);
    CHECK(pill({false, true, true, true}) == REMOTE && lockNote(REMOTE) == LOCK_REMOTE);
    CHECK(pill({true, true, true, true}) == STOPPED && lockNote(STOPPED) == LOCK_WAIT_IDLE);
    // Widths, fit and X positions: values produced by the panel package's hmi_text.py.
    CHECK(textWidth(HMIL_FACE_SB21, "192.168.1.50") == 99);
    CHECK(alignX(HMIL_ITEM_ip, "192.168.1.50") == 867);
    CHECK(alignX(HMIL_ITEM_unit, "11") == 954);
    CHECK(alignX(HMIL_ITEM_toast, "BUSY - wait until the machine is idle.") == 397);
    CHECK(alignX(HMIL_ITEM_hist_count1, "128") == 943);
    CHECK(alignX(HMIL_ITEM_opt1 + 5, "38400") == 173);
    CHECK(alignX(HMIL_ITEM_edit_title, "Device / Unit ID") == 482);
    CHECK(alignX(HMIL_ITEM_hist_saved, "3 / 500 saved") == 868);
    CHECK(alignX(HMIL_ITEM_hist_range, "1-7 of 500") == 706);
    CHECK(alignX(HMIL_ITEM_power1, "--") == 850);
    CHECK(textWidth(HMIL_FACE_B76, "11") == 44); // big digits
    CHECK(textWidth(HMIL_FACE_B76, "a") == 48);  // glyph missing in that face = space
    CHECK(textWidth(HMIL_FACE_B41, "192.168.100.200") == 260);
    char longHost[200] = "a-very-long-broker-hostname.subdomain.example-company.internal";
    fitText(HMIL_FACE_SB21, longHost, sizeof(longHost), 966 - 568);
    CHECK(!strcmp(longHost, "a-very-long-broker-hostname.subdoma..."));
    CHECK(textWidth(HMIL_FACE_SB21, longHost) == 394 &&
          alignX(HMIL_ITEM_mqtt_host, longHost) == 572);
    char tiny[8] = "WWWWWWW"; // wider than the box even as "...": never left of the box
    fitText(HMIL_FACE_SB21, tiny, sizeof(tiny), 4);
    CHECK(!strcmp(tiny, "...") && alignX(HMIL_ITEM_ip, tiny) == 966 - 15);
    char huge[200];
    memset(huge, 'W', 199);
    huge[199] = 0;
    CHECK(alignX(HMIL_ITEM_ip, huge) == HMIL_ITEMS[HMIL_ITEM_ip].box_left);
    char t[32];
    formatTime(0, t, sizeof(t));
    CHECK(!strcmp(t, "--/--/---- --:--:--"));
    formatTime(1762850096, t, sizeof(t));
    CHECK(!strcmp(t, "11/11/2025 15:34:56"));
    formatTime(1704067199, t, sizeof(t));
    CHECK(!strcmp(t, "01/01/2024 06:59:59"));
    formatTime(951782400, t, sizeof(t));
    CHECK(!strcmp(t, "29/02/2000 07:00:00"));
    // Field rules of the reference validate().
    char m[96];
    CHECK(!validateField(HMIL_FIELD_unit, "247", m) && !validateField(HMIL_FIELD_unit, "007", m));
    CHECK(!strcmp(validateField(HMIL_FIELD_unit, "248", m), "Enter a whole number from 1 to 247."));
    CHECK(validateField(HMIL_FIELD_unit, "0", m) && validateField(HMIL_FIELD_unit, "", m));
    CHECK(validateField(HMIL_FIELD_unit, "999999999999", m));
    CHECK(!strcmp(validateField(HMIL_FIELD_tcp_port, "80", m),
                  "Port 80 is reserved for the web page."));
    CHECK(!validateField(HMIL_FIELD_tcp_port, "65535", m));
    CHECK(validateField(HMIL_FIELD_tcp_port, "65536", m) &&
          !validateField(HMIL_FIELD_mqtt_port, "80", m));
    CHECK(!validateField(HMIL_FIELD_ip, "10.0.0.255", m) &&
          !validateField(HMIL_FIELD_dns, "0.0.0.0", m));
    const char *bad[] = {"1.2.3",     "1.2.3.256",  "1..2.3", "1.2.3.4.",
                         "1.2.3.4.5", "1234.1.1.1", ".1.2.3", ""};
    for (const char *b : bad)
        CHECK(!strcmp(validateField(HMIL_FIELD_ip, b, m),
                      "Enter four numbers from 0 to 255, separated by dots."));
    CHECK(!validateField(HMIL_FIELD_mask, "255.255.254.0", m));
    CHECK(!strcmp(validateField(HMIL_FIELD_mask, "255.0.255.0", m),
                  "Enter a contiguous subnet mask."));
    CHECK(validateField(HMIL_FIELD_mask, "0.0.0.0", m));
    CHECK(!validateField(HMIL_FIELD_mqtt_host, "broker.example.com", m));
    CHECK(!validateField(HMIL_FIELD_mqtt_host, "10.1.2.3", m));
    const char *hosts[] = {"",   "a b", "mqtt://x", "host:1883", "host/path",
                           "-x", "x.",  "a..b",     "h_st"};
    for (const char *h : hosts)
        CHECK(validateField(HMIL_FIELD_mqtt_host, h, m));
    CHECK(!validateField(HMIL_FIELD_mqtt_root, "tunnel/board-01/v1", m));
    const char *roots[] = {"", "a b", "a/+", "a/#", "/a", "a/", "a//b"};
    for (const char *r : roots)
        CHECK(validateField(HMIL_FIELD_mqtt_root, r, m));
    CHECK(!validateField(HMIL_FIELD_mqtt_user, "", m) &&
          !validateField(HMIL_FIELD_mqtt_password, "p w+#/", m));
    std::string big(65, 'u');
    CHECK(!strcmp(validateField(HMIL_FIELD_mqtt_user, big.c_str(), m),
                  "Use up to 64 ASCII characters."));
}
static void checkStoppedSettings() {
    // Every state stays fenced during actual activity/FOTA or a remote session.
    for (unsigned n = 0; n <= TUNNEL_WAIT_INVENTORY_START; ++n) {
        auto state = static_cast<TunnelState>(n);
        CHECK(settingsGate(state, true, false) == LOCK_WAIT_IDLE);
        CHECK(settingsGate(state, false, true) == LOCK_REMOTE);
        CHECK(settingsGate(state, true, true) == LOCK_REMOTE);
        const bool quiet = state == TUNNEL_STARTUP_WAIT || state == TUNNEL_STOPPED ||
            state == TUNNEL_MANUAL || state == TUNNEL_WAIT_SS1_LOW ||
            state == TUNNEL_WAIT_REARM || state == TUNNEL_FAULT;
        CHECK(settingsGate(state, false, false) == (quiet ? LOCK_NONE : LOCK_WAIT_IDLE));
    }
    CHECK(settingsGate(static_cast<TunnelState>(255), false, false) == LOCK_WAIT_IDLE);
    Fake host;
    Ui view(host);
    Status stopped;
    stopped.pill = STOPPED;
    stopped.settingsLock = settingsGate(TUNNEL_STOPPED, false, false);
    uint32_t clock = 1;
    auto click = [&](uint16_t event) {
        view.tick(++clock, stopped);
        view.event(event);
        for (unsigned i = 0; i < 400 && !view.idle(); ++i) {
            view.tick(++clock, stopped);
            view.flush();
        }
        CHECK(view.idle());
    };
    click(0x0104); // Modbus
    click(0x0206); // Unit editor
    CHECK(view.editing());
    click(HMIL_EV_EDITOR_CLEAR);
    click(0x0432); // '2'
    click(HMIL_EV_EDITOR_DONE);
    CHECK(view.draftSettings().unit == 2);
    CHECK(host.words[HMIL_VP_STATE] == STOPPED); // Never pretend the safety latch was cleared.
    CHECK(host.words[HMIL_VP_LOCK] == LOCK_NONE);
    click(HMIL_EV_APPLY_CHANGES_PRESSED);
    CHECK(view.dialogOpen());
    // Activity starts with the same STOPPED badge: permission must still refresh.
    stopped.settingsLock = settingsGate(TUNNEL_STOPPED, true, false);
    click(HMIL_EV_DIALOG_CONFIRM_BUTTON);
    CHECK(host.submits == 0 && !view.dialogOpen());
    CHECK(host.words[HMIL_VP_LOCK] == LOCK_WAIT_IDLE);
    stopped.settingsLock = settingsGate(TUNNEL_STOPPED, false, false);
    click(HMIL_EV_APPLY_CHANGES_PRESSED);
    CHECK(view.dialogOpen());
    stopped.settingsLock = settingsGate(TUNNEL_STOPPED, false, true);
    click(HMIL_EV_DIALOG_CONFIRM_BUTTON);
    CHECK(host.submits == 0 && host.words[HMIL_VP_LOCK] == LOCK_REMOTE);
    stopped.settingsLock = settingsGate(TUNNEL_STOPPED, false, false);
    click(HMIL_EV_APPLY_CHANGES_PRESSED);
    click(HMIL_EV_DIALOG_CONFIRM_BUTTON);
    CHECK(host.submits == 1 && host.submitted.unit == 2 && host.speedCalls == 0);
    CHECK(host.words[HMIL_VP_STATE] == STOPPED);
}

static void checkAntennaUi() {
    Fake host; host.antAccept=true;
    Ui view(host); Status s; s.pill=READY; s.settingsLock=LOCK_NONE;
    uint32_t clock=1;
    auto settleLocal=[&]() {
        for (unsigned i=0;i<400;++i) { view.tick(++clock,s); view.flush(); if (view.idle()) return; }
        CHECK(!"antenna UI did not settle");
    };
    auto click=[&](uint16_t event) { view.tick(++clock,s); view.event(event); settleLocal(); };
    auto value=[&](unsigned item) { return host.texts[HMIL_ITEMS[item].vp]; };
    auto selector=[&](unsigned item) { return host.words[HMIL_ITEMS[item].vp]; };
    const antenna::Values original={{0,10,22,30},5};
    click(0x0106);
    CHECK(host.antReads==1 && host.ant.busy && value(HMIL_ITEM_power1)=="--");
    click(0x0215); CHECK(!view.editing()); // wait for actual reader values
    host.antComplete(antenna::LOADED,original); settleLocal();
    CHECK(value(HMIL_ITEM_power1)=="0" && value(HMIL_ITEM_power2)=="10");
    CHECK(selector(HMIL_ITEM_sel_ant1)==1 && selector(HMIL_ITEM_sel_ant2)==2);
    CHECK(value(HMIL_ITEM_sum4)=="2 of 4 on" && !view.unapplied());
    click(0x0900); click(0x0907); // lower/upper clamped
    CHECK(!view.unapplied());
    click(0x0901); CHECK(value(HMIL_ITEM_power1)=="1" && view.unapplied());
    click(0x0216); CHECK(view.editing());
    click(HMIL_EV_EDITOR_CLEAR); click(0x0433); click(0x0431); //31 rejected
    click(HMIL_EV_EDITOR_DONE); CHECK(view.editing());
    click(HMIL_EV_EDITOR_BACKSPACE); click(0x0430);
    click(HMIL_EV_EDITOR_DONE); CHECK(!view.editing() && value(HMIL_ITEM_power2)=="30");
    click(0x2112); click(0x2132); // disable both active ports
    click(HMIL_EV_APPLY_CHANGES_PRESSED);
    CHECK(!view.dialogOpen() && value(HMIL_ITEM_toast)=="Enable at least one antenna.");
    click(0x2121); click(0x2192); // ANT2 only; RAM
    click(HMIL_EV_APPLY_CHANGES_PRESSED); CHECK(view.dialogOpen());
    click(HMIL_EV_DIALOG_CANCEL); CHECK(!host.antWrites);
    click(HMIL_EV_APPLY_CHANGES_PRESSED);
    s.settingsLock=LOCK_REMOTE;
    click(HMIL_EV_DIALOG_CONFIRM_BUTTON); CHECK(!host.antWrites && !view.dialogOpen());
    s.settingsLock=LOCK_NONE;
    click(HMIL_EV_APPLY_CHANGES_PRESSED); click(HMIL_EV_DIALOG_CONFIRM_BUTTON);
    CHECK(host.antWrites==1 && !host.antPersist && host.antSubmitted.mask==2);
    CHECK(host.antSubmitted.power[0]==1 && host.antSubmitted.power[1]==30);
    CHECK(value(HMIL_ITEM_power1)=="--");
    click(0x0901); CHECK(host.antWrites==1); // no editing while write pending
    host.antComplete(antenna::SAVED,host.antSubmitted); settleLocal();
    CHECK(!view.unapplied() && value(HMIL_ITEM_power1)=="1");
    CHECK(value(HMIL_ITEM_toast)=="Antenna settings applied and verified.");
    click(0x2191); // persistent policy alone is an explicit write
    click(HMIL_EV_APPLY_CHANGES_PRESSED); click(HMIL_EV_DIALOG_CONFIRM_BUTTON);
    CHECK(host.antWrites==2 && host.antPersist);
    host.antComplete(antenna::MISMATCH,original); settleLocal();
    CHECK(value(HMIL_ITEM_power1)=="--" && value(HMIL_ITEM_toast).find("Readback differs")!=std::string::npos);
    click(0x0901); CHECK(!view.unapplied()); // unknown readback cannot become a new write
    click(0x0102); click(0x0106);
    CHECK(host.antReads==2); host.antComplete(antenna::LOADED,original); settleLocal();
    click(0x0901); click(0x0102); CHECK(view.dialogOpen());
    click(HMIL_EV_DIALOG_CONFIRM_BUTTON); CHECK(!view.unapplied() && host.antWrites==2);
    click(0x0106); CHECK(host.antReads==3);
    host.antComplete(antenna::TIMEOUT,original); settleLocal();
    CHECK(value(HMIL_ITEM_toast)=="Reader timeout. Reopen Antennas to check.");
    CHECK(!host.submits && !host.speedCalls); // no network restart or motor command
}
static void checkToastRepaint() {
    Fake host; Ui view(host); Status s; s.pill=READY; s.settingsLock=LOCK_NONE;
    uint32_t clock=1;
    auto drain=[&]() {
        for(unsigned i=0;i<400;++i) { view.tick(++clock,s); view.flush(); if(view.idle()) return; }
        CHECK(!"toast redraw did not finish");
    };
    auto click=[&](uint16_t event) { view.tick(++clock,s); view.event(event); drain(); };
    click(0x0104); // Modbus page
    view.notice("A long notification that must disappear completely."); drain();
    const size_t before=host.frames.size();
    host.room=0;
    view.notice("Saved."); view.flush();
    CHECK(host.frames.size()==before && !view.idle()); // retain dirty text/page on backpressure
    host.room=-1; drain();
    CHECK(host.texts[HMIL_ITEMS[HMIL_ITEM_toast].vp]=="Saved.");
    CHECK(host.frames.back().kind=='P' && host.pages.back()==HMIL_PAGE_modbus);
    click(0x0206); click(HMIL_EV_EDITOR_CLEAR); click(0x0432);
    CHECK(view.editing());
    clock+=3600; drain();
    CHECK(view.editing() && host.pages.back()==HMIL_PAGE_edit_number);
    CHECK(host.texts[HMIL_ITEMS[HMIL_ITEM_edit_entry].vp]=="2");
    CHECK(host.texts[HMIL_ITEMS[HMIL_ITEM_toast].vp].empty());
    CHECK(host.words[HMIL_ITEMS[HMIL_ITEM_toast_show].vp]==0);
    CHECK(host.frames.back().kind=='P' && !host.submits && !host.speedCalls);
}
int main() {
    checkToastRepaint();
    checkAntennaUi();
    checkStoppedSettings();
    checkPure();
    cfg::Settings &s = fake.stored;
    strcpy(s.host, "broker.example.com");
    s.mqttEnabled = 1;
    s.adminSet = 1;
    memset(s.adminSalt, 0x5a, sizeof(s.adminSalt));
    memset(s.adminHash, 0xa5, sizeof(s.adminHash));
    CHECK(!cfg::validate(s));
    status.pill = READY;
    status.link = status.hasIp = true;
    status.speed = 3;
    const uint8_t ip[] = {192, 168, 1, 50}, mask[] = {255, 255, 255, 0}, gw[] = {192, 168, 1, 1},
                  dns[] = {1, 1, 1, 1};
    memcpy(status.ip, ip, 4);
    memcpy(status.mask, mask, 4);
    memcpy(status.gateway, gw, 4);
    memcpy(status.dns, dns, 4);
    Ui ui(fake);

    // --- Transport full: nothing is lost, nothing spins, at most 6 frames a flush.
    fake.room = 0;
    ui.tick(now, status);
    ui.flush();
    CHECK(fake.frames.empty() && fake.refused == 1 && !ui.idle());
    fake.room = 1; // an aligned text needs X + string: X goes out, the string is refused
    ui.flush();
    CHECK(fake.frames.size() == 1 && fake.frames[0].kind == 'W' && fake.frames[0].vp == 0x4011);
    fake.room = -1;
    ui.flush(); // both are sent again, in order
    CHECK(fake.frames.size() == 7);
    CHECK(fake.frames[1].kind == 'W' && fake.frames[1].vp == 0x4011 && fake.frames[1].value == 867);
    CHECK(fake.frames[2].kind == 'T' && fake.frames[2].vp == 0x2100 &&
          fake.frames[2].text == "192.168.1.50");
    settle(ui);
    // Every published variable was written exactly once despite the refusals.
    std::map<uint16_t, unsigned> seen;
    for (size_t i = 1; i < fake.frames.size(); i++)
        if (fake.frames[i].kind == 'T' || fake.frames[i].vp < 0x4000)
            ++seen[fake.frames[i].vp];
    for (unsigned i = 0; i < HMIL_ITEM_COUNT; i++)
        CHECK(seen[HMIL_ITEMS[i].vp] == 1);
    CHECK(fake.pages.empty());         // the UI never changes the page on its own
    for (const Frame &f : fake.frames) // machine words and scratch variables are never written
        CHECK(f.vp != 0x2000 && (f.vp < 0x3001 || f.vp > 0x3005) && f.vp != 0x2F02 &&
              f.vp != 0x2FF0);

    // --- Real data on first publish.
    CHECK(text(HMIL_ITEM_ip) == "192.168.1.50" && xOf(HMIL_ITEM_ip) == 867);
    CHECK(text(HMIL_ITEM_mask) == "255.255.255.0" && text(HMIL_ITEM_gateway) == "192.168.1.1");
    CHECK(text(HMIL_ITEM_dns) == "1.1.1.1" && text(HMIL_ITEM_unit) == "1" &&
          text(HMIL_ITEM_tcp_port) == "502");
    CHECK(text(HMIL_ITEM_baud) == "19200" && text(HMIL_ITEM_mqtt_port) == "1883");
    CHECK(text(HMIL_ITEM_mqtt_host) == "broker.example.com" &&
          text(HMIL_ITEM_mqtt_user) == "Not set");
    CHECK(text(HMIL_ITEM_mqtt_password) == "Not set" &&
          text(HMIL_ITEM_mqtt_root) == "tunnel/01/v1");
    CHECK(word(HMIL_ITEM_sel_dhcp) == 1 && word(HMIL_ITEM_sel_parity) == 2 &&
          word(HMIL_ITEM_sel_stop_bits) == 1);
    CHECK(word(HMIL_ITEM_sel_mqtt_enabled) == 1 && word(HMIL_ITEM_speed) == 3 &&
          word(HMIL_ITEM_apply_en) == 0);
    CHECK(word(HMIL_ITEM_state) == READY && word(HMIL_ITEM_lock) == 0 &&
          word(HMIL_ITEM_toast_show) == 0);
    CHECK(text(HMIL_ITEM_sum1) == "DHCP" && text(HMIL_ITEM_sum2) == "Unit 1 - 19200");
    CHECK(text(HMIL_ITEM_sum3) == "broker.example.com" && text(HMIL_ITEM_sum6) == "Speed level 3");
    // Phase 2 values are placeholders and their segmented controls show nothing selected.
    CHECK(text(HMIL_ITEM_power1) == "--" && text(HMIL_ITEM_profile) == "--" &&
          text(HMIL_ITEM_info1) == "--");
    CHECK(text(HMIL_ITEM_return_loss) == "--" && text(HMIL_ITEM_sum4) == "--" &&
          text(HMIL_ITEM_sum5) == "--");
    CHECK(word(HMIL_ITEM_sel_ant1) == 0 && word(HMIL_ITEM_sel_maintenance) == 0);
    CHECK(word(HMIL_ITEM_hist_rows) == 8 && text(HMIL_ITEM_hist_saved) == "0 / 500 saved");
    CHECK(text(HMIL_ITEM_hist_range).empty() && text(HMIL_ITEM_hist_id1).empty());
    // Link loss while DHCP.
    status.hasIp = false;
    settle(ui);
    CHECK(text(HMIL_ITEM_ip) == "WAIT IP" && text(HMIL_ITEM_mask) == "--");
    status.link = false;
    settle(ui);
    CHECK(text(HMIL_ITEM_ip) == "NO LINK");
    status.link = status.hasIp = true;
    settle(ui);
    // The panel answering for the first time republishes everything.
    size_t before = fake.frames.size();
    status.panelSeen = true;
    settle(ui);
    CHECK(fake.frames.size() - before > HMIL_ITEM_COUNT);
    // Background refresh: one variable per 80 ms while nothing else is pending.
    before = fake.frames.size();
    for (unsigned i = 0; i < 200 && fake.frames.size() == before; i++) { // next refresh
        ++now;
        ui.tick(now, status);
        ui.flush();
    }
    CHECK(fake.frames.size() > before && fake.frames.size() <= before + 2);
    before = fake.frames.size();
    for (unsigned i = 0; i < 79; i++) {
        ++now;
        ui.tick(now, status);
        ui.flush();
    }
    CHECK(fake.frames.size() == before);
    ++now;
    ui.tick(now, status);
    ui.flush();
    CHECK(fake.frames.size() > before && fake.frames.size() <= before + 2);
    settle(ui);

    // --- Number editor: typing, repeated keys, validation, alignment of the result.
    press(ui, 0x0104); // Modbus page
    CHECK(ui.currentPage() == HMIL_PAGE_modbus && fake.pages.empty());
    before = fake.frames.size();
    press(ui, 0x0206); // unit
    CHECK(ui.editing() && fake.pages.back() == HMIL_PAGE_edit_number);
    CHECK(fake.frames.back().kind == 'P'); // the page is switched after its content
    CHECK(text(HMIL_ITEM_edit_title) == "Device / Unit ID" && xOf(HMIL_ITEM_edit_title) == 482);
    CHECK(text(HMIL_ITEM_edit_hint) == "Range 1 - 247" && text(HMIL_ITEM_edit_entry) == "1");
    CHECK(text(HMIL_ITEM_edit_error).empty() && word(HMIL_ITEM_crumb) == 2 &&
          word(HMIL_ITEM_edit_pw) == 0);
    press(ui, HMIL_EV_EDITOR_CLEAR);
    CHECK(text(HMIL_ITEM_edit_entry).empty());
    type(ui, "11"); // the same code twice is two key presses
    CHECK(text(HMIL_ITEM_edit_entry) == "11");
    type(ui, "a.-"); // not digits
    CHECK(text(HMIL_ITEM_edit_entry) == "11");
    type(ui, "5");
    press(ui, HMIL_EV_EDITOR_BACKSPACE);
    CHECK(text(HMIL_ITEM_edit_entry) == "11");
    type(ui, "11");
    press(ui, HMIL_EV_EDITOR_DONE); // 1111 is out of range
    CHECK(ui.editing() && text(HMIL_ITEM_edit_error) == "Enter a whole number from 1 to 247.");
    CHECK(ui.draftSettings().unit == 1 && fake.pages.back() == HMIL_PAGE_edit_number);
    press(ui, HMIL_EV_EDITOR_BACKSPACE);
    press(ui, HMIL_EV_EDITOR_BACKSPACE);
    press(ui, HMIL_EV_EDITOR_DONE);
    CHECK(!ui.editing() && ui.draftSettings().unit == 11 && fake.pages.back() == HMIL_PAGE_modbus);
    CHECK(text(HMIL_ITEM_unit) == "11" && xOf(HMIL_ITEM_unit) == 954 &&
          word(HMIL_ITEM_apply_en) == 1);
    CHECK(text(HMIL_ITEM_sum2) == "Unit 1 - 19200" && fake.submits == 0); // nothing applied yet
    type(ui, "9"); // keys with no editor open are ignored
    CHECK(ui.draftSettings().unit == 11);
    press(ui, 0x0207); // TCP port: 12 digits at most, port 80 refused
    press(ui, HMIL_EV_EDITOR_CLEAR);
    type(ui, "1234567890123");
    CHECK(text(HMIL_ITEM_edit_entry) == "3456789012"); // 12 digits kept, tail shown (390 px)
    press(ui, HMIL_EV_EDITOR_CLEAR);
    type(ui, "80");
    press(ui, HMIL_EV_EDITOR_DONE);
    CHECK(ui.editing() && text(HMIL_ITEM_edit_error) == "Port 80 is reserved for the web page.");
    press(ui, HMIL_EV_EDITOR_CANCEL);
    CHECK(!ui.editing() && ui.draftSettings().tcpPort == 502 &&
          fake.pages.back() == HMIL_PAGE_modbus);

    // --- Option list (20 slots): current value marked, a tap commits immediately.
    press(ui, 0x0208);
    CHECK(ui.editing() && fake.pages.back() == HMIL_PAGE_edit_options);
    CHECK(text(HMIL_ITEM_opt1) == "1200" && text(HMIL_ITEM_opt1 + 4).empty() &&
          text(HMIL_ITEM_opt1 + 7) == "115200");
    CHECK(text(HMIL_ITEM_optsel1 + 4) == "19200" && text(HMIL_ITEM_optsel1).empty());
    CHECK(text(HMIL_ITEM_opt1 + 8).empty() && text(HMIL_ITEM_opt1 + 19).empty());
    CHECK(word(HMIL_ITEM_opt_sel) == 5 && word(HMIL_ITEM_opt_pager) == 0 &&
          text(HMIL_ITEM_opt_page).empty());
    CHECK(xOf(HMIL_ITEM_opt1 + 5) == 173); // "38400" centred in its slot
    press(ui, HMIL_EV_EDITOR_OPTION_NEXT); // single page: stays
    CHECK(text(HMIL_ITEM_opt1) == "1200");
    press(ui, 0x0609); // empty slot
    CHECK(ui.editing());
    press(ui, 0x0606);
    CHECK(!ui.editing() && ui.draftSettings().baud == 38400 && text(HMIL_ITEM_baud) == "38400");
    CHECK(fake.pages.back() == HMIL_PAGE_modbus);
    // Segmented controls.
    press(ui, 0x2093); // parity Odd
    press(ui, 0x20A2); // 2 stop bits
    CHECK(ui.draftSettings().parity == 2 && ui.draftSettings().stopBits == 2);
    CHECK(word(HMIL_ITEM_sel_parity) == 3 && word(HMIL_ITEM_sel_stop_bits) == 2);
    press(ui, 0x2094); // no such option
    press(ui, 0x2090);
    CHECK(ui.draftSettings().parity == 2);

    // --- Leaving a section with unapplied changes.
    press(ui, 0x0105); // to MQTT
    CHECK(ui.dialogOpen() && fake.pages.back() == HMIL_PAGE_dialog);
    CHECK(text(HMIL_ITEM_dlg_title) == "Discard changes?" && word(HMIL_ITEM_dlg_btn) == 1);
    CHECK(text(HMIL_ITEM_dlg_body) == "Your changes have not been applied.");
    press(ui, HMIL_EV_DIALOG_CANCEL);
    CHECK(!ui.dialogOpen() && fake.pages.back() == HMIL_PAGE_modbus &&
          ui.currentPage() == HMIL_PAGE_modbus);
    CHECK(ui.draftSettings().unit == 11 && word(HMIL_ITEM_apply_en) == 1);
    press(ui, HMIL_EV_DIALOG_CONFIRM_BUTTON); // no dialog: ignored
    CHECK(ui.draftSettings().unit == 11);
    press(ui, 0x0105);
    press(ui, HMIL_EV_DIALOG_CONFIRM_BUTTON);
    CHECK(!ui.unapplied() && ui.draftSettings().unit == 1 && ui.draftSettings().baud == 19200);
    CHECK(text(HMIL_ITEM_unit) == "1" && text(HMIL_ITEM_baud) == "19200" &&
          word(HMIL_ITEM_sel_parity) == 2);
    CHECK(word(HMIL_ITEM_apply_en) == 0 && fake.pages.back() == HMIL_PAGE_mqtt &&
          ui.currentPage() == HMIL_PAGE_mqtt);
    CHECK(fake.submits == 0);

    // --- Password editor: stars unless Show; the field itself never shows the text.
    before = fake.frames.size();
    press(ui, 0x020F);
    CHECK(fake.pages.back() == HMIL_PAGE_edit_lower && word(HMIL_ITEM_edit_pw) == 1 &&
          word(HMIL_ITEM_crumb) == 3);
    type(ui, "s3cret");
    CHECK(text(HMIL_ITEM_edit_entry) == "******" && !anyFrameHas(before, "s3"));
    press(ui, HMIL_EV_PASSWORD_EDITOR_SHOW_HIDE);
    CHECK(text(HMIL_ITEM_edit_entry) == "s3cret" && word(HMIL_ITEM_edit_pw) == 2);
    press(ui, HMIL_EV_PASSWORD_EDITOR_SHOW_HIDE);
    CHECK(text(HMIL_ITEM_edit_entry) == "******" && word(HMIL_ITEM_edit_pw) == 1);
    before = fake.frames.size();
    press(ui, HMIL_EV_EDITOR_DONE);
    CHECK(!strcmp(ui.draftSettings().password, "s3cret") &&
          text(HMIL_ITEM_mqtt_password) == "******");
    CHECK(xOf(HMIL_ITEM_mqtt_password) == 966 - 6 * 9 && text(HMIL_ITEM_edit_entry).empty());
    CHECK(!anyFrameHas(before, "s3cret"));
    press(ui, 0x020E); // Show is only for the password editor
    press(ui, HMIL_EV_PASSWORD_EDITOR_SHOW_HIDE);
    CHECK(word(HMIL_ITEM_edit_pw) == 0);
    type(ui, "operator 1");
    CHECK(text(HMIL_ITEM_edit_entry) == "operator 1");
    press(ui, HMIL_EV_EDITOR_DONE);
    CHECK(!strcmp(ui.draftSettings().user, "operator 1") &&
          text(HMIL_ITEM_mqtt_user) == "operator 1");
    press(ui, 0x020D); // host: refused text stays in the editor
    type(ui, "/x");
    press(ui, HMIL_EV_EDITOR_DONE);
    CHECK(ui.editing() && text(HMIL_ITEM_edit_error) ==
                              "Enter a hostname or IPv4 address, without a prefix or path.");
    press(ui, HMIL_EV_EDITOR_CLEAR);
    // A long entry keeps its tail visible (27 'W' fit the 690 px line in that face).
    for (unsigned i = 0; i < 60; i++)
        ui.event(0x0457);
    settle(ui);
    CHECK(text(HMIL_ITEM_edit_entry) == std::string(27, 'W'));
    press(ui, HMIL_EV_EDITOR_CLEAR);
    for (unsigned i = 0; i < 127; i++) // narrow glyphs: more would fit than one frame carries
        ui.event(0x0469);
    settle(ui);
    CHECK(text(HMIL_ITEM_edit_entry) == std::string(108, 'i'));
    for (const Frame &f : fake.frames)
        CHECK(f.text.size() <= 108);
    press(ui, HMIL_EV_EDITOR_CANCEL);
    CHECK(!strcmp(ui.draftSettings().host, "broker.example.com"));

    // --- Apply is refused while the machine is busy, at both checkpoints.
    status.pill = RUNNING;
    press(ui, HMIL_EV_APPLY_CHANGES_PRESSED);
    CHECK(!ui.dialogOpen() && word(HMIL_ITEM_state) == RUNNING &&
          word(HMIL_ITEM_lock) == LOCK_WAIT_IDLE);
    CHECK(text(HMIL_ITEM_toast) == "BUSY - wait until the machine is idle." &&
          word(HMIL_ITEM_toast_show) == 1);
    CHECK(xOf(HMIL_ITEM_toast) == 397);
    press(ui, 0x020C); // opening an editor
    CHECK(!ui.editing());
    press(ui, 0x20B2); // segmented control
    CHECK(ui.draftSettings().mqttEnabled == 1 && word(HMIL_ITEM_sel_mqtt_enabled) == 1);
    status.pill = REMOTE;
    press(ui, 0x020C);
    CHECK(!ui.editing() && word(HMIL_ITEM_lock) == LOCK_REMOTE);
    CHECK(text(HMIL_ITEM_toast) == "BUSY - release remote control first.");
    status.pill = VERIFY;
    press(ui, 0x020C);
    CHECK(!ui.editing() && word(HMIL_ITEM_state) == VERIFY);
    status.pill = STOPPED;
    press(ui, HMIL_EV_APPLY_CHANGES_PRESSED);
    CHECK(!ui.dialogOpen() && word(HMIL_ITEM_state) == STOPPED);
    status.pill = READY;
    press(ui, HMIL_EV_APPLY_CHANGES_PRESSED);
    CHECK(ui.dialogOpen() && fake.pages.back() == HMIL_PAGE_dialog && word(HMIL_ITEM_dlg_btn) == 3);
    CHECK(text(HMIL_ITEM_dlg_title) == "Apply and restart?");
    CHECK(text(HMIL_ITEM_dlg_body) == "The board will restart and reconnect with these settings.");
    status.pill = RUNNING; // a cycle starts while the dialog is open
    press(ui, HMIL_EV_DIALOG_CONFIRM_BUTTON);
    CHECK(fake.submits == 0 && !ui.dialogOpen() && fake.pages.back() == HMIL_PAGE_mqtt);
    CHECK(text(HMIL_ITEM_toast) == "BUSY - wait until the machine is idle." && ui.unapplied());
    status.pill = READY;
    press(ui, HMIL_EV_APPLY_CHANGES_PRESSED);
    press(ui, HMIL_EV_DIALOG_CANCEL);
    CHECK(fake.submits == 0 && fake.pages.back() == HMIL_PAGE_mqtt);
    press(ui, HMIL_EV_APPLY_CHANGES_PRESSED);
    fake.apply = APPLY_PENDING; // what the save path reports once a request is accepted
    press(ui, HMIL_EV_DIALOG_CONFIRM_BUTTON);
    CHECK(fake.submits == 1 && fake.pages.back() == HMIL_PAGE_mqtt);
    CHECK(!strcmp(fake.submitted.password, "s3cret") && !strcmp(fake.submitted.user, "operator 1"));
    CHECK(fake.submitted.unit == 1 && !cfg::validate(fake.submitted));
    // The admin account is passed through untouched.
    CHECK(fake.submitted.adminSet == 1 && !memcmp(fake.submitted.adminSalt, s.adminSalt, 16) &&
          !memcmp(fake.submitted.adminHash, s.adminHash, 32));
    CHECK(text(HMIL_ITEM_toast) == "Saving. The board will restart.");
    // The save path can still deny it (machine became busy before the fence).
    settle(ui);
    CHECK(text(HMIL_ITEM_toast) == "Saving. The board will restart.");
    fake.apply = APPLY_DENIED;
    settle(ui);
    CHECK(text(HMIL_ITEM_toast) == "BUSY - wait until the machine is idle.");
    fake.apply = APPLY_IDLE;
    fake.submitResult = SUBMIT_IN_PROGRESS;
    press(ui, HMIL_EV_APPLY_CHANGES_PRESSED);
    press(ui, HMIL_EV_DIALOG_CONFIRM_BUTTON);
    CHECK(fake.submits == 2 && text(HMIL_ITEM_toast) == "Another settings change is in progress.");
    fake.submitResult = SUBMIT_OK;

    // --- Toast disappears after about 3.5 s.
    CHECK(word(HMIL_ITEM_toast_show) == 1);
    now += 3400;
    settle(ui);
    CHECK(word(HMIL_ITEM_toast_show) == 1);
    now += 200;
    settle(ui);
    CHECK(word(HMIL_ITEM_toast_show) == 0);
    CHECK(text(HMIL_ITEM_toast).empty());
    CHECK(fake.frames.back().kind == 'P' && fake.pages.back() == ui.currentPage());

    // --- Ethernet: addresses are editable only in Static mode; cross-field check at Apply.
    press(ui, 0x0103);
    press(ui, HMIL_EV_DIALOG_CONFIRM_BUTTON); // discard the MQTT draft
    CHECK(!ui.unapplied() && ui.currentPage() == HMIL_PAGE_ethernet);
    press(ui, 0x0202);
    CHECK(!ui.editing() && text(HMIL_ITEM_toast) == "Select Static IP to edit this address.");
    press(ui, 0x2012);
    CHECK(word(HMIL_ITEM_sel_dhcp) == 2 && text(HMIL_ITEM_ip) == "192.168.4.1" &&
          word(HMIL_ITEM_apply_en) == 1);
    CHECK(text(HMIL_ITEM_gateway) == "0.0.0.0" && text(HMIL_ITEM_sum1) == "DHCP");
    press(ui, 0x0202);
    CHECK(ui.editing() && fake.pages.back() == HMIL_PAGE_edit_ip &&
          text(HMIL_ITEM_edit_entry) == "192.168.4.1");
    CHECK(text(HMIL_ITEM_edit_hint) == "Four numbers from 0 to 255" && word(HMIL_ITEM_crumb) == 1);
    press(ui, HMIL_EV_EDITOR_CLEAR);
    type(ui, "10.0.0x.7");
    CHECK(text(HMIL_ITEM_edit_entry) == "10.0.0.7");
    type(ui, "12345678"); // 15 characters at most
    CHECK(text(HMIL_ITEM_edit_entry) == "10.0.0.71234567");
    press(ui, HMIL_EV_EDITOR_DONE);
    CHECK(ui.editing() &&
          text(HMIL_ITEM_edit_error) == "Enter four numbers from 0 to 255, separated by dots.");
    for (unsigned i = 0; i < 7; i++)
        press(ui, HMIL_EV_EDITOR_BACKSPACE);
    press(ui, HMIL_EV_EDITOR_DONE);
    CHECK(!ui.editing() && text(HMIL_ITEM_ip) == "10.0.0.7" && ui.draftSettings().ip[0] == 10);
    press(ui, 0x0204);
    press(ui, HMIL_EV_EDITOR_CLEAR);
    type(ui, "10.9.0.1"); // valid address, wrong subnet for 255.255.255.0
    press(ui, HMIL_EV_EDITOR_DONE);
    CHECK(!ui.editing() && text(HMIL_ITEM_gateway) == "10.9.0.1");
    press(ui, HMIL_EV_APPLY_CHANGES_PRESSED);
    CHECK(!ui.dialogOpen() &&
          text(HMIL_ITEM_toast) == "The gateway must be in the same subnet as the IP.");
    press(ui, 0x2011);
    CHECK(text(HMIL_ITEM_ip) == "192.168.1.50" && word(HMIL_ITEM_sel_dhcp) == 1);
    // The machine page forcing (control/result page) closes an open editor; no page fight.
    press(ui, 0x2012);
    press(ui, 0x0203);
    CHECK(ui.editing());
    size_t pagesBefore = fake.pages.size();
    status.forcedPages = 1;
    status.forcedPage = 2;
    settle(ui);
    CHECK(!ui.editing() && ui.currentPage() == 2 && fake.pages.size() == pagesBefore);
    type(ui, "7");
    press(ui, HMIL_EV_EDITOR_DONE);
    CHECK(fake.pages.size() == pagesBefore && ui.draftSettings().mask[3] == 0);
    press(ui, 0x0101); // Cancel on the result page returns Home: no dialog from there
    CHECK(!ui.dialogOpen() && ui.currentPage() == HMIL_PAGE_home && ui.unapplied());
    press(ui, 0x0103);
    press(ui, 0x010B); // leaving Ethernet with the kept draft asks again
    CHECK(ui.dialogOpen());
    press(ui, HMIL_EV_DIALOG_CONFIRM_BUTTON);
    CHECK(!ui.unapplied() && ui.currentPage() == HMIL_PAGE_machine);

    // --- Machine page: speed goes through the host only after the dialog.
    press(ui, 0x0804);
    CHECK(word(HMIL_ITEM_speed) == 4 && word(HMIL_ITEM_apply_en) == 1 && fake.speedCalls == 0);
    press(ui, 0x0806); // no such level
    CHECK(ui.draftSpeedLevel() == 4);
    press(ui, HMIL_EV_APPLY_CHANGES_PRESSED);
    CHECK(ui.dialogOpen() && word(HMIL_ITEM_dlg_btn) == 2 &&
          text(HMIL_ITEM_dlg_title) == "Apply changes?");
    press(ui, HMIL_EV_DIALOG_CONFIRM_BUTTON);
    CHECK(fake.speedCalls == 1 && fake.speedSet == 4 && fake.submits == 2);
    CHECK(text(HMIL_ITEM_toast) == "Conveyor speed saved." &&
          fake.pages.back() == HMIL_PAGE_machine);
    status.speed = 4; // the machine now reports the new level
    settle(ui);
    CHECK(word(HMIL_ITEM_apply_en) == 0 && text(HMIL_ITEM_sum6) == "Speed level 4" &&
          !ui.unapplied());
    status.speed = 2; // changed remotely while not editing: the page follows
    settle(ui);
    CHECK(word(HMIL_ITEM_speed) == 2 && !ui.unapplied());
    fake.speedResult = SPEED_BUSY;
    press(ui, 0x0805);
    press(ui, HMIL_EV_APPLY_CHANGES_PRESSED);
    press(ui, HMIL_EV_DIALOG_CONFIRM_BUTTON);
    CHECK(fake.speedCalls == 2 &&
          text(HMIL_ITEM_toast) == "Not saved - machine busy or reader not ready.");
    CHECK(ui.unapplied());
    status.pill = RUNNING;
    press(ui, 0x0801);
    CHECK(ui.draftSpeedLevel() == 5);
    press(ui, HMIL_EV_APPLY_CHANGES_PRESSED);
    CHECK(!ui.dialogOpen() && fake.speedCalls == 2);
    status.pill = READY;
    press(ui, 0x0802);
    CHECK(!ui.unapplied());

    // --- Phase 2 pages: a toast, never a host call, never an editor.
    const uint16_t later[] = {0x021A, 0x021B, 0x021D, 0x21C3, 0x21E2, 0x0704, 0x0705};
    press(ui, 0x0106);
    for (uint16_t code : later) {
        fake.texts[HMIL_ITEMS[HMIL_ITEM_toast].vp].clear();
        press(ui, code);
        CHECK(text(HMIL_ITEM_toast) == "Not available in this firmware yet." && !ui.editing());
        CHECK(!ui.unapplied() && !ui.dialogOpen());
    }
    for (uint16_t p : {uint16_t(0x0107), uint16_t(0x010A)}) {
        press(ui, p);
        fake.texts[HMIL_ITEMS[HMIL_ITEM_toast].vp].clear();
        press(ui, HMIL_EV_APPLY_CHANGES_PRESSED);
        CHECK(text(HMIL_ITEM_toast) == "Not available in this firmware yet." && !ui.dialogOpen());
    }
    CHECK(xOf(HMIL_ITEM_toast) == 416 && fake.submits == 2 && fake.speedCalls == 2);
    CHECK(word(HMIL_ITEM_sel_ant1) == 0 && text(HMIL_ITEM_power1) == "--");

    // --- History page.
    press(ui, 0x010C);
    CHECK(word(HMIL_ITEM_hist_rows) == 8);
    for (unsigned i = 1; i <= 16; i++)
        CHECK(fake.log.append(i, i == 16 ? 0 : 1762850096 + i, i % 2, uint16_t(100 + i)));
    fake.texts[HMIL_ITEMS[HMIL_ITEM_toast].vp].clear();
    settle(ui);
    CHECK(text(HMIL_ITEM_toast).empty()); // newest is an AUTO cycle: no toast
    CHECK(text(HMIL_ITEM_hist_id1) == "#16" && text(HMIL_ITEM_hist_time1) == "--/--/---- --:--:--");
    CHECK(text(HMIL_ITEM_hist_mode1) == "Auto" && text(HMIL_ITEM_hist_count1) == "116");
    CHECK(text(HMIL_ITEM_hist_id1 + 1) == "#15" &&
          text(HMIL_ITEM_hist_time1 + 1) == "11/11/2025 15:35:11");
    CHECK(text(HMIL_ITEM_hist_mode1 + 1) == "Semi Auto" &&
          text(HMIL_ITEM_hist_count1 + 6) == "110");
    CHECK(word(HMIL_ITEM_hist_rows) == 7 && text(HMIL_ITEM_hist_saved) == "16 / 500 saved");
    CHECK(text(HMIL_ITEM_hist_range) == "1-7 of 16");
    press(ui, HMIL_EV_HISTORY_PREV_PAGE);
    CHECK(text(HMIL_ITEM_hist_range) == "1-7 of 16");
    press(ui, HMIL_EV_HISTORY_NEXT_PAGE);
    press(ui, HMIL_EV_HISTORY_NEXT_PAGE);
    CHECK(text(HMIL_ITEM_hist_range) == "15-16 of 16" && word(HMIL_ITEM_hist_rows) == 2);
    CHECK(text(HMIL_ITEM_hist_id1) == "#2" && text(HMIL_ITEM_hist_id1 + 1) == "#1" &&
          text(HMIL_ITEM_hist_id1 + 2).empty());
    CHECK(text(HMIL_ITEM_hist_count1 + 2).empty());
    press(ui, HMIL_EV_HISTORY_NEXT_PAGE);
    CHECK(text(HMIL_ITEM_hist_range) == "15-16 of 16");
    CHECK(fake.log.append(99, 1762850096, hist::MODE_SEMI, 64)); // operator confirmed a result
    settle(ui);
    CHECK(text(HMIL_ITEM_toast) == "Finalized EPC count saved to History.");
    CHECK(text(HMIL_ITEM_hist_range) == "15-17 of 17" && text(HMIL_ITEM_hist_id1 + 2) == "#1");
    CHECK(fake.pages.back() == HMIL_PAGE_history); // toast repaints only the current page

    // --- Flow control: never more than 6 frames per flush, page after content.
    fake.room = -1;
    ui.tick(now, status);
    ui.event(0x0103);
    ui.event(0x2012);
    ui.event(0x0203);
    before = fake.frames.size();
    ui.flush();
    CHECK(fake.frames.size() - before <= Ui::FRAMES_PER_FLUSH && fake.frames.back().kind != 'P');
    fake.room = 0;
    for (unsigned i = 0; i < 50; i++) {
        ui.tick(++now, status);
        ui.flush(); // full UART: returns at once, state intact
    }
    CHECK(ui.editing() && !ui.idle());
    fake.room = -1;
    settle(ui);
    CHECK(fake.frames.back().kind == 'P' && fake.pages.back() == HMIL_PAGE_edit_ip);
    CHECK(text(HMIL_ITEM_edit_title) == "Subnet mask" &&
          text(HMIL_ITEM_edit_entry) == "255.255.255.0");
    printf("HMI v2 UI passed: alignment/fit vs reference, editors, validation, busy gating, "
           "discard dialog, toast timeout, history paging, speed, phase-2 stubs, UART retry\n");
    return 0;
}
