#include "motor_profile.h"

#include <stdint.h>
#include <stdio.h>

static unsigned int failure_count;

#define CHECK_EQUAL_U32(label, expected_value, actual_value)                                       \
    do {                                                                                           \
        uint32_t const check_expected = (expected_value);                                          \
        uint32_t const check_actual = (actual_value);                                              \
        if (check_expected != check_actual) {                                                      \
            (void)printf("FAIL %s: expected %lu, got %lu\n", (label),                              \
                         (unsigned long)check_expected, (unsigned long)check_actual);              \
            failure_count++;                                                                       \
        }                                                                                          \
    } while (0)

static uint32_t ReferenceLinearPeriod(uint32_t from_period_us, uint32_t to_period_us,
                                      uint32_t pulse, uint32_t pulse_total) {
    uint32_t period_range;

    if ((pulse_total == 0U) || (pulse >= pulse_total)) {
        return to_period_us;
    }

    if (from_period_us > to_period_us) {
        period_range = from_period_us - to_period_us;
        return from_period_us - (uint32_t)(((uint64_t)period_range * pulse) / pulse_total);
    }

    period_range = to_period_us - from_period_us;
    return from_period_us + (uint32_t)(((uint64_t)period_range * pulse) / pulse_total);
}

static uint32_t ReferenceTotalPulses(const MotorProfile *profile) {
    return profile->accel_pulses + profile->cruise_pulses + profile->decel_pulses;
}

static uint32_t ReferencePeriodUs(const MotorProfile *profile, uint32_t completed_pulses) {
    uint32_t accel_end;
    uint32_t cruise_end;
    uint32_t total;

    accel_end = profile->accel_pulses;
    cruise_end = accel_end + profile->cruise_pulses;
    total = cruise_end + profile->decel_pulses;

    if ((completed_pulses == 0U) || (completed_pulses >= total)) {
        return profile->start_period_us;
    }
    if (completed_pulses <= accel_end) {
        return ReferenceLinearPeriod(profile->start_period_us, profile->run_period_us,
                                     completed_pulses, profile->accel_pulses);
    }
    if (completed_pulses <= cruise_end) {
        return profile->run_period_us;
    }
    if (profile->decel_pulses != 0U) {
        return ReferenceLinearPeriod(profile->run_period_us, profile->start_period_us,
                                     completed_pulses - cruise_end, profile->decel_pulses);
    }
    return profile->run_period_us;
}

static void CheckEveryCount(const char *name, const MotorProfile *profile) {
    uint32_t completed;
    uint32_t total;

    total = ReferenceTotalPulses(profile);
    for (completed = 0U; completed <= total; completed++) {
        uint32_t expected;
        uint32_t actual;

        expected = ReferencePeriodUs(profile, completed);
        actual = MotorProfile_PeriodUs(profile, completed);
        if (actual != expected) {
            (void)printf("FAIL %s at completed=%lu: expected %lu, got %lu\n", name,
                         (unsigned long)completed, (unsigned long)expected, (unsigned long)actual);
            failure_count++;
        }
    }
}

int main(void) {
    static const MotorProfile close_profile = {2801U, 701U, 500U, 3500U, 2000U};
    static const MotorProfile open_profile = {2801U, 701U, 500U, 4000U, 600U};
    static const MotorProfile wide_accel_profile = {UINT32_C(4000000001), UINT32_C(1),
                                                    UINT32_C(4000000000), 0U, 0U};
    static const MotorProfile wide_decel_profile = {UINT32_C(4000000001), UINT32_C(1), 0U, 0U,
                                                    UINT32_C(4000000000)};

    CHECK_EQUAL_U32("close total", 6000U, MotorProfile_TotalPulses(&close_profile));
    CHECK_EQUAL_U32("open total", 5100U, MotorProfile_TotalPulses(&open_profile));

    CHECK_EQUAL_U32("close start period", 2801U, MotorProfile_PeriodUs(&close_profile, 0U));
    CHECK_EQUAL_U32("close first acceleration period", 2797U,
                    MotorProfile_PeriodUs(&close_profile, 1U));
    CHECK_EQUAL_U32("close acceleration midpoint", 1751U,
                    MotorProfile_PeriodUs(&close_profile, 250U));
    CHECK_EQUAL_U32("close run period", 701U, MotorProfile_PeriodUs(&close_profile, 500U));
    CHECK_EQUAL_U32("close cruise end", 701U, MotorProfile_PeriodUs(&close_profile, 4000U));
    CHECK_EQUAL_U32("close first deceleration period", 702U,
                    MotorProfile_PeriodUs(&close_profile, 4001U));
    CHECK_EQUAL_U32("close final emitted period", 2799U,
                    MotorProfile_PeriodUs(&close_profile, 5999U));
    CHECK_EQUAL_U32("close terminal period", 2801U, MotorProfile_PeriodUs(&close_profile, 6000U));
    CHECK_EQUAL_U32("close post-terminal period", 2801U,
                    MotorProfile_PeriodUs(&close_profile, UINT32_MAX));

    CHECK_EQUAL_U32("open start period", 2801U, MotorProfile_PeriodUs(&open_profile, 0U));
    CHECK_EQUAL_U32("open run period", 701U, MotorProfile_PeriodUs(&open_profile, 500U));
    CHECK_EQUAL_U32("open cruise end", 701U, MotorProfile_PeriodUs(&open_profile, 4500U));
    CHECK_EQUAL_U32("open first deceleration period", 704U,
                    MotorProfile_PeriodUs(&open_profile, 4501U));
    CHECK_EQUAL_U32("open final emitted period", 2797U,
                    MotorProfile_PeriodUs(&open_profile, 5099U));
    CHECK_EQUAL_U32("open terminal period", 2801U, MotorProfile_PeriodUs(&open_profile, 5100U));
    CHECK_EQUAL_U32("open post-terminal period", 2801U,
                    MotorProfile_PeriodUs(&open_profile, UINT32_MAX));

    CheckEveryCount("close profile", &close_profile);
    CheckEveryCount("open profile", &open_profile);

    /* These products exceed 32 bits.  They catch an implementation that omits
       the uint64_t promotion used by the original F103 interpolation. */
    CHECK_EQUAL_U32("wide acceleration interpolation", UINT32_C(1000000001),
                    MotorProfile_PeriodUs(&wide_accel_profile, UINT32_C(3000000000)));
    CHECK_EQUAL_U32("wide deceleration interpolation", UINT32_C(3000000001),
                    MotorProfile_PeriodUs(&wide_decel_profile, UINT32_C(3000000000)));

    if (failure_count != 0U) {
        (void)printf("motor_profile: %u failure(s)\n", failure_count);
        return 1;
    }

    (void)printf("motor_profile: PASS (close=6000, open=5100, "
                 "start=2801us, run=701us)\n");
    return 0;
}
