#pragma once
#include <stdint.h>
#include <stddef.h>
inline uint32_t esp_random() {
    static uint32_t n = 12345;
    n ^= n << 13;
    n ^= n >> 17;
    n ^= n << 5;
    return n;
}
inline void esp_fill_random(void *p, size_t n) {
    auto *b = (uint8_t *)p;
    for (size_t i = 0; i < n; i++)
        b[i] = uint8_t(esp_random());
}
