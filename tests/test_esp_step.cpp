#include <stdio.h>
#include <stdlib.h>
#include <initializer_list>
#include "StepSequence.h"
#include "app_config.h"
#include "BoardProfile.h"
#include "SafetyConfig.h"
static unsigned checks = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static StepSequence start(const MotorProfile &p, uint32_t done = 0) {
    StepSequence s;
    s.running = true;
    s.profiled = true;
    s.profile = p;
    s.target = MotorProfile_TotalPulses(&p);
    s.count = done;
    return s;
}
int main() {
    MotorProfile close = {STEP_START_PERIOD_US, STEP_RUN_PERIOD_US, STEP_CLOSE_ACCEL_PULSES,
                          STEP_CLOSE_CRUISE_PULSES, STEP_CLOSE_DECEL_PULSES};
    MotorProfile open = {STEP_START_PERIOD_US, STEP_RUN_PERIOD_US, STEP_OPEN_ACCEL_PULSES,
                         STEP_OPEN_CRUISE_PULSES, STEP_OPEN_DECEL_PULSES};
    CHECK(close.start_period_us == 934 && close.run_period_us == 400);
    CHECK(MotorProfile_TotalPulses(&close) == 6000 && MotorProfile_TotalPulses(&open) == 5100);
    for (const MotorProfile *p : {&close, &open}) {
        StepSequence s = start(*p);
        uint32_t previous = 934, rising = 0, falling = 0;
        while (s.running) {
            uint32_t done = s.count;
            uint32_t highTicks = s.edge();
            CHECK(s.high);
            ++rising;
            CHECK(highTicks >= 400 && highTicks <= 934);
            if (done <= p->accel_pulses)
                CHECK(highTicks <= previous);
            else if (done <= p->accel_pulses + p->cruise_pulses)
                CHECK(highTicks == 400);
            else
                CHECK(highTicks >= previous);
            previous = highTicks;
            uint32_t lowTicks = s.edge();
            CHECK(!s.high && s.count == done + 1);
            ++falling;
            if (s.running)
                CHECK(highTicks == lowTicks);
        }
        CHECK(rising == s.target && falling == s.target && s.count == s.target);
        CHECK(s.edge() == 0 && !s.high);
    }
    StepSequence s = start(close);
    for (unsigned i = 0; i < 234; i++) {
        s.edge();
        s.edge();
    }
    s.stop();
    CHECK(!s.running && !s.high && s.count == 234);
    CHECK(s.edge() == 0 && s.count == 234); // No queued pulse after STOP.
    s = start(close, s.count);
    CHECK(s.edge() == MotorProfile_PeriodUs(&close, 234));
    s.stop();
    CHECK(!s.high && !s.running); // STOP during a HIGH pulse.
    CHECK(board::GATE_PUL[0] == 48 && board::GATE_DIR[0] == 10 && board::GATE_EN[0] == 9);
    CHECK(board::GATE_PUL[1] == 8 && board::GATE_DIR[1] == 15 && board::GATE_EN[1] == 7);
    CHECK(board::GATE_LIMIT[0] == 39 && board::GATE_LIMIT[1] == 1);
    CHECK(board::SERVO_PUL == 6 && board::SERVO_DIR == 5);
    CHECK(STEP_RELEASE_LEVEL == 1 && STEP_HOLD_LEVEL == 0);
    printf("PASS %u checks: exact 6000/5100 pulses, acceleration/cruise/deceleration, equal pulse "
           "halves, stop/resume, EN mapping\n",
           checks);
}
