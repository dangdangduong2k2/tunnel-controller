#pragma once
// Owned by the Arduino control task; asynchronous fault flags are atomic.
bool EspRuntimeBegin();
bool EspRuntimeReady();
void EspRuntimeDisable();
void EspFlushPcTx();
