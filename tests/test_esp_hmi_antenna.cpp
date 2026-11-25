// Real local ownership adapter + transaction engine, simulated machine/reader I/O.
#include "EspHmiAntenna.h"
#include "EspPlatform.h"
#include "EspConfig.h"
#include "EspFota.h"
#include "EspHistory.h"
#include "tunnel.h"
#include "motor_pwm.h"
#include "hmi.h"
#include "zk7182.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
static uint32_t clockMs=0, token=1;
static bool remote=false, remotePending=false, legacyPending=false, config=false, fota=false, flash=false, moving=false, scan=false,
    inventory=false, pending=false, ready=true, controlLock=false, maintenance=false, gotReply=false;
static TunnelState state=TUNNEL_STARTUP_WAIT;
static unsigned heartbeats=0, requests=0;
static uint8_t lastCommand;
uint32_t millis() { return clockMs; }
uint32_t EspRfStartToken() { return token; }
bool EspHistoryWriting() { return flash; }
extern "C" {
uint8_t EspModbusActive() { return remote; }
uint8_t EspModbusReaderBusy() { return remotePending; }
uint8_t PcControl_IsReaderBusy() { return legacyPending; }
uint8_t EspConfigBusy() { return config; }
uint8_t EspFotaBusy() { return fota; }
uint8_t MotorPwm_IsRunning(MotorPwmId) { return moving; }
uint8_t ZK7182_IsScanActive() { return scan; }
uint8_t ZK7182_IsInventoryRunning() { return inventory; }
uint8_t ZK7182_IsInventoryResponsePending() { return pending; }
uint8_t ZK7182_IsReady() { return ready; }
TunnelState Tunnel_GetState() { return state; }
uint8_t Hmi_IsControlLocked() { return controlLock; }
void Hmi_SetControlLocked(uint8_t value) { controlLock=value!=0; }
uint8_t Tunnel_IsPcMaintenance() { return maintenance; }
void Tunnel_PcHeartbeat() { ++heartbeats; }
uint8_t Tunnel_RemoteCommand(uint8_t command,uint16_t value) {
    assert(command==0x20); maintenance=value!=0; return 0;
}
uint8_t ZK7182_BeginPcCommand(uint8_t command,const uint8_t*,uint8_t) {
    assert(maintenance && controlLock && !remote && !moving && !scan);
    lastCommand=command; ++requests; ready=false; return 1;
}
uint8_t ZK7182_TakePcReply(uint8_t *b,uint8_t *n,uint8_t *s) {
    if (!gotReply) return 0;
    gotReply=false; ready=true; *s=0;
    memset(b,0,14); b[0]=lastCommand;
    *n=lastCommand==0x94?6:lastCommand==0x21?14:2;
    if (lastCommand==0x94) for (unsigned i=2;i<6;++i) b[i]=22;
    if (lastCommand==0x21) b[10]=15;
    return 1;
}
}
static void poll() { ++clockMs; EspHmiAntennaPoll(); }
int main() {
    for (bool *guard : {&remote,&remotePending,&legacyPending,&config,&fota,&flash,&moving,&scan,&inventory,&pending,&controlLock,&maintenance}) {
        *guard=true; assert(!EspHmiAntennaStart(nullptr,true)); *guard=false;
    }
    ready=false; assert(!EspHmiAntennaStart(nullptr,true)); ready=true;
    state=TUNNEL_WAIT_CONFIRM; assert(!EspHmiAntennaStart(nullptr,true));
    for (TunnelState quiet : {TUNNEL_STARTUP_WAIT,TUNNEL_STOPPED,TUNNEL_FAULT,TUNNEL_MANUAL,TUNNEL_WAIT_REARM}) {
        state=quiet; assert(EspHmiAntennaStart(nullptr,true));
        assert(maintenance && controlLock && EspHmiAntennaBusy());
        assert(!EspHmiAntennaStart(nullptr,true));
        poll(); assert(lastCommand==0x94); gotReply=true; poll();
        poll(); assert(lastCommand==0x21); gotReply=true; poll();
        assert(EspHmiAntennaState().valid && !EspHmiAntennaBusy() && !controlLock && !maintenance);
    }
    antenna::Values v={{22,22,22,22},15};
    for (unsigned event=0;event<3;++event) {
        unsigned before=requests;
        assert(EspHmiAntennaStart(&v,true)); poll(); assert(lastCommand==0x2f);
        if (event==0) ++token; // STOP changed generation while Set was in flight
        if (event==1) EspHmiAntennaCancel();
        if (event==2) moving=true;
        poll(); assert(EspHmiAntennaBusy() && maintenance && controlLock);
        gotReply=true; poll();
        assert(!EspHmiAntennaBusy() && !maintenance && !controlLock);
        assert(EspHmiAntennaState().result==antenna::CANCELLED && requests==before+1);
        moving=false;
    }
    assert(heartbeats>0);
    puts("PASS local antenna owner: startup/stopped/fault, remote/motion/flash/OTA exclusion, STOP cancellation, reply drain, HMI lock release");
}
