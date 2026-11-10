#include "ModbusCore.h"
#include "EspModbus.h"
#include "EspPlatform.h"
extern "C" {
#include "tunnel.h"
#include "../firmware/TunnelESP32/src/reader/zk7182.h"
#include "stm32f4xx_hal.h"
}
#include <cstring>
#include <cassert>
static bool pendingStop = false;
static bool configBusy = false;
extern "C" uint8_t EspConfigBusy(void) {
    return configBusy;
}
extern "C" void MbTest_ConfigBusy(uint8_t value) {
    configBusy = value != 0;
}
static uint16_t readStatus(uint16_t a) {
    if (a == 2)
        return Tunnel_GetState();
    if (a == 3)
        return Tunnel_GetFault();
    return 0;
}
static void stop(bool emergency) {
    pendingStop = true;
    if (emergency)
        EspRequestEmergency();
    else
        EspRequestSafetyStop(STOP_PC_DISCONNECT);
}
static uint8_t command(uint8_t c, uint16_t v) {
    uint8_t r = Tunnel_RemoteCommand(c, v);
    if (c == 0x22 && v == 1 && !r) {
        pendingStop = true;
        EspRequestSafetyStop(STOP_PC_TAKEOVER);
    }
    return r;
}
static bool readerBegin(uint8_t c, const uint8_t *p, uint8_t n) {
    return Tunnel_IsPcMaintenance() && ZK7182_IsReady() && ZK7182_BeginPcCommand(c, p, n);
}
static bool readerReply(uint8_t *p, uint8_t *n, uint8_t *s) {
    return ZK7182_TakePcReply(p, n, s) != 0;
}
static mb::Backend backend = {readStatus,         command, readerBegin, readerReply,
                              Tunnel_PcHeartbeat, stop};
static mb::Core core(backend);
static uint8_t queue[8][22], lengths[8], senders[8], head, tail, reply[260];
extern "C" void MbTest_Init(void) {
    core = mb::Core(backend);
    head = tail = 0;
    pendingStop = false;
    configBusy = false;
}
extern "C" void EspModbusPoll(void) {
    core.tick(HAL_GetTick());
    if (configBusy || pendingStop || head == tail)
        return;
    core.pdu(senders[head], queue[head], lengths[head], reply);
    head = (head + 1) % 8;
}
extern "C" void EspModbusFence(void) {
    pendingStop = false;
    head = tail;
}
extern "C" uint8_t EspModbusActive(void) {
    return core.active();
}
extern "C" uint8_t EspModbusLegacyAllow(uint8_t c) {
    if (configBusy && c != 0x17)
        return 0;
    core.tick(HAL_GetTick());
    return (!pendingStop || c == 0x17) && core.legacyAllow(c);
}
extern "C" uint8_t EspModbusLegacyOwns(void) {
    return core.owns(mb::LEGACY);
}
extern "C" void EspModbusLegacyRelease(void) {
    core.disconnect(mb::LEGACY);
}
extern "C" void MbTest_Queue(uint8_t peer, const uint8_t *p, uint8_t n) {
    assert(n <= 22 && (tail + 1) % 8 != head);
    memcpy(queue[tail], p, n);
    lengths[tail] = n;
    senders[tail] = peer;
    tail = (tail + 1) % 8;
}
extern "C" uint16_t MbTest_Reg(uint16_t a) {
    uint8_t q[] = {4, uint8_t(a >> 8), uint8_t(a), 0, 1}, r[260];
    assert(core.pdu(1, q, 5, r) == 4);
    return uint16_t(r[2]) * 256 + r[3];
}
extern "C" uint8_t MbTest_Exception(void) {
    return reply[0] & 0x80 ? reply[1] : 0;
}
