#pragma once
#include <stdint.h>
#define LOW 0
#define OUTPUT 1
#define INPUT 0
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define portENTER_CRITICAL_ISR(p) ((void)(p))
#define portEXIT_CRITICAL_ISR(p) ((void)(p))
void digitalWrite(uint8_t, int);
void pinMode(uint8_t, int);
uint32_t millis();
void delayMicroseconds(uint32_t);
typedef int esp_err_t;
#define ESP_OK 0
