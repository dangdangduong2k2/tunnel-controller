#pragma once
#include <stdint.h>
namespace fota {
enum BootDecision { WAIT_HEALTH, CONFIRM_BOOT, ROLLBACK_BOOT };
inline bool fenceMayStart(bool cancelled, bool physicalEmergency, bool released) {
    // A normal HOLD safety latch is expected after our own fence, not a cancellation.
    return !cancelled && !physicalEmergency && !released;
}
inline BootDecision bootDecision(uint32_t now, uint32_t started, uint32_t alive,
                                 uint32_t healthySince, bool storage, bool matchingVersion) {
    // Deadline wins even when health is good but confirmation repeatedly fails.
    if (uint32_t(now-started)>=30000U) return ROLLBACK_BOOT;
    if (uint32_t(now-started)>=10000U && uint32_t(now-alive)<500U && healthySince &&
        uint32_t(now-healthySince)>=5000U && storage && matchingVersion) return CONFIRM_BOOT;
    return WAIT_HEALTH;
}
}
