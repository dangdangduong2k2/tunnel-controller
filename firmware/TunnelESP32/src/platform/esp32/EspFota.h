#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
// MQTT callback only queues. No machine/flash/network work in callback.
void EspFotaBegin();
void EspFotaReceive(const char *data, size_t length, bool retained);
void EspFotaGet();
void EspFotaPoll(bool safe, const char *session, uint32_t epoch);
void EspFotaHealth(bool healthy);
void EspFotaTransmit();
extern "C" {
#endif
uint8_t EspFotaBusy(void);
void EspFotaFence(void);
void EspFotaCancel(void);
#ifdef __cplusplus
}
#endif
