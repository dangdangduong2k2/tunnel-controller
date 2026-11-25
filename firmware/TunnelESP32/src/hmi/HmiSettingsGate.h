#pragma once
#include "HmiUi.h"
#include "../control/tunnel.h"

namespace hmiui {
// Configuration permission is independent of the Emergency/Stopped status badge.
// A stopped machine may be configured without homing or clearing its safety latch.
// Busy includes motion, inventory, reader transactions and FOTA.
inline uint8_t settingsGate(TunnelState state, bool busy, bool remote) {
    if (remote)
        return LOCK_REMOTE;
    if (busy)
        return LOCK_WAIT_IDLE;
    switch (state) {
    case TUNNEL_STARTUP_WAIT:
    case TUNNEL_STOPPED:
    case TUNNEL_MANUAL:
    case TUNNEL_WAIT_SS1_LOW:
    case TUNNEL_WAIT_REARM:
    case TUNNEL_FAULT:
        return LOCK_NONE;
    default:
        // Includes waiting for the reader, result confirmation and every cycle state.
        return LOCK_WAIT_IDLE;
    }
}
} // namespace hmiui
