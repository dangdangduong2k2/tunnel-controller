#include "tunnel_test_stubs.h"
#include "../speed_store_fake.h"

#include "board.h"
#include "debug_state.h"
#include "stm32f4xx_hal.h"
#include "tunnel.h"

#include <string.h>

GPIO_TypeDef tunnel_test_gpioa;
GPIO_TypeDef tunnel_test_gpiob;
GPIO_TypeDef tunnel_test_gpioc;
GPIO_TypeDef tunnel_test_gpiod;
uint32_t tunnel_test_primask;
uint32_t tunnel_test_tick;
volatile DebugStateTypeDef g_debug;
#ifndef TUNNEL_TEST_REAL_HMI
volatile HmiDiagnostics g_hmi_debug;
#endif

TunnelTestMotor tunnel_test_motors[MOTOR_PWM_DRIVER_COUNT];
TunnelTestReader tunnel_test_reader;
uint32_t tunnel_test_motor_init_calls;
uint32_t tunnel_test_emergency_stop_calls;
uint32_t tunnel_test_hmi_flush_calls;
uint32_t tunnel_test_quantity_count;
uint16_t tunnel_test_last_quantity;
uint32_t tunnel_test_page_count;
uint16_t tunnel_test_last_page;
TunnelTestWordWrite tunnel_test_word_writes[TUNNEL_TEST_TX_CAPACITY];
uint32_t tunnel_test_word_write_count;
uint16_t tunnel_test_last_indicators[5];
uint32_t tunnel_test_indicator_count;

static HmiEvent tunnel_test_events[TUNNEL_TEST_EVENT_CAPACITY];
static uint32_t tunnel_test_event_head;
static uint32_t tunnel_test_event_tail;
static uint8_t tunnel_test_emergency_event_pending;
static uint8_t tunnel_test_reset_event_pending;
static uint8_t tunnel_test_motor_emergency_latched, released, stop_reason;

void TunnelTest_ResetStubs(void) {
    SpeedStoreTest_Reset();
    (void)memset(&tunnel_test_gpioa, 0, sizeof(tunnel_test_gpioa));
    (void)memset(&tunnel_test_gpiob, 0, sizeof(tunnel_test_gpiob));
    (void)memset(&tunnel_test_gpioc, 0, sizeof(tunnel_test_gpioc));
    (void)memset(&tunnel_test_gpiod, 0, sizeof(tunnel_test_gpiod));
    (void)memset((void *)&g_debug, 0, sizeof(g_debug));
    (void)memset((void *)&g_hmi_debug, 0, sizeof(g_hmi_debug));
    (void)memset(tunnel_test_motors, 0, sizeof(tunnel_test_motors));
    (void)memset(&tunnel_test_reader, 0, sizeof(tunnel_test_reader));
    (void)memset(tunnel_test_events, 0, sizeof(tunnel_test_events));
    (void)memset(tunnel_test_word_writes, 0, sizeof(tunnel_test_word_writes));
    (void)memset(tunnel_test_last_indicators, 0, sizeof(tunnel_test_last_indicators));

    tunnel_test_gpioa.IDR = UINT32_MAX;
    tunnel_test_gpiob.IDR = UINT32_MAX;
    tunnel_test_gpioc.IDR = UINT32_MAX;
    tunnel_test_gpiod.IDR = UINT32_MAX;
    tunnel_test_primask = 0U;
    tunnel_test_tick = 0U;
    tunnel_test_reader.ready = 1U;
    tunnel_test_reader.result = ZK7182_SCAN_RESULT_IN_PROGRESS;
    tunnel_test_motor_init_calls = 0U;
    tunnel_test_emergency_stop_calls = 0U;
    tunnel_test_hmi_flush_calls = 0U;
    tunnel_test_quantity_count = 0U;
    tunnel_test_last_quantity = 0U;
    tunnel_test_page_count = 0U;
    tunnel_test_last_page = 0U;
    tunnel_test_word_write_count = 0U;
    tunnel_test_indicator_count = 0U;
    tunnel_test_event_head = 0U;
    tunnel_test_event_tail = 0U;
    tunnel_test_emergency_event_pending = 0U;
    tunnel_test_reset_event_pending = 0U;
    tunnel_test_motor_emergency_latched = 0U;
    released = 0U;
    stop_reason = STOP_NONE;
}

