#pragma once
// Hardware pins only. Configure communication at http://<device-ip>/.
// Factory defaults and persistent schema are in DeviceConfig.cpp/h.
// Schematic 2026-10-19: SP3485 and W5500.
#define MODBUS_RX 16
#define MODBUS_TX 18
#define MODBUS_DE 17
#define MODBUS_SCK 13
#define MODBUS_MISO 12
#define MODBUS_MOSI 11
#define MODBUS_CS 14
#define MODBUS_IRQ 21
#define MODBUS_RST 47
