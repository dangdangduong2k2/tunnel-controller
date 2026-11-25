#include <Arduino.h>
#include "EspHmiAntenna.h"
#include "EspConfig.h"
#include "EspFota.h"
#include "EspHistory.h"
#include "EspPlatform.h"
#include "../../hmi/HmiSettingsGate.h"
extern "C" {
#include "../../control/tunnel.h"
#include "../../control/motor_pwm.h"
#include "../../hmi/hmi.h"
#include "../../reader/zk7182.h"
#include "../../protocol/legacy/pc_control.h"
uint8_t EspModbusActive(void);
}

namespace {
bool quiet() {
    for (unsigned i = 0; i < 4; ++i)
        if (MotorPwm_IsRunning((MotorPwmId)i)) return false;
    return !ZK7182_IsScanActive() && !ZK7182_IsInventoryRunning() &&
           hmiui::settingsGate(Tunnel_GetState(), false, false) == hmiui::LOCK_NONE;
}
class ReaderTransport : public antenna::Transport {
    uint32_t stopToken = 0;
  public:
    bool acquire() override {
        if (EspModbusActive() || EspModbusReaderBusy() || PcControl_IsReaderBusy() ||
            Hmi_IsControlLocked() || Tunnel_IsPcMaintenance() ||
            EspConfigBusy() || EspFotaBusy() || EspHistoryWriting() || !quiet() ||
            !ZK7182_IsReady() || ZK7182_IsInventoryResponsePending()) return false;
        stopToken = EspRfStartToken();
        Tunnel_PcHeartbeat();
        if (Tunnel_RemoteCommand(0x20, 1) != 0) return false;
        Hmi_SetControlLocked(1); // block queued motion/Reset; Emergency remains available
        return true;
    }
    bool allowed() override {
        return stopToken == EspRfStartToken() && Tunnel_IsPcMaintenance() &&
               !EspModbusActive() && !EspConfigBusy() && !EspFotaBusy() && quiet();
    }
    bool ready() override { return ZK7182_IsReady() != 0; }
    bool begin(uint8_t command, const uint8_t *data, uint8_t length) override {
        return allowed() && ZK7182_BeginPcCommand(command, data, length);
    }
    bool reply(uint8_t *data, uint8_t *length, uint8_t *status) override {
        return ZK7182_TakePcReply(data, length, status) != 0;
    }
    void release() override {
        // Local job fenced all other owners; never clear another controller's maintenance.
        if (!EspModbusActive()) {
            (void)Tunnel_RemoteCommand(0x20, 0);
            Hmi_SetControlLocked(0);
        }
    }
} reader;
antenna::Job job(reader);
}
bool EspHmiAntennaBusy() { return job.state().busy; }
const antenna::State &EspHmiAntennaState() { return job.state(); }
bool EspHmiAntennaStart(const antenna::Values *values, bool save) {
    return job.start(millis(), values, save);
}
void EspHmiAntennaPoll() {
    if (job.state().busy && reader.allowed()) Tunnel_PcHeartbeat();
    job.tick(millis());
}
void EspHmiAntennaCancel() { job.cancel(); }
