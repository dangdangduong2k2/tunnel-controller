#pragma once
#include <stdint.h>
extern "C" {
#include "../../control/motor_profile.h"
}
// Used directly by the GPTimer ISR and host tests. One edge per call.
struct StepSequence {
    bool running = false, high = false, profiled = false;
    uint8_t dir = 0;
    uint32_t count = 0, target = 0, period = 934;
    MotorProfile profile = {0, 0, 0, 0, 0};
    void stop() {
        running = false;
        high = false;
    }
    uint32_t edge() {
        if (!running)
            return 0;
        if (high) {
            high = false;
            ++count;
            if (target && count >= target) {
                running = false;
                return 0;
            }
        } else {
            if (profiled)
                period = MotorProfile_PeriodUs(&profile, count);
            high = true;
        }
        return period; // Half-period ticks, on the 2 MHz timer.
    }
};
