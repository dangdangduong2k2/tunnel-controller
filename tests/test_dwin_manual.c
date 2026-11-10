/* Exercise the production shared UART, DWIN parser, cycle and RFID reader.
 * UART frames match the existing DWIN LCD display4 touch configuration. */
#include "test_dual_control.c"

static void DwinHome(void) {
    DualBoot();
    Lcd(HMI_VP_BUTTON_RESET, 1U);
    DualRun(100U);
    assert(Tunnel_GetState() == TUNNEL_RETURN_GATE_TO_SS2);
    assert(MotorPwm_IsRunning(STEP_DRIVER) && MotorPwm_IsRunning(STEP2_DRIVER));
    TunnelTest_SetSensor(SENSOR_SS2_PIN, 1U, 1U);
    DualRun(80U);
    assert(!MotorPwm_IsRunning(STEP_DRIVER) && MotorPwm_IsRunning(STEP2_DRIVER));
    TunnelTest_SetSensor(SENSOR_SS7_PIN, 1U, 1U);
    DualRun(80U);
    assert(Tunnel_GetState() == TUNNEL_STOPPED);
    assert((Tunnel_GetStopFlags() & 3U) == 2U);
}

static void TestGateAfterReset(void) {
    DwinHome();
    Lcd(HMI_VP_BUTTON_GATE, 0U);
    DualRun(10U);
    assert(Tunnel_GetState() == TUNNEL_MODE_OPEN_GATE);
    assert(MotorPwm_IsRunning(STEP_DRIVER) && MotorPwm_IsRunning(STEP2_DRIVER));
    /* Manual buttons must not take over an in-progress gate move. */
    Lcd(HMI_VP_BUTTON_CONVEYOR, 1U);
    Lcd(HMI_VP_BUTTON_SCAN, 1U);
    DualRun(10U);
    assert(!MotorPwm_IsRunning(CONVEYOR_DRIVER) && !ZK7182_IsScanActive());
    TunnelTest_SetSensor(SENSOR_SS2_PIN, 0U, 1U);
    TunnelTest_SetSensor(SENSOR_SS7_PIN, 0U, 1U);
    TunnelTest_CompleteGatePair();
    DualRun(200U);
    Lcd(HMI_VP_BUTTON_CONVEYOR, 0U);
    DualRun(10U);
    assert(!MotorPwm_IsRunning(CONVEYOR_DRIVER));
    Lcd(HMI_VP_BUTTON_GATE, 1U);
    DualRun(100U);
    assert(Tunnel_GetState() == TUNNEL_RETURN_GATE_TO_SS2);
    assert(MotorPwm_IsRunning(STEP_DRIVER) && MotorPwm_IsRunning(STEP2_DRIVER));
    TunnelTest_SetSensor(SENSOR_SS7_PIN, 1U, 1U);
    DualRun(80U);
    assert(MotorPwm_IsRunning(STEP_DRIVER) && !MotorPwm_IsRunning(STEP2_DRIVER));
    TunnelTest_SetSensor(SENSOR_SS2_PIN, 1U, 1U);
    DualRun(80U);
    assert(!MotorPwm_IsRunning(STEP_DRIVER) && !MotorPwm_IsRunning(STEP2_DRIVER));
}

