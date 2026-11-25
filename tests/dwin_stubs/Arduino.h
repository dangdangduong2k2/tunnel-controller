#pragma once
#include <stddef.h>
#include <stdint.h>
uint32_t millis();
void delay(uint32_t ms);
struct TestSerial {
    int availableForWrite();
    size_t write(const uint8_t *,size_t);
    int available();
    int read();
};
extern TestSerial Serial0;
