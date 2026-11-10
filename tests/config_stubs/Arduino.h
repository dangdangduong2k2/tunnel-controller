#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <cstdio>
#define LOW 0
#define HIGH 1
#define INPUT_PULLUP 2
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
extern uint32_t testClock;
extern int testButton, testRestarts;
inline uint32_t millis() {
    return testClock;
}
inline int digitalRead(int) {
    return testButton;
}
inline void pinMode(int, int) {}
inline int xTaskCreate(void (*)(void *), const char *, unsigned, void *, int, void *) {
    return pdPASS;
}
struct TestYield {};
inline void vTaskDelay(unsigned) {
    throw TestYield{};
}
struct TestESP {
    void restart() {
        ++testRestarts;
    }
};
inline TestESP ESP;