void TunnelTest_SetSensor(uint16_t pin, uint8_t active, uint8_t emit_exti) {
    if (active != 0U) {
        tunnel_test_gpioc.IDR &= ~(uint32_t)pin;
    } else {
        tunnel_test_gpioc.IDR |= pin;
    }
    if (emit_exti != 0U) {
        HAL_GPIO_EXTI_Callback(pin);
    }
}

void TunnelTest_QueueEvent(uint16_t vp, uint16_t value) {
    uint32_t next;

    if ((vp == 0x1040U) && (value == 1U)) {
        /* Match the production transport: emergency discards ordinary backlog. */
        tunnel_test_event_tail = tunnel_test_event_head;
        tunnel_test_emergency_event_pending = 1U;
        tunnel_test_reset_event_pending = 0U;
        return;
    }
    if (tunnel_test_emergency_event_pending != 0U) {
        return;
    }
    if ((vp == 0x1050U) && (value == 1U)) {
        tunnel_test_event_tail = tunnel_test_event_head;
        tunnel_test_reset_event_pending = 1U;
        return;
    }
    if (tunnel_test_reset_event_pending != 0U) {
        return;
    }
    next = (tunnel_test_event_head + 1U) % TUNNEL_TEST_EVENT_CAPACITY;
    if (next != tunnel_test_event_tail) {
        tunnel_test_events[tunnel_test_event_head].vp = vp;
        tunnel_test_events[tunnel_test_event_head].value = value;
        tunnel_test_event_head = next;
    }
}

void TunnelTest_AdvanceTick(uint32_t milliseconds) {
    while (milliseconds != 0U) {
        tunnel_test_tick++;
        Tunnel_Tick1ms();
        milliseconds--;
    }
}

void TunnelTest_CompleteProfile(MotorPwmId motor) {
    if (motor < MOTOR_PWM_DRIVER_COUNT) {
        tunnel_test_motors[motor].pulse_count = tunnel_test_motors[motor].pulse_target;
        tunnel_test_motors[motor].running = 0U;
    }
}

