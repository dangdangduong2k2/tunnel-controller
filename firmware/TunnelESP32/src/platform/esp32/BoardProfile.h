#pragma once
#include <stdint.h>

// Verified against Schematic_2026-10-19_MachDieuKhienLEDMatrix.PDF.
// Physical connector order for J3/J6: 1:+5V 2:EN 3:DIR 4:PUL 5:GND.
// DWIN uses the existing P1 UART0 RX44/TX43. No pin rerouting.
// J5 also carries PC commands in @TUN: envelopes. CH340 TX via R29 shares
// RX44 electrically; protocol framing does not isolate competing outputs.
namespace board {
// User confirmed labels P6/P3/P2 correspond to schematic J6/J3/J2.
// Two TB6600 door axes, followed by the existing PUL/DIR conveyor servo.
static const uint8_t GATE_EN[2] = {9, 7}, GATE_DIR[2] = {10, 15}, GATE_PUL[2] = {48, 8};
static const uint8_t GATE_LIMIT[2] = {39, 1};                           // IN2 and IN7 (J14)
static const uint8_t SERVO_EN_UNUSED = 4, SERVO_DIR = 5, SERVO_PUL = 6; // J2
static const uint8_t INPUT_PINS[7] = {38, 39, 40, 41, 42, 2, 1};
static const uint8_t UART_RX = 44, UART_TX = 43;
static const uint8_t LCD_RX = UART_RX, LCD_TX = UART_TX;
static const uint8_t USB_DM = 19, USB_DP = 20;
static const uint8_t LEFT = 1, DOOR_CLOSED = 2, RIGHT = 4, DOOR2_CLOSED = 8;
} // namespace board
