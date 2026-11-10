#ifndef MOTOR_PWM_H
#define MOTOR_PWM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "../platform/compat/stm32f4xx_hal.h"
#include "motor_profile.h"
#include "../common/StopReason.h"

typedef enum {
    MOTOR_PWM_DRIVER_1 = 0U,
    MOTOR_PWM_DRIVER_2,
    MOTOR_PWM_DRIVER_3,
    MOTOR_PWM_DRIVER_4,
    MOTOR_PWM_DRIVER_COUNT
} MotorPwmId;

uint8_t MotorPwm_StartContinuousArr(MotorPwmId motor, uint8_t direction, uint16_t arr);
void MotorPwm_Init(void);
void MotorPwm_EmergencyStop(void);
void MotorPwm_SafetyStop(uint8_t reason);
uint8_t MotorPwm_GetStopReason(void);
uint8_t MotorPwm_IsReleased(void);
uint8_t MotorPwm_StartContinuous(MotorPwmId motor, uint8_t direction, uint32_t period_us);
/* Rounded up to a whole pair of timer ticks to keep exactly 50% duty.
 * TIM4 supports 500 ns period increments; other timers support 1000 ns. */
uint8_t MotorPwm_StartContinuousNs(MotorPwmId motor, uint8_t direction, uint32_t period_ns);
uint8_t MotorPwm_StartSteps(MotorPwmId motor, uint8_t direction, uint32_t period_us,
                            uint32_t pulse_count);
/* profile must remain valid for the entire move. Use completed_pulses=0 for
 * a new move, or the stopped count to resume after a rejected limit bounce. */
uint8_t MotorPwm_StartProfile(MotorPwmId motor, uint8_t direction, const MotorProfile *profile,
                              uint32_t completed_pulses);
void MotorPwm_Stop(MotorPwmId motor);
uint8_t MotorPwm_SetPeriod(MotorPwmId motor, uint32_t period_us);
uint8_t MotorPwm_IsRunning(MotorPwmId motor);
uint32_t MotorPwm_GetPulseCount(MotorPwmId motor);
uint32_t MotorPwm_GetPeriodUs(MotorPwmId motor);
uint32_t MotorPwm_GetPeriodNs(MotorPwmId motor);
void MotorPwm_IRQHandler(TIM_HandleTypeDef *timer);

/* Optional application hook. It executes in the timer interrupt context. */
void MotorPwm_PulseElapsedCallback(MotorPwmId motor, uint32_t pulse_count);

extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim4;
extern TIM_HandleTypeDef htim5;

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_PWM_H */
