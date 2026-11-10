#ifndef MAIN_H
#define MAIN_H

#include "stm32f4xx_hal.h"

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart3;

void Error_Handler(void);

#endif /* MAIN_H */
