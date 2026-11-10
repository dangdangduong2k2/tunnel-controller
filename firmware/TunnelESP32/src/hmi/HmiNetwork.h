#pragma once
#include "../config/DeviceConfig.h"

struct HmiNetworkStatus {
    bool link, hasIp, fallback, mqttConnected;
    uint8_t ip[4], mask[4], gateway[4];
};

// One nonblocking field per 100 ms, entire view refreshed every 1.2 seconds.
// Repeating also restores text after an independently powered DWIN restarts.
class HmiNetworkPublisher {
  public:
    void poll(uint32_t now, const cfg::Settings &settings, const HmiNetworkStatus &status);

  private:
    uint32_t last = 0;
    uint8_t field = 0;
    bool sent = false;
};
