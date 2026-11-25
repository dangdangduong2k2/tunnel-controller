#pragma once
#include <stdint.h>
#ifdef __cplusplus
// Boot migration; normal control is inhibited while waiting for ESP OTA health.
void EspDwinUpdateBegin();
void EspDwinUpdatePoll();
const char *EspDwinUpdateStatus();
const char *EspDwinUpdateStage();
const char *EspDwinUpdateNotice();
// Uncertain panel flash: keep motion disabled until a later boot repairs/verifies.
extern "C" {
#endif
uint8_t EspDwinUpdateHold(void);
#ifdef __cplusplus
}
#endif
