#include <Arduino.h>
#include <ETH.h>
#include "EspHmiUi.h"
#include "EspHmiAntenna.h"
#include "EspDwinUpdate.h"
#include "EspConfig.h"
#include "EspHistory.h"
#include "EspPlatform.h"
#include "../../hmi/HmiUi.h"
#include "../../hmi/HmiSettingsGate.h"
extern "C" {
#include "../../hmi/hmi.h"
#include "../../control/tunnel.h"
}

// 1 = bench diagnostic build (shows panel-link counters on the toast line). Must be 0 for release.
#define HMI_LINK_DIAG 0

namespace {
// The panel UART queue holds 256 bytes and is shared with the machine's own
// frames (quantity, page, lamps) and the PC envelope. UI frames are written
// only when the transmitter was completely idle at the start of this pass and
// only as many as certainly fit, so a UI burst never makes a write wait and
// never takes the room a result page or a PC reply needs.
const int QUEUE_BUDGET = 200;   // of 256 queue bytes per idle pass
const int MACHINE_RESERVE = 48; // fallback rule only
const uint32_t IDLE_PROBE_STALL_MS = 250;
const unsigned EVENTS_PER_POLL = 8;
uint32_t ownPageWrites = 0, lastIdle = 0;
int budget = 0;
bool remote = false, fallback = false;

// Queue bytes one write takes: an event record plus an 8-byte header for
// every 60-byte piece the driver splits it into (rounded up generously).
bool room(int frameBytes) {
    const int cost = fallback ? frameBytes : frameBytes + 16 + 11 * ((frameBytes + 59) / 60);
    if (cost > budget)
        return false;
    budget -= cost;
    return true;
}
void openBudget(uint32_t now) {
    const bool idle = EspHmiTxIdle();
    if (idle)
        lastIdle = now;
    // A full queue drains in about 35 ms. If the idle probe has said "busy" for
    // much longer it is not usable on this core; fall back to the free-space rule.
    fallback = !idle && uint32_t(now - lastIdle) > IDLE_PROBE_STALL_MS;
    budget = idle ? QUEUE_BUDGET : fallback ? int(EspHmiTxFree()) - MACHINE_RESERVE : 0;
    // PC replies go first, and the one state whose quantity/page write must not
    // be delayed (it faults after 5 s without it) gets the line to itself.
    if (EspPcTxPending() || Tunnel_GetState() == TUNNEL_DISPLAY_RESULT)
        budget = 0;
}
bool atRest(TunnelState state) {
    return state == TUNNEL_WAIT_SS1_LOW || state == TUNNEL_WAIT_REARM || state == TUNNEL_STOPPED ||
           state == TUNNEL_STARTUP_WAIT;
}
struct PanelHost : hmiui::Host {
    // Frame = 6 header bytes + payload (+2 CRC when the panel uses it).
    bool sendText(uint16_t vp, const char *text, uint16_t bytes) override {
        return room(bytes + 8) && Hmi_SendText(vp, text, bytes);
    }
    bool sendWord(uint16_t vp, uint16_t value) override {
        return room(10) && Hmi_SendWord(vp, value);
    }
    bool sendPage(uint16_t page) override {
        if (!room(12) || !Hmi_SendPage(page))
            return false;
        ++ownPageWrites;
        return true;
    }
    const cfg::Settings &settings() override {
        return EspConfigGet();
    }
    uint8_t submitSettings(const cfg::Settings &next) override {
        return EspConfigSubmitLocal(next);
    }
    uint8_t applyState() override {
        return EspConfigApplyState();
    }
    const antenna::State &antennaState() override { return EspHmiAntennaState(); }
    bool requestAntenna(const antenna::Values *next, bool save) override {
        return EspHmiAntennaStart(next, save);
    }
    // Existing speed path (operation 0x19): refuses during motion, scan or a reader
    // transaction and acknowledges only a verified store. It claims no ownership;
    // with no remote owner its lease heartbeat has nothing to extend.
    uint8_t setSpeed(uint8_t level) override {
        if (remote || Hmi_IsControlLocked() || Tunnel_IsPcMaintenance())
            return hmiui::SPEED_BUSY;
        // The speed store writes NVS on this task; do not queue behind a history write.
        if (EspHistoryWriting())
            return hmiui::SPEED_BUSY;
        return Tunnel_RemoteCommand(0x19U, level);
    }
    const hist::Log &history() override {
        return EspHistoryLog();
    }
};
PanelHost host;
hmiui::Ui ui(host);
} // namespace

