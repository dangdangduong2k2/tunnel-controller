#pragma once
#include "../../protocol/modbus/ModbusCore.h"
void EspMqttBegin(mb::Core &core);
void EspMqttPoll();
void EspMqttFence();
bool EspMqttConnected();
// Called by the MQTT sender task only.
void EspMqttFotaPublish(bool ack, const char *data);
const char *EspMqttSession();
uint32_t EspMqttEpoch();
