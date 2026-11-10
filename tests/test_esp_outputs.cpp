// Compile the production ESP motor backend against simulated driver APIs.
// No serial connection or real GPIO is used by this executable.
#include "Arduino.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "driver/ledc.h"
#include "BoardProfile.h"
#include "EspPlatform.h"
#include "SafetyConfig.h"
extern "C" {
#include "motor_pwm.h"
#include "app_config.h"
}
#include <assert.h>
#include <stdio.h>
static int pins[49], timerCount, servoPin;
static uint32_t now, duty, hz;
static FakeTimer timers[2];
void digitalWrite(uint8_t p, int level) {
    pins[p] = level;
}
void pinMode(uint8_t, int) {}
uint32_t millis() {
    return now;
}
void delayMicroseconds(uint32_t) {}
int gpio_get_level(gpio_num_t p) {
    return pins[p];
}
int gpio_set_level(gpio_num_t p, int level) {
    pins[p] = level;
    return 0;
}
esp_err_t gptimer_new_timer(const gptimer_config_t *c, gptimer_handle_t *t) {
    assert(c->resolution_hz == 2000000 && timerCount < 2);
    *t = &timers[timerCount++];
    return 0;
}
esp_err_t gptimer_register_event_callbacks(gptimer_handle_t t, const gptimer_event_callbacks_t *c,
                                           void *u) {
    t->callbacks = *c;
    t->context = u;
    return 0;
}
esp_err_t gptimer_enable(gptimer_handle_t) {
    return 0;
}
esp_err_t gptimer_start(gptimer_handle_t) {
    return 0;
}
esp_err_t gptimer_set_alarm_action(gptimer_handle_t t, const gptimer_alarm_config_t *a) {
    t->armed = a != nullptr;
    if (a)
        t->alarm = a->alarm_count;
    return 0;
}
esp_err_t gptimer_get_raw_count(gptimer_handle_t t, uint64_t *c) {
    *c = t->count;
    return 0;
}
esp_err_t ledc_stop(int, int, int) {
    duty = 0;
    pins[servoPin] = 0;
    return 0;
}
esp_err_t ledc_timer_config(const ledc_timer_config_t *c) {
    hz = c->freq_hz;
    return 0;
}
esp_err_t ledc_channel_config(const ledc_channel_config_t *c) {
    servoPin = c->gpio_num;
    duty = c->duty;
    return 0;
}
esp_err_t ledc_set_duty(int, int, uint32_t d) {
    duty = d;
    return 0;
}
esp_err_t ledc_update_duty(int, int) {
    return 0;
}
static void Edges(int axis, unsigned n) {
    FakeTimer &t = timers[axis];
    while (n-- && t.armed) {
        t.armed = false;
        t.count = t.alarm;
        gptimer_alarm_event_data_t event = {t.alarm, t.count};
        t.callbacks.on_alarm(&t, &event, t.context);
    }
}
int main() {
    assert(CONVEYOR_DIRECTION == 0U); // J2 forward is GPIO5 LOW on this installation.
    MotorProfile close = {934, 400, 500, 3500, 2000}, open = {934, 400, 500, 4000, 600};
    MotorPwm_Init();
    assert(EspMotorReady() && timerCount == 2 && servoPin == 6);
    assert(pins[9] == STEP_HOLD_LEVEL && pins[7] == STEP_HOLD_LEVEL);
    assert(MotorPwm_StartProfile(MOTOR_PWM_DRIVER_1, 0, &close, 0));
    assert(MotorPwm_StartProfile(MOTOR_PWM_DRIVER_2, 0, &close, 0));
    Edges(0, 246);
    Edges(1, 912);
    assert(MotorPwm_GetPulseCount(MOTOR_PWM_DRIVER_1) == 123);
    assert(MotorPwm_GetPulseCount(MOTOR_PWM_DRIVER_2) == 456);
    pins[41] = 1;
    Edges(1, 2);
    assert(MotorPwm_IsRunning(MOTOR_PWM_DRIVER_2));
    pins[41] = 0;
    pins[39] = 1;
    Edges(0, 1);
    Edges(1, 2);
    assert(!MotorPwm_IsRunning(MOTOR_PWM_DRIVER_1) && pins[48] == 0);
    assert(MotorPwm_IsRunning(MOTOR_PWM_DRIVER_2) &&
           MotorPwm_GetPulseCount(MOTOR_PWM_DRIVER_2) == 458);
    pins[1] = 1;
    Edges(1, 1);
    assert(!MotorPwm_IsRunning(MOTOR_PWM_DRIVER_2) && pins[8] == 0);
    // Logical OPEN must move away from an asserted close limit.
    assert(MotorPwm_StartProfile(MOTOR_PWM_DRIVER_1, 1, &open, 0));
    assert(MotorPwm_StartProfile(MOTOR_PWM_DRIVER_2, 1, &open, 0));
    Edges(0, 10200);
    Edges(1, 10200);
    assert(MotorPwm_GetPulseCount(MOTOR_PWM_DRIVER_1) == 5100);
    assert(MotorPwm_GetPulseCount(MOTOR_PWM_DRIVER_2) == 5100);
    assert(!MotorPwm_IsRunning(MOTOR_PWM_DRIVER_1) && !MotorPwm_IsRunning(MOTOR_PWM_DRIVER_2));
    assert(pins[48] == 0 && pins[8] == 0);
    pins[39] = pins[1] = 0;
    assert(MotorPwm_StartProfile(MOTOR_PWM_DRIVER_1, 0, &close, 0));
    assert(MotorPwm_StartProfile(MOTOR_PWM_DRIVER_2, 0, &close, 0));
    assert(MotorPwm_StartContinuousArr(CONVEYOR_DRIVER, CONVEYOR_DIRECTION, 29));
    assert(hz == 133333 && duty > 0 && pins[5] == CONVEYOR_DIRECTION);
    MotorPwm_Stop(CONVEYOR_DRIVER);
    assert(!duty && pins[6] == 0);
    assert(MotorPwm_StartContinuousArr(CONVEYOR_DRIVER, CONVEYOR_REVERSE_DIRECTION, 29));
    assert(hz == 133333 && duty > 0 && pins[5] == CONVEYOR_REVERSE_DIRECTION);
    MotorPwm_Stop(CONVEYOR_DRIVER);
    assert(!duty && pins[6] == 0);
    assert(MotorPwm_StartContinuousArr(CONVEYOR_DRIVER, CONVEYOR_DIRECTION, 29));
    assert(hz == 133333 && duty > 0 && pins[5] == CONVEYOR_DIRECTION);
    Edges(0, 1);
    Edges(1, 1);
    assert(pins[48] && pins[8]);
    MotorPwm_EmergencyStop();
    assert(EspEmergencyLatched() && !MotorPwm_IsRunning(MOTOR_PWM_DRIVER_1) &&
           !MotorPwm_IsRunning(MOTOR_PWM_DRIVER_2));
    assert(!MotorPwm_IsRunning(MOTOR_PWM_DRIVER_4) && duty == 0);
    assert(pins[48] == 0 && pins[8] == 0 && pins[6] == 0 && pins[9] == STEP_RELEASE_LEVEL &&
           pins[7] == STEP_RELEASE_LEVEL);
    assert(!MotorPwm_StartProfile(MOTOR_PWM_DRIVER_2, 0, &close, 0));
    assert(!MotorPwm_StartContinuousArr(CONVEYOR_DRIVER, CONVEYOR_DIRECTION, 29));
    MotorPwm_Init();
    assert(!EspEmergencyLatched());
    assert(MotorPwm_StartProfile(MOTOR_PWM_DRIVER_2, 0, &close, 0));
    now = 101;
    EspSafetyPoll();
    assert(EspEmergencyLatched() && !MotorPwm_IsRunning(MOTOR_PWM_DRIVER_2));
    assert(pins[9] == STEP_HOLD_LEVEL && pins[7] == STEP_HOLD_LEVEL);
    assert(MotorPwm_GetStopReason() == STOP_LOOP_WATCHDOG && !MotorPwm_IsReleased());
    assert(!MotorPwm_StartProfile(MOTOR_PWM_DRIVER_1, 0, &close, 0));
    MotorPwm_Init();
    EspControlAlive();
    assert(MotorPwm_StartProfile(MOTOR_PWM_DRIVER_1, 0, &close, 0));
    assert(MotorPwm_StartProfile(MOTOR_PWM_DRIVER_2, 0, &close, 0));
    assert(MotorPwm_StartContinuousArr(CONVEYOR_DRIVER, CONVEYOR_DIRECTION, 29));
    MotorPwm_SafetyStop(STOP_UART);
    assert(!MotorPwm_IsRunning(MOTOR_PWM_DRIVER_1) && !MotorPwm_IsRunning(MOTOR_PWM_DRIVER_2) &&
           !duty);
    assert(pins[9] == STEP_HOLD_LEVEL && pins[7] == STEP_HOLD_LEVEL);
    MotorPwm_EmergencyStop();
    MotorPwm_SafetyStop(STOP_UART);
    EspSafetyPoll();
    assert(pins[9] == STEP_RELEASE_LEVEL && pins[7] == STEP_RELEASE_LEVEL && MotorPwm_IsReleased());
    assert(MotorPwm_GetStopReason() == STOP_EMERGENCY);
    MotorPwm_Init();
    assert(!MotorPwm_IsReleased() && !MotorPwm_GetStopReason());
    // Exercise the SAME authorization called by the real USB worker, not the
    // reader transport mock: claim HOLD may scan without unlocking any motor.
    MotorPwm_SafetyStop(STOP_PC_TAKEOVER);
    uint32_t beforeClaimScan = EspRfStartToken();
    assert(!EspRfStartAllowed(beforeClaimScan));
    assert(EspRfAuthorizeStandalone(beforeClaimScan));
    uint32_t scanToken = EspRfStartToken();
    assert(EspRfStartAllowed(scanToken) && !EspRfStartAllowed(beforeClaimScan));
    assert(EspEmergencyLatched());
    assert(!MotorPwm_StartProfile(MOTOR_PWM_DRIVER_1, 0, &close, 0));
    assert(!MotorPwm_StartContinuousArr(CONVEYOR_DRIVER, CONVEYOR_DIRECTION, 29));
    assert(pins[9] == STEP_HOLD_LEVEL && pins[7] == STEP_HOLD_LEVEL);
    MotorPwm_SafetyStop(STOP_PC_DISCONNECT);
    assert(!EspRfStartAllowed(scanToken));
    assert(!EspRfAuthorizeStandalone(scanToken)); // STOP between check and grant
    assert(EspRfAuthorizeStandalone(EspRfStartToken()));
    uint32_t newToken = EspRfStartToken();
    assert(EspRfStartAllowed(newToken) && !EspRfStartAllowed(scanToken));
    EspRfRevoke(); // Scan OFF invalidates a START still queued in the worker
    assert(!EspRfStartAllowed(newToken));
    assert(EspRfAuthorizeStandalone(EspRfStartToken()));
    newToken = EspRfStartToken();
    MotorPwm_EmergencyStop();
    assert(!EspRfStartAllowed(newToken));
    assert(!EspRfAuthorizeStandalone(EspRfStartToken()));
    MotorPwm_Init();
    assert(EspRfStartAllowed(EspRfStartToken()) && !EspRfStartAllowed(newToken));
    puts("PASS real ESP output backend: faults HOLD both EN, Emergency RELEASE sticky until Reset; "
         "J6/J3 independent timers and limits, J2 forward DIR LOW / Rescan reverse DIR HIGH at "
         "133333 Hz, exact two-door opening count, all pulse outputs stop and both EN release, "
         "explicit reset, watchdog");
}
