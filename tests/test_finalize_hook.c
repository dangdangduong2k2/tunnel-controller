/* Result observer (Tunnel_SetFinalizeHook): exactly once per finalized result.
 * Reuses the fixtures of the inherited cycle suite without changing it. */
#define main TunnelHmiSuiteMain
#include "test_tunnel_hmi.c"
#undef main

static uint32_t calls, last_token, previous_token;
static uint16_t last_quantity;
static uint8_t last_semi;
static void Observe(uint8_t semi, uint16_t quantity, uint32_t cycle_token) {
    calls++;
    previous_token = last_token;
    last_semi = semi;
    last_quantity = quantity;
    last_token = cycle_token;
}
/* A parcel has left: clear the exit sensor so the next cycle can arm. */
static uint8_t NextParcel(void) {
    SetSensorDebounced(SENSOR_SS3_PIN, 0U);
    tunnel_test_reader.ready = 1U;
    return WaitForState(TUNNEL_WAIT_SS1_LOW, 2000U);
}

static void TestAutoReportsOnceAfterGateOpened(void) {
    current_test = "AUTO finalize";
    calls = 0U;
    if ((BootToIdle() == 0U) || (DriveCycleToInventory() == 0U)) {
        return;
    }
    if (FinishInventoryAtSs3(TUNNEL_OPEN_STEP, 3U, 1U) == 0U) {
        return;
    }
    AdvanceAndProcess(200U);
    CHECK_U32("AUTO not reported while the gate is still opening", 0U, calls);
    FinishOpenMoveToWaitRearm(TUNNEL_OPEN_STEP);
    CHECK_U32("AUTO reported once at completion", 1U, calls);
    CHECK_U32("AUTO mode", 0U, last_semi);
    CHECK_U32("AUTO quantity", 3U, last_quantity);
    AdvanceAndProcess(3000U);
    CHECK_U32("idle time never reports again", 1U, calls);
    /* The next parcel is a new result with a new token. */
    if ((NextParcel() == 0U) || (DriveCycleToInventory() == 0U) ||
        (FinishInventoryAtSs3(TUNNEL_OPEN_STEP, 9U, 0U) == 0U)) {
        return;
    }
    FinishOpenMoveToWaitRearm(TUNNEL_OPEN_STEP);
    CHECK_U32("second AUTO cycle reported once", 2U, calls);
    CHECK_U32("second AUTO quantity", 9U, last_quantity);
    CHECK_TRUE("each result has its own token", last_token != previous_token);
}

static void TestSemiReportsAtConfirmOnly(void) {
    current_test = "SEMI finalize";
    calls = 0U;
    if (BootToIdle() == 0U) {
        return;
    }
    QueueAndPump(TEST_VP_SEMI, 1U);
    QueueAndPump(TEST_VP_CONFIRM, 1U); /* outside WAIT_CONFIRM */
    if ((DriveCycleToInventory() == 0U) ||
        (FinishInventoryAtSs3(TUNNEL_WAIT_CONFIRM, 2U, 0U) == 0U)) {
        return;
    }
    AdvanceAndProcess(5000U); /* result page refreshes never finalize */
    CHECK_U32("waiting for the operator reports nothing", 0U, calls);
    if ((StartRescanAndReachForward() == 0U) ||
        (FinishInventoryAtSs3(TUNNEL_WAIT_CONFIRM, 5U, 1U) == 0U)) {
        return;
    }
    CHECK_U32("Rescan reports nothing", 0U, calls);
    QueueAndPump(TEST_VP_CONFIRM, 1U);
    if (WaitForState(TUNNEL_OPEN_STEP, 100U) == 0U) {
        return;
    }
    CHECK_U32("Confirm reports once", 1U, calls);
    CHECK_U32("SEMI mode", 1U, last_semi);
    CHECK_U32("quantity of the confirmed (rescanned) result", 5U, last_quantity);
    QueueAndPump(TEST_VP_CONFIRM, 1U);
    QueueAndPump(TEST_VP_CONFIRM, 1U);
    CHECK_U32("repeated Confirm while opening reports nothing", 1U, calls);
    FinishOpenMoveToWaitRearm(TUNNEL_OPEN_STEP);
    AdvanceAndProcess(1000U);
    CHECK_U32("opening completion does not report SEMI a second time", 1U, calls);
}