uint32_t HAL_GetTick(void) {
    return tunnel_test_tick;
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin) {
    return ((port->IDR & pin) != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

void HAL_GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *config) {
    (void)port;
    (void)config;
}

void HAL_NVIC_SetPriority(int irq, uint32_t priority, uint32_t subpriority) {
    (void)irq;
    (void)priority;
    (void)subpriority;
}

void HAL_NVIC_EnableIRQ(int irq) {
    (void)irq;
}

void MotorPwm_Init(void) {
    uint32_t motor;

    tunnel_test_motor_init_calls++;
    tunnel_test_motor_emergency_latched = 0U;
    released = 0U;
    stop_reason = STOP_NONE;
    for (motor = 0U; motor < (uint32_t)MOTOR_PWM_DRIVER_COUNT; motor++) {
        tunnel_test_motors[motor].running = 0U;
    }
}

void MotorPwm_EmergencyStop(void) {
    uint32_t motor;

    released = 1U;
    stop_reason = STOP_EMERGENCY;
    tunnel_test_emergency_stop_calls++;
    tunnel_test_motor_emergency_latched = 1U;
    for (motor = 0U; motor < (uint32_t)MOTOR_PWM_DRIVER_COUNT; motor++) {
        tunnel_test_motors[motor].running = 0U;
    }
}

uint8_t MotorPwm_IsReleased(void) {
    return released;
}
uint8_t MotorPwm_GetStopReason(void) {
    return stop_reason;
}
void MotorPwm_SafetyStop(uint8_t reason) {
    uint32_t motor;
    if (!stop_reason || stop_reason == STOP_PC_TAKEOVER)
        stop_reason = reason;
    tunnel_test_motor_emergency_latched = 1U;
    for (motor = 0; motor < MOTOR_PWM_DRIVER_COUNT; motor++)
        tunnel_test_motors[motor].running = 0U;
}

uint8_t MotorPwm_StartContinuous(MotorPwmId motor, uint8_t direction, uint32_t period_us) {
    TunnelTestMotor *selected;

    if ((motor >= MOTOR_PWM_DRIVER_COUNT) || (tunnel_test_motor_emergency_latched != 0U)) {
        return 0U;
    }
    selected = &tunnel_test_motors[motor];
    selected->start_continuous_calls++;
    selected->running = 1U;
    selected->direction = direction;
    selected->period_us = period_us;
    selected->period_ns = period_us * 1000UL;
    selected->pulse_count = 0U;
    selected->pulse_target = 0U;
    selected->last_start_tick = tunnel_test_tick;
    return 1U;
}

uint8_t MotorPwm_StartContinuousArr(MotorPwmId motor, uint8_t direction, uint16_t arr) {
    if ((motor != MOTOR_PWM_DRIVER_4) || (arr < 4U)) {
        return 0U;
    }
    return MotorPwm_StartContinuousNs(motor, direction, ((uint32_t)arr + 1U) * 250UL);
}

uint8_t MotorPwm_StartContinuousNs(MotorPwmId motor, uint8_t direction, uint32_t period_ns) {
    if (MotorPwm_StartContinuous(motor, direction, period_ns / 1000UL) == 0U) {
        return 0U;
    }
    tunnel_test_motors[motor].period_ns = period_ns;
    return 1U;
}

uint8_t MotorPwm_StartSteps(MotorPwmId motor, uint8_t direction, uint32_t period_us,
                            uint32_t pulse_count) {
    TunnelTestMotor *selected;

    if ((motor >= MOTOR_PWM_DRIVER_COUNT) || (pulse_count == 0U) ||
        (tunnel_test_motor_emergency_latched != 0U)) {
        return 0U;
    }
    selected = &tunnel_test_motors[motor];
    selected->start_profile_calls++;
    selected->running = 1U;
    selected->direction = direction;
    selected->period_us = period_us;
    selected->pulse_count = 0U;
    selected->pulse_target = pulse_count;
    selected->last_profile_completed = 0U;
    selected->last_start_tick = tunnel_test_tick;
    return 1U;
}

uint8_t MotorPwm_StartProfile(MotorPwmId motor, uint8_t direction, const MotorProfile *profile,
                              uint32_t completed_pulses) {
    TunnelTestMotor *selected;
    uint32_t target;

    if ((motor >= MOTOR_PWM_DRIVER_COUNT) || (profile == NULL) ||
        (tunnel_test_motor_emergency_latched != 0U)) {
        return 0U;
    }
    target = profile->accel_pulses + profile->cruise_pulses + profile->decel_pulses;
    if ((target == 0U) || (completed_pulses >= target)) {
        return 0U;
    }
    selected = &tunnel_test_motors[motor];
    selected->start_profile_calls++;
    selected->running = 1U;
    selected->direction = direction;
    selected->period_us = profile->start_period_us;
    selected->pulse_count = completed_pulses;
    selected->pulse_target = target;
    selected->last_profile_completed = completed_pulses;
    selected->last_start_tick = tunnel_test_tick;
    return 1U;
}

void MotorPwm_Stop(MotorPwmId motor) {
    if (motor < MOTOR_PWM_DRIVER_COUNT) {
        tunnel_test_motors[motor].stop_calls++;
        tunnel_test_motors[motor].running = 0U;
        tunnel_test_motors[motor].last_stop_tick = tunnel_test_tick;
    }
}

uint8_t MotorPwm_SetPeriod(MotorPwmId motor, uint32_t period_us) {
    if (motor >= MOTOR_PWM_DRIVER_COUNT) {
        return 0U;
    }
    tunnel_test_motors[motor].period_us = period_us;
    return 1U;
}

uint8_t MotorPwm_IsRunning(MotorPwmId motor) {
    return (motor < MOTOR_PWM_DRIVER_COUNT) ? tunnel_test_motors[motor].running : 0U;
}

uint32_t MotorPwm_GetPulseCount(MotorPwmId motor) {
    return (motor < MOTOR_PWM_DRIVER_COUNT) ? tunnel_test_motors[motor].pulse_count : 0U;
}

uint32_t MotorPwm_GetPeriodUs(MotorPwmId motor) {
    return (motor < MOTOR_PWM_DRIVER_COUNT) ? tunnel_test_motors[motor].period_us : 0U;
}

void MotorPwm_IRQHandler(TIM_HandleTypeDef *timer) {
    (void)timer;
}

#ifndef TUNNEL_TEST_REAL_READER
void ZK7182_Init(void) {}

void ZK7182_Process(void) {}

void ZK7182_TransportReady(void) {
    tunnel_test_reader.ready = 1U;
}

void ZK7182_TransportLost(void) {
    tunnel_test_reader.ready = 0U;
    tunnel_test_reader.scan_active = 0U;
}

uint8_t ZK7182_IsReady(void) {
    return tunnel_test_reader.ready;
}

uint8_t ZK7182_StartScan(uint32_t duration_ms) {
    if ((tunnel_test_reader.ready == 0U) || (tunnel_test_reader.scan_active != 0U) ||
        (tunnel_test_reader.response_pending != 0U)) {
        return 0U;
    }
    tunnel_test_reader.start_calls++;
    tunnel_test_reader.last_duration_ms = duration_ms;
    tunnel_test_reader.scan_start_tick = tunnel_test_tick;
    tunnel_test_reader.scan_active = 1U;
    tunnel_test_reader.scan_complete = 0U;
    tunnel_test_reader.paused = 0U;
    tunnel_test_reader.result = ZK7182_SCAN_RESULT_IN_PROGRESS;
    tunnel_test_reader.unique_count = 0U;
    tunnel_test_reader.total_count = 0U;
    return 1U;
}

uint8_t ZK7182_IsInventoryRunning(void) {
    return tunnel_test_reader.scan_active;
}
uint8_t ZK7182_IsScanActive(void) {
    return tunnel_test_reader.scan_active;
}

uint8_t ZK7182_IsScanComplete(void) {
    return tunnel_test_reader.scan_complete;
}

uint8_t ZK7182_IsInventoryResponsePending(void) {
    return tunnel_test_reader.response_pending;
}

ZK7182_ScanResult ZK7182_GetScanResult(void) {
    return tunnel_test_reader.result;
}

uint16_t ZK7182_GetUniqueEpcCount(void) {
    return tunnel_test_reader.unique_count;
}

uint32_t ZK7182_GetTotalReadCount(void) {
    return tunnel_test_reader.total_count;
}

void ZK7182_PauseScan(void) {
    tunnel_test_reader.pause_calls++;
    if (tunnel_test_reader.scan_active != 0U) {
        tunnel_test_reader.paused = 1U;
    }
}

void ZK7182_ResumeScan(void) {
    tunnel_test_reader.resume_calls++;
    if (tunnel_test_reader.scan_active != 0U) {
        tunnel_test_reader.paused = 0U;
    }
}

void ZK7182_CancelScanAtUniqueCount(uint16_t unique_count) {
    tunnel_test_reader.cancel_at_calls++;
    if (unique_count < tunnel_test_reader.unique_count) {
        tunnel_test_reader.unique_count = unique_count;
    }
    ZK7182_CancelScan();
}

void ZK7182_ClearScanComplete(void) {
    tunnel_test_reader.scan_complete = 0U;
}

void ZK7182_CancelScan(void) {
    uint8_t had_transaction;

    had_transaction = (uint8_t)((tunnel_test_reader.scan_active != 0U) ||
                                (tunnel_test_reader.response_pending != 0U));
    tunnel_test_reader.cancel_calls++;
    tunnel_test_reader.scan_active = 0U;
    tunnel_test_reader.scan_complete = 0U;
    tunnel_test_reader.response_pending = 0U;
    tunnel_test_reader.paused = 0U;
    tunnel_test_reader.result = ZK7182_SCAN_RESULT_IDLE;
    if (had_transaction != 0U) {
        /* The production reader reconfigures itself after aborting a session. */
        tunnel_test_reader.ready = 0U;
    }
}

#endif /* Stub reader only for isolated Tunnel tests. */

uint8_t TunnelTest_IsEmergencyLatched(void) {
    return tunnel_test_motor_emergency_latched;
}

#ifndef TUNNEL_TEST_REAL_HMI
uint8_t Hmi_PopEvent(HmiEvent *event) {
    if (event == NULL) {
        return 0U;
    }
    if (tunnel_test_emergency_event_pending != 0U) {
        event->vp = 0x1040U;
        event->value = 1U;
        tunnel_test_emergency_event_pending = 0U;
        return 1U;
    }
    if (tunnel_test_reset_event_pending != 0U) {
        event->vp = 0x1050U;
        event->value = 1U;
        tunnel_test_reset_event_pending = 0U;
        return 1U;
    }
    if (tunnel_test_event_tail == tunnel_test_event_head) {
        return 0U;
    }
    *event = tunnel_test_events[tunnel_test_event_tail];
    tunnel_test_event_tail = (tunnel_test_event_tail + 1U) % TUNNEL_TEST_EVENT_CAPACITY;
    return 1U;
}

static uint8_t control_locked;
uint8_t Hmi_IsControlLocked(void) {
    return control_locked;
}
void Hmi_SetControlLocked(uint8_t locked) {
    control_locked = locked;
    Hmi_DiscardActionsAfterStop();
}
void Hmi_DiscardActionsAfterStop(void) {
    Hmi_FlushEvents();
    tunnel_test_emergency_event_pending = 0U;
    tunnel_test_reset_event_pending = 0U;
}

void Hmi_FlushEvents(void) {
    tunnel_test_hmi_flush_calls++;
    tunnel_test_event_tail = tunnel_test_event_head;
    /* Production flush fences normal actions, never priority stop requests. */
}

void Hmi_Init(void) {}

void Hmi_Process(void) {}

void Hmi_IRQHandler(void) {}

uint8_t Hmi_SendQuantity(uint16_t quantity) {
    tunnel_test_quantity_count++;
    tunnel_test_last_quantity = quantity;
    return 1U;
}

uint8_t Hmi_SendPage(uint16_t page) {
    tunnel_test_page_count++;
    tunnel_test_last_page = page;
    return 1U;
}

uint8_t Hmi_SendWord(uint16_t vp, uint16_t value) {
    uint32_t index;

    index = tunnel_test_word_write_count;
    if (index < TUNNEL_TEST_TX_CAPACITY) {
        tunnel_test_word_writes[index].vp = vp;
        tunnel_test_word_writes[index].value = value;
    }
    tunnel_test_word_write_count++;
    return 1U;
}

uint8_t Hmi_SendIndicators(uint16_t conveyor, uint16_t gate, uint16_t scan, uint16_t auto_mode,
                           uint16_t semi_mode) {
    tunnel_test_last_indicators[0] = conveyor;
    tunnel_test_last_indicators[1] = gate;
    tunnel_test_last_indicators[2] = scan;
    tunnel_test_last_indicators[3] = auto_mode;
    tunnel_test_last_indicators[4] = semi_mode;
    tunnel_test_indicator_count++;
    return 1U;
}
#endif

void TunnelTest_SetPairedSensor(uint16_t pin, uint8_t active, uint8_t emit_exti) {
    TunnelTest_SetSensor(pin, active, emit_exti);
    if (pin == SENSOR_SS2_PIN) {
        TunnelTest_SetSensor(SENSOR_SS7_PIN, active, emit_exti);
    }
}
void TunnelTest_CompleteGatePair(void) {
    TunnelTest_CompleteProfile(MOTOR_PWM_DRIVER_1);
    TunnelTest_CompleteProfile(MOTOR_PWM_DRIVER_2);
}
