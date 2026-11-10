#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void EspModbusBegin(void);
uint8_t EspModbusRtuReady(void);
void EspModbusPoll(void);
void EspModbusFence(void);
uint8_t EspModbusActive(void);
uint32_t EspModbusControlLeaseMs(void);
uint8_t EspModbusLegacyAllow(uint8_t command);
uint8_t EspModbusLegacyOwns(void);
void EspModbusLegacyRelease(void);
#ifdef __cplusplus
}
#endif