static void TestRemoteConfirmReportsOnce(void) {
    current_test = "remote Confirm finalize";
    calls = 0U;
    if (BootToIdle() == 0U) {
        return;
    }
    QueueAndPump(TEST_VP_SEMI, 1U);
    CHECK_U32("remote Confirm outside WAIT_CONFIRM is busy", 2U, Tunnel_RemoteCommand(0x15U, 0U));
    if ((DriveCycleToInventory() == 0U) ||
        (FinishInventoryAtSs3(TUNNEL_WAIT_CONFIRM, 11U, 0U) == 0U)) {
        return;
    }
    CHECK_U32("remote Rescan accepted", 0U, Tunnel_RemoteCommand(0x16U, 0U));
    CHECK_U32("remote Rescan reports nothing", 0U, calls);
    (void)Tunnel_RemoteCommand(0x17U, 0U); /* stop that rescan */
    CHECK_U32("stop reports nothing", 0U, calls);

    if (BootToIdle() == 0U) {
        return;
    }
    QueueAndPump(TEST_VP_SEMI, 1U);
    if ((DriveCycleToInventory() == 0U) ||
        (FinishInventoryAtSs3(TUNNEL_WAIT_CONFIRM, 12U, 0U) == 0U)) {
        return;
    }
    CHECK_U32("remote Confirm accepted", 0U, Tunnel_RemoteCommand(0x15U, 0U));
    CHECK_U32("remote Confirm reports once", 1U, calls);
    CHECK_U32("remote Confirm is a SEMI result", 1U, last_semi);
    CHECK_U32("remote Confirm quantity", 12U, last_quantity);
    CHECK_U32("second remote Confirm is busy", 2U, Tunnel_RemoteCommand(0x15U, 0U));
    CHECK_U32("second remote Confirm reports nothing", 1U, calls);
}

