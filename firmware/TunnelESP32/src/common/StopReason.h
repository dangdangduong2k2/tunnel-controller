#pragma once
/* Stable wire values: status byte 24. Only STOP_EMERGENCY releases EN. */
enum {
    STOP_NONE = 0,
    STOP_EMERGENCY = 1,
    STOP_UART = 2,
    STOP_BAD_PACKET = 3,
    STOP_RX_OVERFLOW = 4,
    STOP_TX_QUEUE = 5,
    STOP_PC_TIMEOUT = 6,
    STOP_LOOP_WATCHDOG = 7,
    STOP_MOTOR = 8,
    STOP_INITIALIZATION = 9,
    STOP_PC_DISCONNECT = 10,
    STOP_PC_TAKEOVER = 11
};
