#ifndef TUNNEL_TEST_STUBS_H
#define TUNNEL_TEST_STUBS_H

#include <stdint.h>
#include "hmi.h"
#include "motor_pwm.h"
#include "zk7182.h"

#define TUNNEL_TEST_EVENT_CAPACITY 64U
#define TUNNEL_TEST_TX_CAPACITY 128U

typedef struct {
    uint16_t vp;
    uint16_t value;
} TunnelTestWordWrite;

typedef struct {
    uint32_t start_continuous_calls;
    uint32_t start_profile_calls;
    uint32_t stop_calls;
    uint8_t running;
    uint8_t direction;
    uint32_t period_us;
    uint32_t period_ns;
    uint32_t pulse_count;
    uint32_t pulse_target;
    uint32_t last_profile_completed;
    uint32_t last_start_tick;
    uint32_t last_stop_tick;
} TunnelTestMotor;

typedef struct {
    uint8_t ready;
    uint8_t scan_active;
    uint8_t scan_complete;
    uint8_t response_pending;
    uint8_t paused;
    ZK7182_ScanResult result;
    uint16_t unique_count;
    uint32_t total_count;
    uint32_t start_calls;
    uint32_t pause_calls;
    uint32_t resume_calls;
    uint32_t cancel_calls;
    uint32_t cancel_at_calls;
    uint32_t last_duration_ms;
    uint32_t scan_start_tick;
} TunnelTestReader;

extern uint32_t tunnel_test_tick;
extern TunnelTestMotor tunnel_test_motors[MOTOR_PWM_DRIVER_COUNT];
extern TunnelTestReader tunnel_test_reader;
extern uint32_t tunnel_test_motor_init_calls;
extern uint32_t tunnel_test_emergency_stop_calls;
extern uint32_t tunnel_test_hmi_flush_calls;
extern uint32_t tunnel_test_quantity_count;
extern uint16_t tunnel_test_last_quantity;
extern uint32_t tunnel_test_page_count;
extern uint16_t tunnel_test_last_page;
extern TunnelTestWordWrite tunnel_test_word_writes[TUNNEL_TEST_TX_CAPACITY];
extern uint32_t tunnel_test_word_write_count;
extern uint16_t tunnel_test_last_indicators[5];
extern uint32_t tunnel_test_indicator_count;

void TunnelTest_ResetStubs(void);
void TunnelTest_SetSensor(uint16_t pin, uint8_t active, uint8_t emit_exti);
void TunnelTest_QueueEvent(uint16_t vp, uint16_t value);
void TunnelTest_AdvanceTick(uint32_t milliseconds);
void TunnelTest_CompleteProfile(MotorPwmId motor);

#endif /* TUNNEL_TEST_STUBS_H */

/* Simultaneous-pair fixtures for the inherited cycle regression suite. */
void TunnelTest_SetPairedSensor(uint16_t pin, uint8_t active, uint8_t emit_exti);
void TunnelTest_CompleteGatePair(void);