bool EspMachineQuiet(bool active) {
    const TunnelState state = Tunnel_GetState();
    return !active && (atRest(state) || state == TUNNEL_FAULT);
}
void EspHmiUiPoll(bool remoteOwner, bool ethernetReady, bool active) {
    remote = remoteOwner;
    const uint8_t flags = Tunnel_GetStopFlags(); // 1 stop latched, 8 PC session, 16 DWIN locked
    const TunnelState state = Tunnel_GetState();
    hmiui::Machine machine = {};
    machine.stopped = (flags & 1U) || state == TUNNEL_FAULT;
    machine.remote = remoteOwner || (flags & 8U) || ((flags & 16U) && !EspHmiAntennaBusy()) ||
                     (Tunnel_IsPcMaintenance() && !EspHmiAntennaBusy());
    machine.verify = state == TUNNEL_WAIT_CONFIRM;
    machine.running = hmiui::settingsGate(state, active, false) != hmiui::LOCK_NONE;
    hmiui::Status status;
    status.pill = hmiui::pill(machine);
    status.settingsLock = hmiui::settingsGate(state, active, machine.remote);
    status.panelSeen = g_hmi_debug.config_known != 0U;
    status.link = ethernetReady && ETH.linkUp();
    status.hasIp = ethernetReady && ETH.hasIP();
    const IPAddress ip = ETH.localIP(), mask = ETH.subnetMask(), gateway = ETH.gatewayIP(),
                    dns = ETH.dnsIP();
    for (unsigned i = 0; i < 4; i++) {
        status.ip[i] = ip[i];
        status.mask[i] = mask[i];
        status.gateway[i] = gateway[i];
        status.dns[i] = dns[i];
    }
    status.speed = Tunnel_GetSpeedLevel();
    uint32_t pageWrites = 0;
    status.forcedPage = Hmi_GetLastPage(&pageWrites);
    status.forcedPages = pageWrites - ownPageWrites; // pages switched by the machine code
    const uint32_t now = millis();
    openBudget(now);
    ui.tick(now, status);
    static bool bootNotice=false;
    if (!bootNotice) {
        bootNotice=true;
        const char *message=EspDwinUpdateNotice();
        if (message[0]) ui.notice(message);
    }
    uint16_t code;
    for (unsigned n = 0; n < EVENTS_PER_POLL && Hmi_PopUiEvent(&code); n++)
        ui.event(code);
    ui.flush();
#if HMI_LINK_DIAG
    // Temporary bench aid: once a second show on the panel what the ESP32 has
    // received from it (bytes, frames, damaged frames, UART errors, config replies).
    static uint32_t diagAt = 0;
    if (uint32_t(now - diagAt) >= 1000U && EspHmiTxIdle() && !EspPcTxPending()) {
        diagAt = now;
        char text[64];
        snprintf(text, sizeof(text), "RX b%lu f%lu bad%lu err%lu cfg%lu",
                 (unsigned long)g_hmi_debug.rx_byte_count, (unsigned long)g_hmi_debug.rx_frame_count,
                 (unsigned long)g_hmi_debug.rx_malformed_frame_count,
                 (unsigned long)g_hmi_debug.uart_error_count,
                 (unsigned long)g_hmi_debug.config_response_count);
        Hmi_SendWord(0x4001U, 300U); // toast X register: start at the left of its box
        Hmi_SendText(HMIL_VP_TOAST, text, 48U);
        Hmi_SendWord(HMIL_VP_TOAST_SHOW, 1U);
    }
#endif
}
