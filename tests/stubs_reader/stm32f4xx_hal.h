#ifndef STM32F4XX_HAL_H
#define STM32F4XX_HAL_H
#include <stdint.h>
uint32_t HAL_GetTick(void);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __enable_irq(void);
#endif
