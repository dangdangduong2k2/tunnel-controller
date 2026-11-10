#ifndef STM32F4XX_HAL_H
#define STM32F4XX_HAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { GPIO_PIN_RESET = 0U, GPIO_PIN_SET = 1U } GPIO_PinState;

typedef struct {
    volatile uint32_t IDR;
} GPIO_TypeDef;

typedef struct {
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
    uint32_t Alternate;
} GPIO_InitTypeDef;

typedef struct {
    void *Instance;
} TIM_HandleTypeDef;

extern GPIO_TypeDef tunnel_test_gpioa;
extern GPIO_TypeDef tunnel_test_gpiob;
extern GPIO_TypeDef tunnel_test_gpioc;
extern GPIO_TypeDef tunnel_test_gpiod;
extern uint32_t tunnel_test_primask;

#define GPIOA (&tunnel_test_gpioa)
#define GPIOB (&tunnel_test_gpiob)
#define GPIOC (&tunnel_test_gpioc)
#define GPIOD (&tunnel_test_gpiod)

#define GPIO_PIN_0 ((uint16_t)0x0001U)
#define GPIO_PIN_1 ((uint16_t)0x0002U)
#define GPIO_PIN_2 ((uint16_t)0x0004U)
#define GPIO_PIN_3 ((uint16_t)0x0008U)
#define GPIO_PIN_4 ((uint16_t)0x0010U)
#define GPIO_PIN_5 ((uint16_t)0x0020U)
#define GPIO_PIN_6 ((uint16_t)0x0040U)
#define GPIO_PIN_7 ((uint16_t)0x0080U)
#define GPIO_PIN_8 ((uint16_t)0x0100U)
#define GPIO_PIN_9 ((uint16_t)0x0200U)
#define GPIO_PIN_10 ((uint16_t)0x0400U)
#define GPIO_PIN_11 ((uint16_t)0x0800U)
#define GPIO_PIN_12 ((uint16_t)0x1000U)

#define GPIO_MODE_IT_RISING_FALLING 3U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_LOW 0U

#define EXTI1_IRQn 7
#define EXTI2_IRQn 8
#define EXTI3_IRQn 9

#define __HAL_RCC_GPIOC_CLK_ENABLE() ((void)0)
#define __HAL_GPIO_EXTI_CLEAR_IT(pin) ((void)(pin))

static __inline uint32_t __get_PRIMASK(void) {
    return tunnel_test_primask;
}

static __inline void __disable_irq(void) {
    tunnel_test_primask = 1U;
}

static __inline void __enable_irq(void) {
    tunnel_test_primask = 0U;
}

uint32_t HAL_GetTick(void);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
void HAL_GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *config);
void HAL_NVIC_SetPriority(int irq, uint32_t priority, uint32_t subpriority);
void HAL_NVIC_EnableIRQ(int irq);
void HAL_GPIO_EXTI_Callback(uint16_t gpio_pin);

#ifdef __cplusplus
}
#endif

#endif /* STM32F4XX_HAL_H */
