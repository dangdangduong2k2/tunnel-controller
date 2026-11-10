#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void SharedUart_Init(void);
void SharedUart_ReceiveByte(uint8_t value);
void SharedUart_Fence(void);
uint16_t SharedUart_Encode(const uint8_t *bytes, uint16_t length, uint8_t *output,
                           uint16_t capacity);
#ifdef __cplusplus
}
#endif
