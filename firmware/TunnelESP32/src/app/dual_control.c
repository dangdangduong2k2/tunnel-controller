#include "dual_control.h"
#ifdef ARDUINO
#include "../platform/esp32/EspFota.h"
#include "../platform/esp32/EspDwinUpdate.h"
#endif
#include "../platform/esp32/EspPlatform.h"
#include "../hmi/hmi.h"
#include "../protocol/legacy/pc_control.h"
#include "../control/tunnel.h"
#include "../control/motor_pwm.h"
#if defined(ARDUINO) || defined(TUNNEL_MODBUS_TEST)
#include "../platform/esp32/EspModbus.h"
#if defined(ARDUINO) || defined(TUNNEL_CONFIG_TEST)
#include "../platform/esp32/EspConfig.h"
#endif
#endif

static uint8_t StopPending(void) {
    uint8_t requested = EspTakeEmergencyRequest();
    return requested || (EspEmergencyLatched() && !(Tunnel_GetStopFlags() & 1U));
}
static void StopAndFence(void) {
    if (MotorPwm_IsReleased())
        (void)Tunnel_RemoteCommand(0x17U, 0U);
    else
        Tunnel_StopForSafety(MotorPwm_GetStopReason());
    EspFenceSerialInput();
    PcControl_DiscardInput();
    Hmi_DiscardActionsAfterStop();
#if defined(ARDUINO) || defined(TUNNEL_MODBUS_TEST)
    EspModbusFence();
#endif
}
void DualControl_Process(void) {
    // Parse LCD first (Emergency cuts immediately), but do not execute ordinary
    // LCD events until PC input has had its opportunity to request STOP.
    EspHmiCapture();
    Hmi_Process();
#if defined(ARDUINO) || defined(TUNNEL_MODBUS_TEST)
    /* Honour a DWIN stop before consuming any newly received remote motion. */
    if (StopPending()) {
        StopAndFence();
        return;
    }
    EspModbusPoll();
#endif
    /* Shared UART captures complete PC STOP packets out of the ordinary
     * queue. Acknowledge STOP before fencing an older queued Reset. */
    PcControl_ProcessEmergency();
    if (StopPending()) {
        StopAndFence();
        return;
    }
#if defined(ARDUINO) || defined(TUNNEL_CONFIG_TEST)
    if (EspConfigBusy()) {
        return;
    }
#endif
#ifdef ARDUINO
    if (EspFotaBusy() || EspDwinUpdateHold()) return;
#endif
    EspPcPoll();
    if (StopPending()) {
        StopAndFence();
        return;
    }
    // PC monitoring and reader setup work even when no DWIN is attached.
    if (!g_hmi_debug.auto_upload_enabled && !g_hmi_debug.rx_event_count &&
        !PcControl_HasStatusRequest()
#if defined(ARDUINO) || defined(TUNNEL_MODBUS_TEST)
        && !EspModbusActive()
#endif
    ) {
        return;
    }
    EspPollSensorEdges();
    Tunnel_Process();
    if (StopPending()) {
        StopAndFence();
    }
}
