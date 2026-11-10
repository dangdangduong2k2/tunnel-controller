#ifndef MOTOR_PROFILE_H
#define MOTOR_PROFILE_H

#include <stdint.h>

typedef struct {
    uint32_t start_period_us;
    uint32_t run_period_us;
    uint32_t accel_pulses;
    uint32_t cruise_pulses;
    uint32_t decel_pulses;
} MotorProfile;

/* Zero denotes an invalid/empty profile or a sum that does not fit uint32_t. */
uint32_t MotorProfile_TotalPulses(const MotorProfile *profile);
/* Period of the next pulse after completed_pulses, with the F103 integer
 * interpolation and phase boundaries. Period values are actual microseconds. */
uint32_t MotorProfile_PeriodUs(const MotorProfile *profile, uint32_t completed_pulses);

#endif /* MOTOR_PROFILE_H */
