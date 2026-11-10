#pragma once
#include "../../protocol/modbus/ModbusCore.h"
void EspMqttBegin(mb::Core &core);
void EspMqttPoll();
void EspMqttFence();
bool EspMqttConnected();
