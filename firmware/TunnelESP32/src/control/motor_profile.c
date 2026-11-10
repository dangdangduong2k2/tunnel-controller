#include "motor_profile.h"
#include <stddef.h>

static uint32_t MotorProfile_LinearPeriod(uint32_t from, uint32_t to, uint32_t pulse,
                                          uint32_t total) {
    uint32_t range;

    if ((total == 0U) || (pulse >= total)) {
        return to;
    }
    if (from > to) {
        range = from - to;
        return from - (uint32_t)(((uint64_t)range * pulse) / total);
    }
    range = to - from;
    return from + (uint32_t)(((uint64_t)range * pulse) / total);
}

uint32_t MotorProfile_TotalPulses(const MotorProfile *profile) {
    uint64_t total;

    if (profile == NULL) {
        return 0U;
    }
    total = (uint64_t)profile->accel_pulses + profile->cruise_pulses + profile->decel_pulses;
    return (total > UINT32_MAX) ? 0U : (uint32_t)total;
}

uint32_t MotorProfile_PeriodUs(const MotorProfile *profile, uint32_t completed_pulses) {
    uint32_t decel_start;

    if ((profile == NULL) || (MotorProfile_TotalPulses(profile) == 0U)) {
        return 0U;
    }
    if (completed_pulses <= profile->accel_pulses) {
        return MotorProfile_LinearPeriod(profile->start_period_us, profile->run_period_us,
                                         completed_pulses, profile->accel_pulses);
    }
    decel_start = profile->accel_pulses + profile->cruise_pulses;
    if ((completed_pulses <= decel_start) || (profile->decel_pulses == 0U)) {
        return profile->run_period_us;
    }
    return MotorProfile_LinearPeriod(profile->run_period_us, profile->start_period_us,
                                     completed_pulses - decel_start, profile->decel_pulses);
}
