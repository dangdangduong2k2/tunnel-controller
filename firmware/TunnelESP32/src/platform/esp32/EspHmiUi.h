#pragma once
// DWIN HMI v2 settings/history UI on the ESP32. Control (Arduino) task only: the
// same task that parses the panel UART and runs the machine state machine.
// remoteOwner: a Modbus/MQTT/PC session currently owns control.
// active: a motor is running, a scan is active or a reader transaction is pending
// (the facts EspModbusPoll already collects for the configuration gate).
void EspHmiUiPoll(bool remoteOwner, bool ethernetReady, bool active);
// True when nothing is active and the cycle is at rest (or halted in a fault).
bool EspMachineQuiet(bool active);
