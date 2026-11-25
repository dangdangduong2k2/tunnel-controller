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
/* USB START authorization is separate from the motor HOLD latch. Every STOP
 * invalidates queued START tokens, including across a later explicit scan. */
uint32_t EspRfStartToken(void);
uint8_t EspRfAuthorizeStandalone(uint32_t expected);
void EspRfRevoke(void);
uint8_t EspRfStartAllowed(uint32_t token);
void EspPcTransmit(const uint8_t *, uint16_t);
void EspPcPoll(void);
void EspLog(const char *);
uint8_t EspHmiTransmit(const uint8_t *, uint16_t);
/* Panel UART transmitter: completely idle; free queue bytes; a PC reply is waiting. */
uint8_t EspHmiTxIdle(void);
uint16_t EspHmiTxFree(void);
uint8_t EspPcTxPending(void);
void EspHmiCapture(void);
void EspHmiEmergency(void);
void EspRequestEmergency(void);
void EspRequestSafetyStop(uint8_t reason);
/* OTA's own STOP fence; external stops still cancel OTA. */
void EspRequestUpdateFence(void);
uint8_t EspResetReason(void);
uint8_t EspTakeEmergencyRequest(void);
void EspFenceSerialInput(void);
void EspPollSensorEdges(void);
uint8_t EspModbusReaderBusy(void);
#ifdef __cplusplus
}
#endif
