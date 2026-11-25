#pragma once
#include <stdint.h>
#ifdef __cplusplus
#include "../../config/DeviceConfig.h"
const cfg::Settings &EspConfigGet();
void EspConfigBegin();
void EspConfigWebBegin();
bool EspConfigStorageReady();
void EspConfigPoll(const cfg::Safety &safety);
void EspConfigSetFallback(bool value);
// Local DWIN panel save. Uses the same job as the web handler (STOP fence, NVS
// write on the storage task, restart). No admin password is required at the
// panel and the admin account (flag, salt, hash) is always kept as stored.
// Returns 0 accepted, 1 invalid settings, 2 storage unavailable, 3 a job is in progress.
uint8_t EspConfigSubmitLocal(const cfg::Settings &next);
// 0 idle, 1 requested/saving, 2 denied (machine busy), 3 save failed, 4 saved, restarting.
uint8_t EspConfigApplyState();
extern "C" {
#endif
uint8_t EspConfigBusy(void);
void EspConfigFenced(void);
#ifdef __cplusplus
}
#endif