static void TestScanAfterReset(void) {
    uint32_t sessions;
    DwinHome();
    DualRun(300U);
    assert(ZK7182_IsReady());
    sessions = g_debug.scan_session_count;
    Lcd(HMI_VP_BUTTON_SCAN, 1U);
    DualRun(600U);
    assert(ZK7182_IsScanActive() && rf_on);
    assert(g_debug.scan_session_count == sessions + 1U);
    assert(lcd_quantity == ZK7182_GetUniqueEpcCount());
    assert(!MotorPwm_IsRunning(STEP_DRIVER) && !MotorPwm_IsRunning(STEP2_DRIVER));
    assert(!MotorPwm_IsRunning(CONVEYOR_DRIVER));
    Lcd(HMI_VP_BUTTON_SCAN, 1U);
    DualRun(1200U);
    assert(!ZK7182_IsScanActive());
    Lcd(HMI_VP_BUTTON_SCAN, 1U);
    DualRun(600U);
    assert(ZK7182_IsScanActive() && g_debug.scan_session_count == sessions + 2U);

    DwinHome();
    TunnelTest_SetSensor(SENSOR_SS7_PIN, 0U, 1U);
    DualRun(80U);
    Lcd(HMI_VP_BUTTON_SCAN, 1U);
    DualRun(100U);
    assert(ZK7182_IsScanActive()); /* IN7 already confirmed during Reset. */
    assert(DualRequest(1U, NULL, 0U) == 0U);
    assert(!(reply[12] & 1U)); /* Raw IN7 status must remain truthful. */

    DwinHome();
    TunnelTest_SetSensor(SENSOR_SS2_PIN, 0U, 1U);
    DualRun(80U);
    Lcd(HMI_VP_BUTTON_SCAN, 1U);
    DualRun(100U);
    assert(ZK7182_IsScanActive()); /* Standalone RF ignores door position. */
    assert(Tunnel_GetFault() == TUNNEL_FAULT_NONE);

    DualBoot();
    TunnelTest_SetSensor(SENSOR_SS2_PIN, 1U, 1U);
    DualRun(80U);
    Lcd(HMI_VP_BUTTON_SCAN, 1U);
    DualRun(100U);
    assert(ZK7182_IsScanActive()); /* No home and IN7 never seen: RF only. */
    assert(Tunnel_GetFault() == TUNNEL_FAULT_NONE);
}

static void TestConveyorTapToggle(void) {
    uint32_t starts;
    DwinHome();
    /* The old panel sends 1 on touch-down, then 0 on release (mode 0x03). */
    Lcd(HMI_VP_BUTTON_CONVEYOR, 1U);
    DualRun(10U);
    assert(MotorPwm_IsRunning(CONVEYOR_DRIVER));
    assert(tunnel_test_motors[CONVEYOR_DRIVER].direction == CONVEYOR_DIRECTION);
    Lcd(HMI_VP_BUTTON_CONVEYOR, 0U);
    DualRun(200U);
    assert(MotorPwm_IsRunning(CONVEYOR_DRIVER));
    assert(tunnel_test_motors[CONVEYOR_DRIVER].direction == CONVEYOR_DIRECTION);
    Lcd(HMI_VP_BUTTON_CONVEYOR, 1U);
    DualRun(10U);
    assert(!MotorPwm_IsRunning(CONVEYOR_DRIVER));
    Lcd(HMI_VP_BUTTON_CONVEYOR, 0U);
    DualRun(10U);
    assert(!MotorPwm_IsRunning(CONVEYOR_DRIVER));

    Lcd(HMI_VP_BUTTON_CONVEYOR, 1U);
    DualRun(10U);
    starts = tunnel_test_motors[CONVEYOR_DRIVER].start_continuous_calls;
    Lcd(HMI_VP_BUTTON_CONVEYOR, 1U);
    DualRun(10U);
    assert(MotorPwm_IsRunning(CONVEYOR_DRIVER));
    assert(tunnel_test_motors[CONVEYOR_DRIVER].direction == CONVEYOR_DIRECTION);
    assert(tunnel_test_motors[CONVEYOR_DRIVER].start_continuous_calls == starts);
    /* Exit sensor still stops a latched run; held/duplicate touches cannot restart. */
    TunnelTest_SetSensor(SENSOR_SS3_PIN, 1U, 1U);
    DualRun(10U);
    assert(!MotorPwm_IsRunning(CONVEYOR_DRIVER));
    TunnelTest_SetSensor(SENSOR_SS3_PIN, 0U, 1U);
    DualRun(80U);
    Lcd(HMI_VP_BUTTON_CONVEYOR, 1U);
    DualRun(10U);
    assert(!MotorPwm_IsRunning(CONVEYOR_DRIVER));
    Lcd(HMI_VP_BUTTON_CONVEYOR, 0U);
    Lcd(HMI_VP_BUTTON_CONVEYOR, 1U);
    DualRun(10U);
    assert(MotorPwm_IsRunning(CONVEYOR_DRIVER));
    assert(tunnel_test_motors[CONVEYOR_DRIVER].direction == CONVEYOR_DIRECTION);
    Lcd(HMI_VP_BUTTON_CONVEYOR, 0U);
    DualRun(TUNNEL_CONVEYOR_TIMEOUT_MS + 10U);
    assert(!MotorPwm_IsRunning(CONVEYOR_DRIVER));
}

