/* Run the real tunnel state machine; simulate sensors, motors and reader. */
#define main InheritedTwoDoorRegression
#include "test_two_doors.c"
#undef main

static void WaitState(TunnelState state, uint32_t timeout) {
    while (Tunnel_GetState() != state && timeout--) {
        Run(1U);
    }
    assert(Tunnel_GetState() == state);
}

static void HomeBoth(void) {
    Boot();
    TunnelTest_QueueEvent(HMI_VP_BUTTON_RESET, 1U);
    Run(100U);
    Closed(SENSOR_SS2_PIN);
    Closed(SENSOR_SS7_PIN);
    assert(Tunnel_GetState() == TUNNEL_STOPPED);
}

static void TestConfirmedDoorNeverRecloses(uint8_t interrupt) {
    uint32_t starts;
    EnterClosing();
    Closed(SENSOR_SS7_PIN);
    starts = tunnel_test_motors[STEP2_DRIVER].start_profile_calls;
    TunnelTest_SetSensor(SENSOR_SS7_PIN, 0U, interrupt);
    Run(500U);
    assert(!MotorPwm_IsRunning(STEP2_DRIVER));
    assert(tunnel_test_motors[STEP2_DRIVER].start_profile_calls == starts);
    assert(MotorPwm_IsRunning(STEP_DRIVER));
    Closed(SENSOR_SS2_PIN);
    Run(100U);
    assert(Tunnel_GetState() == TUNNEL_CONVEYOR_INVENTORY_TO_SS3);
    assert(MotorPwm_IsRunning(CONVEYOR_DRIVER) && tunnel_test_reader.scan_active);
    /* The diagnostic pin is genuinely inactive; only the close decision is latched. */
    assert(HAL_GPIO_ReadPin(SENSOR_SS7_PORT, SENSOR_SS7_PIN) != SENSOR_ACTIVE_LEVEL);
    TunnelTest_SetSensor(SENSOR_SS2_PIN, 0U, interrupt);
    Run(10U);
    assert(Tunnel_GetState() == TUNNEL_FAULT && !MotorPwm_IsRunning(CONVEYOR_DRIVER));
}

static void TestDropAcrossReaderStartup(uint8_t before_start) {
    EnterClosing();
    tunnel_test_reader.ready = 0U;
    Closed(SENSOR_SS7_PIN);
    Closed(SENSOR_SS2_PIN);
    assert(Tunnel_GetState() == TUNNEL_WAIT_READER);
    if (before_start) {
        TunnelTest_SetSensor(SENSOR_SS7_PIN, 0U, 1U);
        Run(100U);
    }
    tunnel_test_reader.ready = 1U;
    WaitState(TUNNEL_WAIT_INVENTORY_START, 100U);
    if (!before_start) {
        TunnelTest_SetSensor(SENSOR_SS7_PIN, 0U, 1U);
    }
    Run(100U);
    assert(Tunnel_GetState() == TUNNEL_CONVEYOR_INVENTORY_TO_SS3);
    assert(MotorPwm_IsRunning(CONVEYOR_DRIVER) && tunnel_test_reader.scan_active);
    assert(Tunnel_GetFault() == TUNNEL_FAULT_NONE);
}

static void TestShortGlitchesCannotConfirm(void) {
    uint32_t starts;
    unsigned i;
    EnterClosing();
    Closed(SENSOR_SS2_PIN);
    for (i = 0U; i < 3U; ++i) {
        starts = tunnel_test_motors[STEP2_DRIVER].start_profile_calls;
        TunnelTest_SetSensor(SENSOR_SS7_PIN, 1U, 1U);
        Run(10U);
        assert(!MotorPwm_IsRunning(STEP2_DRIVER));
        TunnelTest_SetSensor(SENSOR_SS7_PIN, 0U, 1U);
        Run(80U);
        assert(MotorPwm_IsRunning(STEP2_DRIVER));
        assert(tunnel_test_motors[STEP2_DRIVER].start_profile_calls == starts + 1U);
        assert(!tunnel_test_reader.scan_active);
    }
    TunnelTest_CompleteProfile(STEP2_DRIVER);
    Run(STEP_CLOSE_TIMEOUT_MS);
    assert(Tunnel_GetState() == TUNNEL_FAULT);
    assert(Tunnel_GetFault() == TUNNEL_FAULT_STEP_CLOSE_TIMEOUT);
    assert(!tunnel_test_reader.scan_active);
}

static void TestRepeatedResetWithinSameClose(void) {
    uint32_t starts;
    Boot();
    TunnelTest_QueueEvent(HMI_VP_BUTTON_RESET, 1U);
    Run(100U);
    Closed(SENSOR_SS7_PIN);
    starts = tunnel_test_motors[STEP2_DRIVER].start_profile_calls;
    TunnelTest_SetSensor(SENSOR_SS7_PIN, 0U, 1U);
    Run(100U);
    TunnelTest_QueueEvent(HMI_VP_BUTTON_RESET, 1U);
    Run(100U);
    assert(!MotorPwm_IsRunning(STEP2_DRIVER));
    assert(tunnel_test_motors[STEP2_DRIVER].start_profile_calls == starts);
    Closed(SENSOR_SS2_PIN);
    assert(Tunnel_GetState() == TUNNEL_STOPPED);
}

