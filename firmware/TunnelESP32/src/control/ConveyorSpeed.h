#pragma once
#include <stdint.h>

/* 4 MHz legacy timer units: period_ns = (ARR + 1) * 250.
 * Keep five increasing levels within the 200 kHz rate already usable on the
 * installed conveyor. Previously levels 4/5 demanded 400/800 kHz.
 * This is pulse frequency, not a calibrated belt speed in m/s. */
static inline uint16_t ConveyorSpeed_Arr(uint8_t level) {
    static const uint16_t values[5] = {41U, 35U, 29U, 24U, 19U};
    return values[(level >= 1U && level <= 5U) ? level - 1U : 0U];
}
