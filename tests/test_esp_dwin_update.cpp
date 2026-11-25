#include "Arduino.h"
#include "esp_ota_ops.h"
#include "dwin_panel_fake.h"
#include "EspDwinUpdate.h"
#include "EspConfig.h"
#include "EspFota.h"
#include "EspHmiAntenna.h"
#include "EspHistory.h"
#include "EspModbus.h"
#include "EspPlatform.h"
#include "tunnel.h"
#include "hmi.h"
extern "C" {
#include "pc_control.h"
}
#include <cstdio>
static PanelFake panel;
TestSerial Serial0;
uint32_t testDwinJournal=0;
static uint32_t token=1,health=0;
static bool stopped=false,locked=false,explicitScan=false,storage=true,motor=true,
    fota=true,config=false,history=false,antennaBusy=false,owner=false;
static esp_ota_img_states_t imageState=ESP_OTA_IMG_PENDING_VERIFY;
uint32_t millis() { return panel.clock; }
void delay(uint32_t ms) { panel.pause(ms); }
int TestSerial::availableForWrite() { return 256; }
size_t TestSerial::write(const uint8_t *b,size_t n) {
    assert(stopped && EspDwinUpdateHold() && explicitScan && imageState==ESP_OTA_IMG_VALID);
    if(b[3]==0x82 && b[4]==0 && b[5]==0xaa) assert(testDwinJournal==dwinpatchdata::IMAGE.journalTag);
    return panel.send(b,n)?n:0;
}
int TestSerial::available() { return int(panel.incoming.size()); }
int TestSerial::read() { return panel.receive(); }
const esp_partition_t *esp_ota_get_running_partition() { static esp_partition_t p; return &p; }
esp_err_t esp_ota_get_state_partition(const esp_partition_t *,esp_ota_img_states_t *s) { *s=imageState; return ESP_OK; }
bool EspConfigStorageReady() { return storage; }
bool EspHistoryWriting() { return history; }
bool EspHmiAntennaBusy() { return antennaBusy; }
extern "C" {
void Tunnel_SetHmiScanExplicit(uint8_t v) { explicitScan=v!=0; }
void Tunnel_StopForSafety(uint8_t) { stopped=true; ++token; }
void Hmi_SetControlLocked(uint8_t v) { locked=v!=0; }
void EspControlAlive() { ++health; }
uint32_t EspRfStartToken() { return token; }
void EspRequestEmergency() { ++token; stopped=true; }
uint8_t EspMotorReady() { return motor; }
uint8_t EspFotaBusy() { return fota; }
uint8_t EspConfigBusy() { return config; }
uint8_t EspModbusActive() { return owner; }
void EspFenceSerialInput() { panel.incoming.clear(); }
void PcControl_DiscardInput() {}
void Hmi_DiscardActionsAfterStop() {}
}
int main() {
    EspDwinUpdateBegin(); assert(EspDwinUpdateHold() && stopped && explicitScan && locked);
    EspDwinUpdatePoll(); assert(!panel.ramWrites && !panel.crcRequests);
    fota=false;
    EspDwinUpdatePoll(); assert(!panel.crcRequests); // FOTA cannot be bypassed just by phase IDLE
    imageState=ESP_OTA_IMG_NEW;
    EspDwinUpdatePoll(); assert(!panel.crcRequests);
    imageState=ESP_OTA_IMG_VALID;
    for(bool *busy:{&config,&history,&antennaBusy,&owner}) {
        *busy=true; EspDwinUpdatePoll(); assert(!panel.crcRequests); *busy=false;
    }
    storage=false; EspDwinUpdatePoll(); assert(!panel.crcRequests); storage=true;
    motor=false; EspDwinUpdatePoll(); assert(!panel.crcRequests); motor=true;
    EspDwinUpdatePoll();
    assert(!EspDwinUpdateHold() && !locked && stopped && panel.flashWrites==1 && !testDwinJournal);
    assert(!strcmp(EspDwinUpdateStatus(),"updated") && !strcmp(EspDwinUpdateStage(),"complete"));
    assert(health>1000); // heartbeat throughout UART pauses, never disable watchdog
    EspDwinUpdatePoll(); assert(panel.flashWrites==1);
    // Uncertain flash keeps the machine gated, and survives an ESP-only reboot.
    panel=PanelFake(); panel.lostFlashAck=true;
    EspDwinUpdateBegin(); EspDwinUpdatePoll();
    assert(EspDwinUpdateHold() && locked && stopped && testDwinJournal && panel.flashWrites==1);
    assert(!strcmp(EspDwinUpdateStatus(),"io_error"));
    panel.lostFlashAck=false;
    EspDwinUpdateBegin(); EspDwinUpdatePoll();
    assert(!EspDwinUpdateHold() && !testDwinJournal && panel.flashWrites==1 && panel.reboots==1);
    assert(!strcmp(EspDwinUpdateStatus(),"already_current"));
    // Wrong layout while a recovery journal exists must not be overwritten.
    panel=PanelFake(); panel.panelCrc=0x1234; panel.wrongLayout=true;
    testDwinJournal=dwinpatchdata::IMAGE.journalTag;
    EspDwinUpdateBegin(); EspDwinUpdatePoll();
    assert(EspDwinUpdateHold() && !panel.flashWrites && testDwinJournal);
    puts("PASS ESP DWIN boot adapter: waits for OTA validity and all owners, motor HOLD, watchdog service, durable journal, exclusive UART, once per boot");
}
