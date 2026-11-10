#pragma once
#include <stdint.h>
#ifdef __cplusplus
#include "../../config/DeviceConfig.h"
const cfg::Settings &EspConfigGet();
void EspConfigBegin();
void EspConfigWebBegin();
void EspConfigPoll(const cfg::Safety &safety);
void EspConfigSetFallback(bool value);
extern "C" {
#endif
uint8_t EspConfigBusy(void);
void EspConfigFenced(void);
#ifdef __cplusplus
}
#endif
