#include <Arduino.h>
#include <atomic>
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "driver/ledc.h"
#include "freertos/semphr.h"
#include "BoardProfile.h"
#include "../../config/SafetyConfig.h"
#include "EspPlatform.h"
#include "StepSequence.h"
extern "C" {
#include "../../control/motor_pwm.h"
}

static portMUX_TYPE stepLock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t outputLock;
struct GateAxis {
    gptimer_handle_t timer = nullptr;
    StepSequence step;
    uint8_t index = 0;
};
static GateAxis gates[2];
static const uint8_t directionInvert[2] = {GATE1_DIRECTION_INVERT, GATE2_DIRECTION_INVERT};
static int axisIndex(MotorPwmId motor) {
    return motor == MOTOR_PWM_DRIVER_1 ? 0 : motor == MOTOR_PWM_DRIVER_2 ? 1 : -1;
}
static std::atomic<bool> emergency(false), ready(false), beltRunning(false);
static std::atomic<bool> released(false);
static std::atomic<uint8_t> stopReason(STOP_NONE);
static std::atomic<uint32_t> alive(0), beltPeriod(0);
static bool initialized = false;

extern "C" uint8_t EspEmergencyHeld(void) {
#if EMERGENCY_GPIO >= 0
    return gpio_get_level((gpio_num_t)EMERGENCY_GPIO) == EMERGENCY_ACTIVE_LEVEL;
#else
    return 0;
#endif
}
extern "C" uint8_t EspEmergencyLatched(void) {
    return emergency.load();
}
extern "C" uint8_t EspMotorReady(void) {
    return ready.load();
}
extern "C" uint8_t MotorPwm_GetStopReason(void) {
    return stopReason.load();
}
extern "C" uint8_t MotorPwm_IsReleased(void) {
    return released.load();
}
static void latchFault(uint8_t reason) {
    uint8_t empty = STOP_NONE;
    stopReason.compare_exchange_strong(empty, reason);
    empty = STOP_PC_TAKEOVER;
    stopReason.compare_exchange_strong(empty, reason);
    emergency = true;
}

