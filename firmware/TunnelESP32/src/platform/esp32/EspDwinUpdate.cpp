#include <Arduino.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <atomic>
#include "EspDwinUpdate.h"
#include "EspPlatform.h"
#include "EspConfig.h"
#include "EspFota.h"
#include "EspHistory.h"
#include "EspHmiAntenna.h"
#include "EspModbus.h"
#include "../../hmi/DwinPatch.h"
#include "../../hmi/DwinScanPatchData.h"
extern "C" {
#include "../../control/tunnel.h"
#include "../../hmi/hmi.h"
#include "../../protocol/legacy/SharedUart.h"
#include "../../protocol/legacy/pc_control.h"
}
namespace {
std::atomic<dwinpatch::Result> result(dwinpatch::NOT_RUN);
std::atomic<const char *> phase("waiting_ota_health");
std::atomic<bool> hold(false),waiting(true);
class SerialPort : public dwinpatch::Port {
    uint32_t generation;
  public:
    SerialPort() : generation(EspRfStartToken()) {}
    uint32_t now() override { return millis(); }
    bool send(const uint8_t *data,size_t size) override {
        if (aborted()) return false;
        const uint32_t start=millis();
        while (Serial0.availableForWrite()<int(size)) {
            if (aborted() || uint32_t(millis()-start)>=100) return false;
            pause(1);
        }
        return Serial0.write(data,size)==size;
    }
    int receive() override { return Serial0.available()?Serial0.read():-1; }
    void pause(uint32_t ms) override {
        for (uint32_t n=0;n<ms;++n) { EspControlAlive(); delay(1); }
    }
    bool aborted() override { return generation!=EspRfStartToken(); }
    void emergency() override { EspRequestEmergency(); }
};
class Store : public dwinpatch::Journal {
    Preferences prefs;
    bool ready;
  public:
    Store() : ready(prefs.begin("dwin-upd",false)) {}
    ~Store() { if (ready) prefs.end(); }
    bool pending(uint32_t tag) override { return ready && prefs.getUInt("pending",0)==tag; }
    bool arm(uint32_t tag) override {
        EspControlAlive();
        return ready && prefs.putUInt("pending",tag)==sizeof(tag) && pending(tag);
    }
    bool clear() override {
        return ready && prefs.putUInt("pending",0)==sizeof(uint32_t) && prefs.getUInt("pending",1)==0;
    }
};
}
void EspDwinUpdateBegin() {
    waiting=true; hold=false; result=dwinpatch::NOT_RUN; phase="waiting_ota_health";
    Tunnel_SetHmiScanExplicit(1);
    Tunnel_StopForSafety(STOP_PC_DISCONNECT);
    Hmi_SetControlLocked(1);
}
void EspDwinUpdatePoll() {
    if (!waiting || !EspConfigStorageReady() || !EspMotorReady() || EspFotaBusy() ||
        EspConfigBusy() || EspHistoryWriting() || EspHmiAntennaBusy() || EspModbusActive()) return;
    esp_ota_img_states_t state;
    const esp_err_t err=esp_ota_get_state_partition(esp_ota_get_running_partition(), &state);
    // Let the existing FOTA health/rollback flow confirm ESP32 first. If power
    // fails during panel flash, the recovery-capable app must remain bootable.
    if (err==ESP_OK && state!=ESP_OTA_IMG_VALID && state!=ESP_OTA_IMG_UNDEFINED) return;
    if (err!=ESP_OK && err!=ESP_ERR_NOT_FOUND) return;
    EspControlAlive();
    SerialPort port;
    Store store;
    const bool pending=store.pending(dwinpatchdata::IMAGE.journalTag);
    dwinpatch::Updater updater(port,store,dwinpatchdata::IMAGE);
    phase="updating";
    result=updater.run(); phase=updater.stage();
    hold=(pending || updater.wroteFlash()) && result!=dwinpatch::UPDATED &&
         result!=dwinpatch::ALREADY_CURRENT;
    EspFenceSerialInput();
    PcControl_DiscardInput();
    Hmi_DiscardActionsAfterStop();
    waiting=false;
    if (!hold) Hmi_SetControlLocked(0);
    EspControlAlive();
}
const char *EspDwinUpdateStatus() { return waiting ? phase.load() : dwinpatch::name(result.load()); }
const char *EspDwinUpdateStage() { return phase.load(); }
uint8_t EspDwinUpdateHold() { return waiting || hold; }
const char *EspDwinUpdateNotice() {
    if (result==dwinpatch::UPDATED) return "DWIN Scan buttons updated and verified.";
    if (result==dwinpatch::ALREADY_CURRENT) return "";
    return "DWIN update not complete. Check board status.";
}
