#pragma once
#include <stdint.h>
class IPAddress {
    uint8_t data[4] = {192, 168, 4, 1};

  public:
    IPAddress() = default;
    explicit IPAddress(const uint8_t *p) {
        for (unsigned i = 0; i < 4; i++)
            data[i] = p[i];
    }
    uint8_t operator[](unsigned i) const {
        return data[i];
    }
};
struct TestETH {
    IPAddress address;
    bool linkUp() {
        return true;
    }
    bool hasIP() {
        return true;
    }
    IPAddress localIP() {
        return address;
    }
};
inline TestETH ETH;
