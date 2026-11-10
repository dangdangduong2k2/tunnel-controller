#include "dwin_control.h"
#include "../platform/esp32/EspPlatform.h"
#include "../hmi/hmi.h"
#include "tunnel.h"

static uint8_t StopPending(void) {
    uint8_t requested = EspTakeEmergencyRequest();
    return requested || (EspEmergencyLatched() && Tunnel_GetState() != TUNNEL_STOPPED);
}
static void StopAndFence(void) {
    (void)Tunnel_RemoteCommand(0x17U, 0U);
    EspFenceSerialInput();
    Hmi_DiscardActionsAfterStop();
}
void DwinControl_Process(void) {
    EspHmiCapture();
    Hmi_Process();
    if (StopPending()) {
        StopAndFence();
        return;
    }
    // Preserve the initial valid-link requirement. Transmitting a display
    // update alone does not prove that Emergency touches can reach the MCU.
    if (!g_hmi_debug.auto_upload_enabled && !g_hmi_debug.rx_event_count)
        return;
    EspPollSensorEdges();
    Tunnel_Process();
    if (StopPending())
        StopAndFence();
}
