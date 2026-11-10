#pragma once
// Production module is colocated with its real header in the ESP32 sketch.
// Use that same layout in the test stub; a one-field mock has the wrong ABI.
#include "../../firmware/TunnelESP32/src/common/debug_state.h"