static void TestNewHomeAndEmergencyRequireFreshIn7(void) {
    unsigned emergency_first;
    for (emergency_first = 0U; emergency_first < 2U; ++emergency_first) {
        HomeBoth();
        TunnelTest_SetSensor(SENSOR_SS7_PIN, 0U, 1U);
        Run(100U);
        if (emergency_first) {
            TunnelTest_QueueEvent(HMI_VP_BUTTON_EMERGENCY, 1U);
            Run(10U);
            assert(MotorPwm_IsReleased());
            TunnelTest_QueueEvent(HMI_VP_BUTTON_SCAN, 1U);
            Run(10U);
            assert(!tunnel_test_reader.scan_active);
        }
        TunnelTest_QueueEvent(HMI_VP_BUTTON_RESET, 1U);
        Run(100U);
        assert(Tunnel_GetState() == TUNNEL_RETURN_GATE_TO_SS2);
        assert(!MotorPwm_IsRunning(STEP_DRIVER) && MotorPwm_IsRunning(STEP2_DRIVER));
        TunnelTest_CompleteProfile(STEP2_DRIVER);
        Run(10U);
        assert(Tunnel_GetState() == TUNNEL_FAULT);
        assert(Tunnel_GetFault() == TUNNEL_FAULT_HOME_LIMIT);
    }
}

static void TestRescanRetainsConfirmationUntilOpen(void) {
    Boot();
    TunnelTest_QueueEvent(HMI_VP_BUTTON_SEMI, 1U);
    Run(10U);
    TunnelTest_SetSensor(SENSOR_SS1_PIN, 1U, 1U);
    Run(1100U);
    TunnelTest_SetSensor(SENSOR_SS1_PIN, 0U, 1U);
    Run(1100U);
    Closed(SENSOR_SS2_PIN);
    Closed(SENSOR_SS7_PIN);
    Run(30U);
    assert(Tunnel_GetState() == TUNNEL_CONVEYOR_INVENTORY_TO_SS3);
    TunnelTest_SetSensor(SENSOR_SS7_PIN, 0U, 1U);
    Run(100U);
    TunnelTest_SetSensor(SENSOR_SS3_PIN, 1U, 1U);
    Run(300U);
    assert(Tunnel_GetState() == TUNNEL_WAIT_CONFIRM);
    TunnelTest_QueueEvent(HMI_VP_BUTTON_RESCAN, 1U);
    Run(30U);
    assert(MotorPwm_IsRunning(CONVEYOR_DRIVER));
    assert(tunnel_test_motors[CONVEYOR_DRIVER].direction == CONVEYOR_REVERSE_DIRECTION);
    TunnelTest_SetSensor(SENSOR_SS3_PIN, 0U, 1U);
    Run(70U);
    TunnelTest_SetSensor(SENSOR_SS1_PIN, 1U, 1U);
    Run(1100U);
    tunnel_test_reader.ready = 1U;
    Run(100U);
    assert(Tunnel_GetState() == TUNNEL_RESCAN_TO_SS3);
    assert(tunnel_test_reader.scan_active && MotorPwm_IsRunning(CONVEYOR_DRIVER));
    TunnelTest_SetSensor(SENSOR_SS1_PIN, 0U, 1U);
    Run(1100U);
    TunnelTest_SetSensor(SENSOR_SS3_PIN, 1U, 1U);
    Run(300U);
    assert(Tunnel_GetState() == TUNNEL_WAIT_CONFIRM);
    TunnelTest_QueueEvent(HMI_VP_BUTTON_CONFIRM, 1U);
    Run(10U);
    assert(Tunnel_GetState() == TUNNEL_OPEN_STEP);
    TunnelTest_SetSensor(SENSOR_SS2_PIN, 0U, 1U);
    TunnelTest_SetSensor(SENSOR_SS3_PIN, 0U, 1U);
    TunnelTest_CompleteGatePair();
    Run(300U);
    assert(Tunnel_GetState() == TUNNEL_WAIT_SS1_LOW);
    /* The next parcel must acquire IN7 again after this opening. */
    TunnelTest_SetSensor(SENSOR_SS1_PIN, 1U, 1U);
    Run(1100U);
    TunnelTest_SetSensor(SENSOR_SS1_PIN, 0U, 1U);
    Run(1100U);
    Closed(SENSOR_SS2_PIN);
    tunnel_test_reader.ready = 1U;
    assert(MotorPwm_IsRunning(STEP2_DRIVER) && !tunnel_test_reader.scan_active);
    TunnelTest_CompleteProfile(STEP2_DRIVER);
    Run(STEP_CLOSE_TIMEOUT_MS);
    assert(Tunnel_GetState() == TUNNEL_FAULT && !tunnel_test_reader.scan_active);
}

int main(void) {
    TestConfirmedDoorNeverRecloses(0U);
    TestConfirmedDoorNeverRecloses(1U);
    TestDropAcrossReaderStartup(0U);
    TestDropAcrossReaderStartup(1U);
    TestShortGlitchesCannotConfirm();
    TestRepeatedResetWithinSameClose();
    TestNewHomeAndEmergencyRequireFreshIn7();
    TestRescanRetainsConfirmationUntilOpen();
    puts("PASS IN7 confirmation: dropout before IN2/reader/ACK/Rescan, no extra closing, real raw "
         "status, 50ms debounce, fresh close/Reset/Emergency, missing IN7 timeout, IN2 interlock");
    return 0;
}
