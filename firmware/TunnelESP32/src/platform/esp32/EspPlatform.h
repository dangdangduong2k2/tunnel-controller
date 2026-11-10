#pragma once
#include <stdint.h>
#include "../../common/StopReason.h"
#ifdef __cplusplus
extern "C" {
#endif
void EspUsbBegin(void);
void EspUsbPoll(void);
void EspMotorsBegin(void);
void EspSafetyPoll(void);
void EspControlAlive(void);
uint8_t EspEmergencyHeld(void);
uint8_t EspEmergencyLatched(void);
uint8_t EspMotorReady(void);
void EspPcTransmit(const uint8_t *, uint16_t);
void EspPcPoll(void);
void EspLog(const char *);
uint8_t EspHmiTransmit(const uint8_t *, uint16_t);
void EspHmiCapture(void);
void EspHmiEmergency(void);
void EspRequestEmergency(void);
void EspRequestSafetyStop(uint8_t reason);
uint8_t EspResetReason(void);
uint8_t EspTakeEmergencyRequest(void);
void EspFenceSerialInput(void);
void EspPollSensorEdges(void);
#ifdef __cplusplus
}
#endif
