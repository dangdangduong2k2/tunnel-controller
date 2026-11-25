#pragma once
#include <stdint.h>
#include "../../app/History.h"
// Finalized-result history on the ESP32: NVS persistence, wall clock (SNTP) and the
// tunnel finalize hook. Begin/Poll/Log are called on the control (Arduino) task only.
void EspHistoryBegin();
// quiet: no motor, scan or reader transaction is active and none is about to start.
// Flash is written only then, by a background task, one small chunk at a time.
void EspHistoryPoll(bool quiet, bool hasIp);
const hist::Log &EspHistoryLog();
// A chunk is being written right now (the NVS lock may be held for a while).
bool EspHistoryWriting();
// Unix time (UTC), or 0 until SNTP has set the clock.
uint32_t EspTimeNow();
