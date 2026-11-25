#pragma once
#include "../../hmi/AntennaSettings.h"
bool EspHmiAntennaBusy();
bool EspHmiAntennaStart(const antenna::Values *values, bool save);
const antenna::State &EspHmiAntennaState();
void EspHmiAntennaPoll();
void EspHmiAntennaCancel();