static bool onAlarm(gptimer_handle_t t, const gptimer_alarm_event_data_t *event, void *context) {
    GateAxis &axis = *static_cast<GateAxis *>(context);
    StepSequence &step = axis.step;
    uint8_t i = axis.index;
    portENTER_CRITICAL_ISR(&stepLock);
    if (EspEmergencyHeld()) {
        released = true;
        stopReason = STOP_EMERGENCY;
        emergency = true;
    }
    if (emergency.load() || !step.running ||
        (step.dir == 0 && gpio_get_level((gpio_num_t)board::GATE_LIMIT[i]))) {
        step.stop();
        gpio_set_level((gpio_num_t)board::GATE_PUL[i], 0);
        if (emergency.load())
            gpio_set_level((gpio_num_t)board::GATE_EN[i],
                           released ? STEP_RELEASE_LEVEL : STEP_HOLD_LEVEL);
        portEXIT_CRITICAL_ISR(&stepLock);
        return false;
    }
    uint32_t halfTicks = step.edge();
    gpio_set_level((gpio_num_t)board::GATE_PUL[i], step.high);
    if (!step.running) {
        portEXIT_CRITICAL_ISR(&stepLock);
        return false;
    }
    // 2 MHz counter: one half-period is exactly 'period_us' ticks.
    gptimer_alarm_config_t next = {};
    next.alarm_count = event->alarm_value + halfTicks;
    if (next.alarm_count <= event->count_value)
        next.alarm_count = event->count_value + halfTicks;
    if (gptimer_set_alarm_action(t, &next) != ESP_OK) {
        latchFault(STOP_MOTOR);
        step.running = false;
        step.high = false;
        gpio_set_level((gpio_num_t)board::GATE_PUL[i], 0);
        gpio_set_level((gpio_num_t)board::GATE_EN[i],
                       released ? STEP_RELEASE_LEVEL : STEP_HOLD_LEVEL);
    }
    portEXIT_CRITICAL_ISR(&stepLock);
    return false;
}
static void stopStep(unsigned i) {
    StepSequence &step = gates[i].step;
    gptimer_handle_t timer = gates[i].timer;
    portENTER_CRITICAL(&stepLock);
    step.stop();
    gpio_set_level((gpio_num_t)board::GATE_PUL[i], 0);
    if (timer)
        gptimer_set_alarm_action(timer, nullptr);
    portEXIT_CRITICAL(&stepLock);
}
extern "C" void MotorPwm_Stop(MotorPwmId motor) {
    if (!outputLock)
        return;
    xSemaphoreTake(outputLock, portMAX_DELAY);
    int i = axisIndex(motor);
    if (i >= 0)
        stopStep((unsigned)i);
    if (motor == MOTOR_PWM_DRIVER_4) {
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, 0);
        beltRunning = false;
    }
    xSemaphoreGive(outputLock);
}
static void stopOutputs(void) {
    if (outputLock)
        xSemaphoreTake(outputLock, portMAX_DELAY);
    for (unsigned i = 0; i < 2; i++)
        stopStep(i);
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, 0);
    beltRunning = false;
    // A fault after a genuine Emergency must NOT re-energize the doors.
    for (unsigned i = 0; i < 2; i++)
        gpio_set_level((gpio_num_t)board::GATE_EN[i],
                       released ? STEP_RELEASE_LEVEL : STEP_HOLD_LEVEL);
    if (outputLock)
        xSemaphoreGive(outputLock);
}
extern "C" void MotorPwm_EmergencyStop(void) {
    released = true;
    stopReason = STOP_EMERGENCY;
    emergency = true;
    stopOutputs();
}
extern "C" void MotorPwm_SafetyStop(uint8_t reason) {
    latchFault(reason);
    stopOutputs();
}
extern "C" void EspMotorsBegin(void) {
    if (initialized)
        return;
    initialized = true;
    outputLock = xSemaphoreCreateMutex();
    const uint8_t pins[] = {board::GATE_DIR[0], board::GATE_PUL[0],     board::GATE_DIR[1],
                            board::GATE_PUL[1], board::SERVO_EN_UNUSED, board::SERVO_DIR,
                            board::SERVO_PUL};
    for (uint8_t pin : pins) {
        digitalWrite(pin, LOW);
        pinMode(pin, OUTPUT);
    }
    for (unsigned i = 0; i < 2; i++) {
        digitalWrite(board::GATE_EN[i], STEP_HOLD_LEVEL);
        pinMode(board::GATE_EN[i], OUTPUT);
    }
#if EMERGENCY_GPIO >= 0
    pinMode(EMERGENCY_GPIO, INPUT);
#endif
    if (!outputLock) {
        latchFault(STOP_INITIALIZATION);
        return;
    }
    esp_err_t err = ESP_OK;
    for (unsigned i = 0; i < 2 && err == ESP_OK; i++) {
        gates[i].index = i;
        gptimer_config_t cfg = {};
        cfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
        cfg.direction = GPTIMER_COUNT_UP;
        cfg.resolution_hz = 2000000;
        err = gptimer_new_timer(&cfg, &gates[i].timer);
        gptimer_event_callbacks_t cb = {};
        cb.on_alarm = onAlarm;
        if (err == ESP_OK)
            err = gptimer_register_event_callbacks(gates[i].timer, &cb, &gates[i]);
        if (err == ESP_OK)
            err = gptimer_enable(gates[i].timer);
        if (err == ESP_OK)
            err = gptimer_start(gates[i].timer);
    }
    ledc_timer_config_t pwm = {};
    pwm.speed_mode = LEDC_LOW_SPEED_MODE;
    pwm.timer_num = LEDC_TIMER_1;
    pwm.duty_resolution = LEDC_TIMER_8_BIT;
    pwm.freq_hz = 100000;
    pwm.clk_cfg = LEDC_USE_XTAL_CLK;
    if (err == ESP_OK)
        err = ledc_timer_config(&pwm);
    ledc_channel_config_t channel = {};
    channel.gpio_num = board::SERVO_PUL;
    channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = LEDC_CHANNEL_2;
    channel.timer_sel = LEDC_TIMER_1;
    channel.duty = 0;
    if (err == ESP_OK)
        err = ledc_channel_config(&channel);
    ready = (err == ESP_OK);
    alive = millis();
    if (EspEmergencyHeld())
        MotorPwm_EmergencyStop();
    else if (!ready.load())
        MotorPwm_SafetyStop(STOP_INITIALIZATION);
    else
        for (unsigned i = 0; i < 2; i++)
            gpio_set_level((gpio_num_t)board::GATE_EN[i], STEP_HOLD_LEVEL);
}
extern "C" void MotorPwm_Init(void) {
    EspMotorsBegin();
    MotorPwm_Stop(MOTOR_PWM_DRIVER_1);
    MotorPwm_Stop(MOTOR_PWM_DRIVER_2);
    MotorPwm_Stop(MOTOR_PWM_DRIVER_4);
    if (!EspEmergencyHeld() && ready.load()) {
        released = false;
        stopReason = STOP_NONE;
        alive = millis();
        emergency = false;
        for (unsigned i = 0; i < 2; i++)
            gpio_set_level((gpio_num_t)board::GATE_EN[i], STEP_HOLD_LEVEL);
    }
}
static uint8_t startStep(unsigned i, uint8_t dir, uint32_t period, uint32_t target,
                         const MotorProfile *profile, uint32_t completed) {
    if (!ready.load() || emergency.load() || EspEmergencyHeld() || dir > 1 || period < 400 ||
        period > 1000000 || (target && completed >= target))
        return 0;
    StepSequence &step = gates[i].step;
    gptimer_handle_t timer = gates[i].timer;
    xSemaphoreTake(outputLock, portMAX_DELAY);
    stopStep(i);
    if (emergency.load() || EspEmergencyHeld()) {
        xSemaphoreGive(outputLock);
        return 0;
    }
    gpio_set_level((gpio_num_t)board::GATE_EN[i], STEP_HOLD_LEVEL);
    gpio_set_level((gpio_num_t)board::GATE_DIR[i], dir ^ directionInvert[i]);
    portENTER_CRITICAL(&stepLock);
    step.dir = dir;
    step.period = period;
    step.target = target;
    step.count = completed;
    step.profiled = (profile != nullptr);
    if (profile)
        step.profile = *profile;
    step.running = true;
    step.high = false;
    uint64_t count = 0;
    esp_err_t err = gptimer_get_raw_count(timer, &count);
    gptimer_alarm_config_t alarm = {};
    alarm.alarm_count = count + 4000; // 2 ms DIR/enable setup.
    if (err == ESP_OK)
        err = gptimer_set_alarm_action(timer, &alarm);
    if (err != ESP_OK)
        latchFault(STOP_MOTOR);
    if (err != ESP_OK || emergency.load()) {
        step.running = false;
        gpio_set_level((gpio_num_t)board::GATE_PUL[i], 0);
    }
    portEXIT_CRITICAL(&stepLock);
    xSemaphoreGive(outputLock);
    return err == ESP_OK && !emergency.load();
}
extern "C" uint8_t MotorPwm_StartProfile(MotorPwmId motor, uint8_t dir, const MotorProfile *profile,
                                         uint32_t completed) {
    int i = axisIndex(motor);
    if (i < 0 || !profile || !MotorProfile_TotalPulses(profile))
        return 0;
    return startStep((unsigned)i, dir, MotorProfile_PeriodUs(profile, completed),
                     MotorProfile_TotalPulses(profile), profile, completed);
}
extern "C" uint8_t MotorPwm_StartSteps(MotorPwmId motor, uint8_t dir, uint32_t period,
                                       uint32_t count) {
    int i = axisIndex(motor);
    return i >= 0 && count ? startStep((unsigned)i, dir, period, count, nullptr, 0) : 0;
}
extern "C" uint8_t MotorPwm_StartContinuousNs(MotorPwmId motor, uint8_t dir, uint32_t ns) {
    int i = axisIndex(motor);
    if (i >= 0)
        return startStep((unsigned)i, dir, (ns + 999) / 1000, 0, nullptr, 0);
    if (motor != MOTOR_PWM_DRIVER_4 || !ready.load() || emergency.load() || EspEmergencyHeld() ||
        !ns || dir > 1)
        return 0;
    uint32_t hz = (1000000000UL + ns / 2) / ns;
    if (hz < 50 || hz > 800000)
        return 0;
    uint8_t bits = 10;
    while (bits > 1 && uint64_t(hz) * (1UL << bits) > 40000000UL)
        --bits;
    xSemaphoreTake(outputLock, portMAX_DELAY);
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, 0);
    beltRunning = false;
    gpio_set_level((gpio_num_t)board::SERVO_DIR, dir);
    delayMicroseconds(10);
    ledc_timer_config_t cfg = {};
    cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    cfg.timer_num = LEDC_TIMER_1;
    cfg.duty_resolution = (ledc_timer_bit_t)bits;
    cfg.freq_hz = hz;
    cfg.clk_cfg = LEDC_USE_XTAL_CLK;
    esp_err_t err = ledc_timer_config(&cfg);
    if (err == ESP_OK && !emergency.load() && !EspEmergencyHeld()) {
        err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, 1UL << (bits - 1));
        if (err == ESP_OK)
            err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2);
        beltRunning = (err == ESP_OK);
        beltPeriod = ns;
    }
    if (emergency.load() || EspEmergencyHeld()) {
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, 0);
        beltRunning = false;
    }
    bool ok = beltRunning.load();
    xSemaphoreGive(outputLock);
    return ok;
}
extern "C" uint8_t MotorPwm_StartContinuousArr(MotorPwmId motor, uint8_t dir, uint16_t arr) {
    return arr >= 4 ? MotorPwm_StartContinuousNs(motor, dir, (uint32_t(arr) + 1) * 250) : 0;
}
extern "C" uint8_t MotorPwm_StartContinuous(MotorPwmId motor, uint8_t dir, uint32_t us) {
    return MotorPwm_StartContinuousNs(motor, dir, us * 1000);
}
extern "C" uint8_t MotorPwm_IsRunning(MotorPwmId motor) {
    if (motor == MOTOR_PWM_DRIVER_4)
        return beltRunning.load();
    int i = axisIndex(motor);
    if (i < 0)
        return 0;
    StepSequence &step = gates[i].step;
    portENTER_CRITICAL(&stepLock);
    bool running = step.running;
    portEXIT_CRITICAL(&stepLock);
    return running;
}
extern "C" uint32_t MotorPwm_GetPulseCount(MotorPwmId motor) {
    int i = axisIndex(motor);
    if (i < 0)
        return 0;
    StepSequence &step = gates[i].step;
    portENTER_CRITICAL(&stepLock);
    uint32_t count = step.count;
    portEXIT_CRITICAL(&stepLock);
    return count;
}
extern "C" uint32_t MotorPwm_GetPeriodNs(MotorPwmId motor) {
    if (motor == MOTOR_PWM_DRIVER_4)
        return beltPeriod.load();
    int i = axisIndex(motor);
    if (i < 0)
        return 0;
    StepSequence &step = gates[i].step;
    portENTER_CRITICAL(&stepLock);
    uint32_t ns = step.period * 1000;
    portEXIT_CRITICAL(&stepLock);
    return ns;
}
extern "C" uint32_t MotorPwm_GetPeriodUs(MotorPwmId motor) {
    return (MotorPwm_GetPeriodNs(motor) + 999) / 1000;
}
extern "C" uint8_t MotorPwm_SetPeriod(MotorPwmId, uint32_t) {
    return 0;
}
extern "C" void EspControlAlive(void) {
    alive = millis();
}
extern "C" void EspSafetyPoll(void) {
    if (EspEmergencyHeld())
        MotorPwm_EmergencyStop();
    else if (uint32_t(millis() - alive.load()) > 100)
        MotorPwm_SafetyStop(STOP_LOOP_WATCHDOG);
    else if (emergency.load())
        stopOutputs();
}
