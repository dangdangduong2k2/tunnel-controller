#ifndef STM32F4XX_HAL_H
#define STM32F4XX_HAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { HAL_OK = 0U, HAL_ERROR = 1U, HAL_BUSY = 2U, HAL_TIMEOUT = 3U } HAL_StatusTypeDef;

typedef struct {
    void *Instance;
    uint32_t ErrorCode;
} UART_HandleTypeDef;

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, uint8_t *data, uint16_t length,
                                    uint32_t timeout);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *huart, uint8_t *data, uint16_t length);
void HAL_UART_IRQHandler(UART_HandleTypeDef *huart);
uint32_t HAL_UART_GetError(const UART_HandleTypeDef *huart);
uint32_t HAL_GetTick(void);

uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __enable_irq(void);

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* STM32F4XX_HAL_H */
