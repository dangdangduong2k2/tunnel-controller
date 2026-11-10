#include "app_config.h"
#include "board.h"
#include "tunnel.h"
#include "tunnel_test_stubs.h"
#include <assert.h>
#include <stdio.h>

static void Run(uint32_t ms) {
    while (ms--) {
        TunnelTest_AdvanceTick(1U);
        Tunnel_Process();
    }
}
static void Boot(void) {
    TunnelTest_ResetStubs();
    Tunnel_Init();
    Run(1300U);
    assert(Tunnel_GetState() == TUNNEL_WAIT_SS1_LOW);
}
static void EnterClosing(void) {
    Boot();
    TunnelTest_SetSensor(SENSOR_SS1_PIN, 1U, 1U);
    Run(1100U);
    assert(Tunnel_GetState() == TUNNEL_CONVEYOR_TO_SS1_HIGH);
    TunnelTest_SetSensor(SENSOR_SS1_PIN, 0U, 1U);
    Run(1100U);
    assert(Tunnel_GetState() == TUNNEL_CLOSE_STEP_TO_SS2);
    assert(MotorPwm_IsRunning(STEP_DRIVER) && MotorPwm_IsRunning(STEP2_DRIVER));
}
static void Closed(uint16_t pin) {
    TunnelTest_SetSensor(pin, 1U, 1U);
    Run(70U);
}
static void TestAsymmetricClose(uint16_t first, MotorPwmId motor, uint16_t second,
                                MotorPwmId other) {
    EnterClosing();
    Closed(first);
    assert(!MotorPwm_IsRunning(motor) && MotorPwm_IsRunning(other));
    assert(Tunnel_GetState() == TUNNEL_CLOSE_STEP_TO_SS2);
    assert(tunnel_test_reader.start_calls == 0U);
    Closed(second);
    Run(50U);
    assert(Tunnel_GetState() == TUNNEL_CONVEYOR_INVENTORY_TO_SS3);
    assert(tunnel_test_reader.start_calls == 1U && MotorPwm_IsRunning(CONVEYOR_DRIVER));
    /* IN7 is a close/start limit; only IN2 remains a live inventory interlock. */
    TunnelTest_SetSensor(first, 0U, 1U);
    if (first == SENSOR_SS7_PIN) {
        Run(100U);
        assert(Tunnel_GetState() == TUNNEL_CONVEYOR_INVENTORY_TO_SS3);
        assert(Tunnel_GetFault() == TUNNEL_FAULT_NONE);
        assert(MotorPwm_IsRunning(CONVEYOR_DRIVER));
        assert(tunnel_test_reader.pause_calls == 0U);
        assert(!MotorPwm_IsRunning(STEP_DRIVER) && !MotorPwm_IsRunning(STEP2_DRIVER));
        TunnelTest_SetSensor(SENSOR_SS2_PIN, 0U, 1U);
    }
    assert(!MotorPwm_IsRunning(CONVEYOR_DRIVER));
    assert(tunnel_test_reader.pause_calls != 0U);
    Run(1U);
    assert(Tunnel_GetState() == TUNNEL_FAULT);
    assert(!MotorPwm_IsRunning(STEP_DRIVER) && !MotorPwm_IsRunning(STEP2_DRIVER));
}
static void TestInventoryPolling(void) {
    EnterClosing();
    Closed(SENSOR_SS2_PIN);
    Closed(SENSOR_SS7_PIN);
    Run(50U);
    assert(Tunnel_GetState() == TUNNEL_CONVEYOR_INVENTORY_TO_SS3);
    TunnelTest_SetSensor(SENSOR_SS7_PIN, 0U, 0U);
    Run(100U);
    assert(Tunnel_GetFault() == TUNNEL_FAULT_NONE);
    assert(Tunnel_GetState() == TUNNEL_CONVEYOR_INVENTORY_TO_SS3);
    assert(MotorPwm_IsRunning(CONVEYOR_DRIVER) && tunnel_test_reader.pause_calls == 0U);
    TunnelTest_SetSensor(SENSOR_SS2_PIN, 0U, 0U);
    Run(1U);
    assert(Tunnel_GetState() == TUNNEL_FAULT);
    assert(Tunnel_GetFault() == TUNNEL_FAULT_SENSOR_ORDER);
    assert(!MotorPwm_IsRunning(CONVEYOR_DRIVER));
}
static void TestBounce(void) {
    EnterClosing();
    tunnel_test_motors[STEP_DRIVER].pulse_count = 123U;
    tunnel_test_motors[STEP2_DRIVER].pulse_count = 456U;
    TunnelTest_SetSensor(SENSOR_SS2_PIN, 1U, 1U);
    Run(10U);
    assert(!MotorPwm_IsRunning(STEP_DRIVER) && MotorPwm_IsRunning(STEP2_DRIVER));
    TunnelTest_SetSensor(SENSOR_SS2_PIN, 0U, 1U);
    Run(70U);
    assert(MotorPwm_IsRunning(STEP_DRIVER));
    assert(tunnel_test_motors[STEP_DRIVER].last_profile_completed == 123U);
    assert(tunnel_test_motors[STEP2_DRIVER].start_profile_calls == 1U);
    assert(tunnel_test_motors[STEP2_DRIVER].pulse_count == 456U);
    assert(tunnel_test_reader.start_calls == 0U);
    Closed(SENSOR_SS2_PIN);
    Closed(SENSOR_SS7_PIN);
    Run(30U);
    assert(Tunnel_GetState() == TUNNEL_CONVEYOR_INVENTORY_TO_SS3);
}
static void TestMissingSecondLimit(void) {
    EnterClosing();
    Closed(SENSOR_SS2_PIN);
    TunnelTest_CompleteProfile(STEP2_DRIVER);
    Run(STEP_CLOSE_TIMEOUT_MS);
    assert(Tunnel_GetState() == TUNNEL_FAULT);
    assert(Tunnel_GetFault() == TUNNEL_FAULT_STEP_CLOSE_TIMEOUT);
    assert(tunnel_test_reader.start_calls == 0U && !MotorPwm_IsRunning(CONVEYOR_DRIVER));
}
static void TestHomeAndOpen(void) {
    Boot();
    Closed(SENSOR_SS2_PIN);
    assert(Tunnel_RemoteCommand(0x10U, 1U) == 0U);
    Run(100U);
    assert(!MotorPwm_IsRunning(STEP_DRIVER) && MotorPwm_IsRunning(STEP2_DRIVER));
    assert(tunnel_test_motors[STEP_DRIVER].start_profile_calls == 0U);
    Closed(SENSOR_SS7_PIN);
    assert(Tunnel_RemoteCommand(0x10U, 0U) == 0U);
    assert(MotorPwm_IsRunning(STEP_DRIVER) && MotorPwm_IsRunning(STEP2_DRIVER));
    TunnelTest_SetSensor(SENSOR_SS2_PIN, 0U, 1U);
    TunnelTest_SetSensor(SENSOR_SS7_PIN, 0U, 1U);
    TunnelTest_CompleteProfile(STEP_DRIVER);
    Run(20U);
    assert(Tunnel_GetState() == TUNNEL_MODE_OPEN_GATE && MotorPwm_IsRunning(STEP2_DRIVER));
    TunnelTest_CompleteProfile(STEP2_DRIVER);
    Run(70U);
    assert(Tunnel_GetState() == TUNNEL_WAIT_REARM);
}
static void TestEmergencyAndFreshReset(void) {
    EnterClosing();
    assert(Tunnel_RemoteCommand(0x17U, 0U) == 0U);
    assert(!MotorPwm_IsRunning(STEP_DRIVER) && !MotorPwm_IsRunning(STEP2_DRIVER));
    assert(!MotorPwm_IsRunning(CONVEYOR_DRIVER));
    assert(Tunnel_RemoteCommand(0x10U, 1U) != 0U);
    assert(Tunnel_RemoteCommand(0x18U, 0U) == 0U);
    Run(100U);
    assert(MotorPwm_IsRunning(STEP_DRIVER) && MotorPwm_IsRunning(STEP2_DRIVER));
    tunnel_test_motors[STEP_DRIVER].pulse_count = 100U;
    tunnel_test_motors[STEP2_DRIVER].pulse_count = 350U;
    assert(Tunnel_RemoteCommand(0x18U, 0U) == 0U);
    Run(100U);
    assert(tunnel_test_motors[STEP_DRIVER].last_profile_completed == 100U);
    assert(tunnel_test_motors[STEP2_DRIVER].last_profile_completed == 350U);
    Closed(SENSOR_SS7_PIN);
    assert(MotorPwm_IsRunning(STEP_DRIVER));
    Closed(SENSOR_SS2_PIN);
    assert(Tunnel_GetState() == TUNNEL_STOPPED);
}
static void TestSecondDoorFailureStopsFirst(void) {
    EnterClosing();
    /* Unexpected driver stop without a limit is not mistaken for door closed. */
    MotorPwm_Stop(STEP2_DRIVER);
    Run(10U);
    assert(Tunnel_GetState() == TUNNEL_FAULT && !MotorPwm_IsRunning(STEP_DRIVER));
    assert(tunnel_test_reader.start_calls == 0U);
}
int main(void) {
    TestAsymmetricClose(SENSOR_SS2_PIN, STEP_DRIVER, SENSOR_SS7_PIN, STEP2_DRIVER);
    TestAsymmetricClose(SENSOR_SS7_PIN, STEP2_DRIVER, SENSOR_SS2_PIN, STEP_DRIVER);
    TestInventoryPolling();
    TestBounce();
    TestMissingSecondLimit();
    TestHomeAndOpen();
    TestEmergencyAndFreshReset();
    TestSecondDoorFailureStopsFirst();
    puts("PASS independent doors: either closes first, both required before RFID, IN7 loss "
         "continues RF/belt, IN2 loss stops via interrupt or polling, separate bounce budgets, "
         "missing IN7, homing/opening, Emergency, repeated Reset, second-driver failure");
    return 0;
}
