#pragma once
typedef int SemaphoreHandle_t;
#define portMAX_DELAY 0
inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    return 1;
}
inline void xSemaphoreTake(SemaphoreHandle_t, int) {}
inline void xSemaphoreGive(SemaphoreHandle_t) {}
