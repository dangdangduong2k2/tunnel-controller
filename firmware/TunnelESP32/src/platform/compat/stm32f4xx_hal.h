#pragma once
#ifdef TUNNEL_HOST_TEST
#include "test_hal.h"
#else
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Legacy algorithm adapter, not an STM32 peripheral emulation. The state
// machine and all reader callbacks have one owner: the control task.
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET = 1 } GPIO_PinState;
typedef struct {
    uint32_t unused;
} GPIO_TypeDef;
typedef struct {
    uint32_t Pin, Mode, Pull, Speed, Alternate;
} GPIO_InitTypeDef;
typedef struct {
    void *Instance;
} TIM_HandleTypeDef;
extern GPIO_TypeDef virtualGpio;
#define GPIOC (&virtualGpio)
#define GPIO_PIN_1 2U
#define GPIO_PIN_2 4U
#define GPIO_PIN_3 8U
#define GPIO_PIN_4 16U
#define GPIO_PIN_7 128U
#define GPIO_MODE_IT_RISING_FALLING 0U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_LOW 0U
#define EXTI1_IRQn 0
#define EXTI2_IRQn 0
#define EXTI3_IRQn 0
#define __HAL_RCC_GPIOC_CLK_ENABLE() ((void)0)
#define __HAL_GPIO_EXTI_CLEAR_IT(pin) ((void)(pin))
// No other task/ISR enters legacy state. Motor ISR uses its own real lock.
static inline uint32_t __get_PRIMASK(void) {
    return 0;
}
static inline void __disable_irq(void) {}
static inline void __enable_irq(void) {}
uint32_t HAL_GetTick(void);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *, uint16_t);
void HAL_GPIO_Init(GPIO_TypeDef *, const GPIO_InitTypeDef *);
void HAL_NVIC_SetPriority(int, uint32_t, uint32_t);
void HAL_NVIC_EnableIRQ(int);
void HAL_GPIO_EXTI_Callback(uint16_t);
#ifdef __cplusplus
}
#endif
#endif