static void TestNoBypassOfStopOrPcOwnership(void) {
    uint8_t on = 1U, off = 0U;
    DwinHome();
    Lcd(HMI_VP_BUTTON_EMERGENCY, 1U);
    DualRun(10U);
    assert(MotorPwm_IsReleased());
    Lcd(HMI_VP_BUTTON_GATE, 0U);
    Lcd(HMI_VP_BUTTON_GATE, 1U);
    Lcd(HMI_VP_BUTTON_SCAN, 1U);
    Lcd(HMI_VP_BUTTON_CONVEYOR, 1U);
    DualRun(10U);
    CheckStopped();
    assert(!ZK7182_IsScanActive());
    Lcd(HMI_VP_BUTTON_RESET, 1U);
    DualRun(100U);
    assert(Tunnel_GetState() == TUNNEL_STOPPED && !(Tunnel_GetStopFlags() & 1U));
    /* A held key rejected before Reset must not turn into a fresh start. */
    Lcd(HMI_VP_BUTTON_CONVEYOR, 1U);
    DualRun(10U);
    assert(!MotorPwm_IsRunning(CONVEYOR_DRIVER));
    Lcd(HMI_VP_BUTTON_CONVEYOR, 0U);
    Lcd(HMI_VP_BUTTON_CONVEYOR, 1U);
    DualRun(10U);
    assert(MotorPwm_IsRunning(CONVEYOR_DRIVER));
    assert(tunnel_test_motors[CONVEYOR_DRIVER].direction == CONVEYOR_DIRECTION);

    assert(DualRequest(0x22U, &on, 1U) == 0U);
    Lcd(HMI_VP_BUTTON_RESET, 1U);
    Lcd(HMI_VP_BUTTON_GATE, 0U);
    Lcd(HMI_VP_BUTTON_SCAN, 1U);
    Lcd(HMI_VP_BUTTON_CONVEYOR, 0U);
    Lcd(HMI_VP_BUTTON_CONVEYOR, 1U);
    DualRun(10U);
    CheckStopped();
    assert(Hmi_IsControlLocked());
    assert(DualRequest(0x22U, &off, 1U) == 0U);
    Lcd(HMI_VP_BUTTON_GATE, 0U);
    Lcd(HMI_VP_BUTTON_SCAN, 1U);
    DualRun(300U);
    assert(ZK7182_IsScanActive()); /* A local explicit RF-only request is allowed. */
    assert(!MotorPwm_IsRunning(STEP_DRIVER) && !MotorPwm_IsRunning(STEP2_DRIVER));
    assert(!MotorPwm_IsRunning(CONVEYOR_DRIVER));
    assert(!Hmi_IsControlLocked());
    Lcd(HMI_VP_BUTTON_SCAN, 1U);
    DualRun(1200U);
    assert(!ZK7182_IsScanActive() && Tunnel_GetState() == TUNNEL_STOPPED);
    Lcd(HMI_VP_BUTTON_RESET, 1U);
    DualRun(100U);
    Lcd(HMI_VP_BUTTON_GATE, 0U);
    DualRun(10U);
    assert(MotorPwm_IsRunning(STEP_DRIVER) && MotorPwm_IsRunning(STEP2_DRIVER));
}

int main(void) {
    TestGateAfterReset();
    TestScanAfterReset();
    TestConveyorTapToggle();
    TestNoBypassOfStopOrPcOwnership();
    puts("PASS DWIN manual: Gate/Scan/Conveyor after Reset, independent IN2/IN7, scan restart/EPC, "
         "tap-run/tap-stop, release/repeat filtering, IN3/timeout cutoff, busy/stop/PC locks");
    return 0;
}