static void TestStopsAndFaultsNeverReport(void) {
    current_test = "no finalize on stop/fault/manual";
    calls = 0U;
    /* Emergency while the result is waiting for the operator. */
    if (BootToIdle() == 0U) {
        return;
    }
    QueueAndPump(TEST_VP_SEMI, 1U);
    if ((DriveCycleToInventory() == 0U) ||
        (FinishInventoryAtSs3(TUNNEL_WAIT_CONFIRM, 4U, 0U) == 0U)) {
        return;
    }
    TunnelTest_QueueEvent(TEST_VP_EMERGENCY, 1U);
    TunnelTest_QueueEvent(TEST_VP_CONFIRM, 1U);
    ProcessNow();
    (void)WaitForState(TUNNEL_STOPPED, 100U);
    QueueAndPump(TEST_VP_CONFIRM, 1U);
    AdvanceAndProcess(500U);
    CHECK_U32("Emergency in WAIT_CONFIRM reports nothing", 0U, calls);

    /* Emergency while an AUTO result's gate is opening. */
    if ((BootToIdle() == 0U) || (DriveCycleToInventory() == 0U) ||
        (FinishInventoryAtSs3(TUNNEL_OPEN_STEP, 6U, 0U) == 0U)) {
        return;
    }
    QueueAndPump(TEST_VP_EMERGENCY, 1U);
    (void)WaitForState(TUNNEL_STOPPED, 100U);
    AdvanceAndProcess(500U);
    CHECK_U32("Emergency during opening reports nothing", 0U, calls);

    /* Scan timeout fault (SS3 never reached). */
    if ((BootToIdle() == 0U) || (DriveCycleToInventory() == 0U)) {
        return;
    }
    TunnelTest_AdvanceTick(30000U);
    ProcessNow();
    CHECK_U32("missing SS3 is a fault", TUNNEL_FAULT, Tunnel_GetState());
    AdvanceAndProcess(500U);
    CHECK_U32("fault reports nothing", 0U, calls);

    /* Manual stationary scan. */
    if (BootToIdle() == 0U) {
        return;
    }
    QueueAndPump(TEST_VP_SCAN, 1U);
    AdvanceAndProcess(40U);
    CHECK_U32("manual scan running", 1U, tunnel_test_reader.scan_active);
    tunnel_test_reader.unique_count = 4U;
    QueueAndPump(TEST_VP_SCAN, 1U);
    AdvanceAndProcess(500U);
    CHECK_U32("manual scan reports nothing", 0U, calls);

    /* AUTO cycle whose reader never became ready: it completes with quantity 0
     * and a recorded READER_NOT_READY fault, which is not a finalized result. */
    if (BootToIdle() == 0U) {
        return;
    }
    tunnel_test_reader.ready = 0U;
    SetSensorDebounced(SENSOR_SS1_PIN, 1U);
    (void)WaitForState(TUNNEL_CONVEYOR_TO_SS1_HIGH, 100U);
    TunnelTest_SetPairedSensor(SENSOR_SS1_PIN, 0U, 1U);
    AdvanceAndProcess(SENSOR_SS1_DEBOUNCE_MS + 10U);
    (void)WaitForState(TUNNEL_CLOSE_STEP_TO_SS2, 100U);
    TunnelTest_SetPairedSensor(SENSOR_SS2_PIN, 1U, 1U);
    if ((WaitForState(TUNNEL_WAIT_READER, 500U) == 0U) ||
        (WaitForState(TUNNEL_OPEN_STEP, TUNNEL_READER_READY_TIMEOUT_MS + 500U) == 0U)) {
        return;
    }
    CHECK_U32("unavailable reader recorded", TUNNEL_FAULT_READER_NOT_READY, Tunnel_GetFault());
    FinishOpenMoveToWaitRearm(TUNNEL_OPEN_STEP);
    CHECK_U32("degraded AUTO cycle reports nothing", 0U, calls);

    /* Mode selection after Reset opens the gate (MODE_OPEN_GATE): not a result. */
    if ((BootToIdle() == 0U) || (DriveCycleToInventory() == 0U)) {
        return;
    }
    QueueAndPump(TEST_VP_EMERGENCY, 1U);
    (void)WaitForState(TUNNEL_STOPPED, 100U);
    QueueAndPump(TEST_VP_RESET, 1U);
    (void)WaitForState(TUNNEL_STOPPED, 200U);
    QueueAndPump(TEST_VP_AUTO, 0U);
    if (WaitForState(TUNNEL_MODE_OPEN_GATE, 100U) == 0U) {
        return;
    }
    FinishOpenMoveToWaitRearm(TUNNEL_MODE_OPEN_GATE);
    CHECK_U32("mode gate opening reports nothing", 0U, calls);
}

int main(void) {
    failure_count = 0U;
    Tunnel_SetFinalizeHook(Observe);
    TestAutoReportsOnceAfterGateOpened();
    TestSemiReportsAtConfirmOnly();
    TestRemoteConfirmReportsOnce();
    TestStopsAndFaultsNeverReport();
    /* Unregistered again: the state machine runs exactly as before. */
    Tunnel_SetFinalizeHook(0);
    calls = 0U;
    TestStartupAndAutoCycle();
    TestSemiConfirmAndRepeatedRescan();
    CHECK_U32("no observer, no report", 0U, calls);
    if (failure_count != 0U) {
        (void)printf("finalize hook: %u failure(s)\n", failure_count);
        return 1;
    }
    (void)printf("finalize hook: PASS (AUTO once at completion, SEMI once at Confirm incl. "
                 "remote, none for Rescan/manual/fault/Emergency/degraded cycle/mode opening)\n");
    return 0;
}
