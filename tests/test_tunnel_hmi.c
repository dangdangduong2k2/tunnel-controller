#include "app_config.h"
#include "board.h"
#include "debug_state.h"
#include "tunnel.h"
#include "tunnel_test_stubs.h"

#include <stdint.h>
#include <stdio.h>

#define TEST_VP_CONVEYOR 0x1001U
#define TEST_VP_GATE 0x1010U
#define TEST_VP_SCAN 0x1020U
#define TEST_VP_AUTO 0x1030U
#define TEST_VP_EMERGENCY 0x1040U
#define TEST_VP_RESET 0x1050U
#define TEST_VP_SEMI 0x1060U
#define TEST_VP_CONFIRM 0x1070U
#define TEST_VP_RESCAN 0x1080U

static unsigned int failure_count;
static const char *current_test;

#define CHECK_TRUE(label, condition)                                                               \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)printf("FAIL [%s] %s\n", current_test, (label));                                 \
            failure_count++;                                                                       \
        }                                                                                          \
    } while (0)

#define CHECK_U32(label, expected_value, actual_value)                                             \
    do {                                                                                           \
        uint32_t const check_expected = (uint32_t)(expected_value);                                \
        uint32_t const check_actual = (uint32_t)(actual_value);                                    \
        if (check_expected != check_actual) {                                                      \
            (void)printf("FAIL [%s] %s: expected %lu, got %lu\n", current_test, (label),           \
                         (unsigned long)check_expected, (unsigned long)check_actual);              \
            failure_count++;                                                                       \
        }                                                                                          \
    } while (0)

static void ProcessNow(void) {
    Tunnel_Process();
}

static void AdvanceAndProcess(uint32_t milliseconds) {
    uint32_t step;

    while (milliseconds != 0U) {
        step = (milliseconds > 5U) ? 5U : milliseconds;
        TunnelTest_AdvanceTick(step);
        Tunnel_Process();
        milliseconds -= step;
    }
}

static uint8_t WaitForState(TunnelState expected, uint32_t timeout_ms) {
    uint32_t elapsed;

    elapsed = 0U;
    while ((Tunnel_GetState() != expected) && (elapsed < timeout_ms)) {
        AdvanceAndProcess(5U);
        elapsed += 5U;
    }
    if (Tunnel_GetState() != expected) {
        (void)printf("FAIL [%s] wait state %u: got %u after %lu ms\n", current_test,
                     (unsigned int)expected, (unsigned int)Tunnel_GetState(),
                     (unsigned long)elapsed);
        failure_count++;
        return 0U;
    }
    return 1U;
}

static uint8_t WaitForMotorRunning(MotorPwmId motor, uint32_t timeout_ms) {
    uint32_t elapsed;

    elapsed = 0U;
    while ((tunnel_test_motors[motor].running == 0U) && (elapsed < timeout_ms)) {
        AdvanceAndProcess(5U);
        elapsed += 5U;
    }
    if (tunnel_test_motors[motor].running == 0U) {
        (void)printf("FAIL [%s] motor %u did not start after %lu ms\n", current_test,
                     (unsigned int)motor, (unsigned long)elapsed);
        failure_count++;
        return 0U;
    }
    return 1U;
}

static void QueueAndPump(uint16_t vp, uint16_t value) {
    TunnelTest_QueueEvent(vp, value);
    ProcessNow();
}

static void PumpQueuedEvents(void) {
    uint32_t index;

    for (index = 0U; index < 12U; index++) {
        ProcessNow();
    }
}

static void SetSensorDebounced(uint16_t pin, uint8_t active) {
    TunnelTest_SetPairedSensor(pin, active, 1U);
    AdvanceAndProcess(((pin == SENSOR_SS1_PIN) ? SENSOR_SS1_DEBOUNCE_MS : SENSOR_DEBOUNCE_MS) +
                      10U);
}

static uint8_t BootToIdle(void) {
    TunnelTest_ResetStubs();
    Tunnel_Init();

    CHECK_U32("boot enters nonblocking startup wait", TUNNEL_STARTUP_WAIT, Tunnel_GetState());
    CHECK_U32("default mode is AUTO", TUNNEL_MODE_AUTO, Tunnel_GetMode());
    AdvanceAndProcess(995U);
    CHECK_U32("startup wait lasts at least 1000 ms", TUNNEL_STARTUP_WAIT, Tunnel_GetState());
    CHECK_U32("no gate PWM during startup", 0U, tunnel_test_motors[STEP_DRIVER].running);
    CHECK_U32("no conveyor PWM during startup", 0U, tunnel_test_motors[CONVEYOR_DRIVER].running);
    AdvanceAndProcess(10U);
    if (WaitForState(TUNNEL_WAIT_SS1_LOW, 250U) == 0U) {
        return 0U;
    }
    return 1U;
}

static uint8_t DriveCycleToInventory(void) {
    uint32_t close_starts;

    if (Tunnel_GetState() != TUNNEL_WAIT_SS1_LOW) {
        CHECK_U32("cycle begins idle", TUNNEL_WAIT_SS1_LOW, Tunnel_GetState());
        return 0U;
    }

    SetSensorDebounced(SENSOR_SS1_PIN, 1U);
    if (WaitForState(TUNNEL_CONVEYOR_TO_SS1_HIGH, 100U) == 0U) {
        return 0U;
    }
    CHECK_U32("first conveyor direction", CONVEYOR_DIRECTION,
              tunnel_test_motors[CONVEYOR_DRIVER].direction);
    CHECK_U32("first conveyor running", 1U, tunnel_test_motors[CONVEYOR_DRIVER].running);
    CHECK_U32("medium conveyor period", 10500U, tunnel_test_motors[CONVEYOR_DRIVER].period_ns);
    CHECK_U32("no inventory while entering with open gate", 0U, tunnel_test_reader.scan_active);

    close_starts = tunnel_test_motors[STEP_DRIVER].start_profile_calls;
    TunnelTest_SetPairedSensor(SENSOR_SS1_PIN, 0U, 1U);
    CHECK_U32("raw SS1 release stops conveyor", 0U, tunnel_test_motors[CONVEYOR_DRIVER].running);
    AdvanceAndProcess(SENSOR_SS1_DEBOUNCE_MS + 10U);
    if (WaitForState(TUNNEL_CLOSE_STEP_TO_SS2, 100U) == 0U) {
        return 0U;
    }
    CHECK_U32("close profile started once", close_starts + 1U,
              tunnel_test_motors[STEP_DRIVER].start_profile_calls);
    CHECK_U32("close direction", STEP_CLOSE_DIRECTION, tunnel_test_motors[STEP_DRIVER].direction);
    CHECK_U32("close budget", STEP_CLOSE_PULSE_COUNT, tunnel_test_motors[STEP_DRIVER].pulse_target);
    CHECK_U32("no inventory while gate is closing", 0U, tunnel_test_reader.scan_active);

    TunnelTest_SetPairedSensor(SENSOR_SS2_PIN, 1U, 1U);
    CHECK_U32("raw SS2 limit stops closing", 0U, tunnel_test_motors[STEP_DRIVER].running);
    AdvanceAndProcess(SENSOR_DEBOUNCE_MS - 10U);
    CHECK_U32("SS2 must be debounced before inventory", 0U, tunnel_test_reader.scan_active);
    AdvanceAndProcess(SENSOR_DEBOUNCE_MS + 10U);
    if (WaitForState(TUNNEL_CONVEYOR_INVENTORY_TO_SS3, 200U) == 0U) {
        return 0U;
    }
    CHECK_U32("inventory scan started", 1U, tunnel_test_reader.scan_active);
    CHECK_U32("inventory conveyor forward", CONVEYOR_DIRECTION,
              tunnel_test_motors[CONVEYOR_DRIVER].direction);
    CHECK_U32("inventory conveyor exact 10.5us", 10500U,
              tunnel_test_motors[CONVEYOR_DRIVER].period_ns);
    CHECK_U32("inventory has one 30s budget", 30000U, tunnel_test_reader.last_duration_ms);
    return 1U;
}

static uint8_t DriveThroughCenterToFinal(uint32_t first_run_ms) {
    AdvanceAndProcess(first_run_ms);
    CHECK_U32("single-pass forward direction", CONVEYOR_DIRECTION,
              tunnel_test_motors[CONVEYOR_DRIVER].direction);
    return 1U;
}

static uint8_t FinishInventoryAtSs3(TunnelState expected, uint16_t quantity,
                                    uint8_t exercise_drain) {
    tunnel_test_reader.unique_count = quantity;
    if ((Tunnel_GetState() == TUNNEL_CONVEYOR_INVENTORY_TO_SS3) ||
        (Tunnel_GetState() == TUNNEL_RESCAN_TO_SS3)) {
        if (DriveThroughCenterToFinal(1000U) == 0U) {
            return 0U;
        }
    }
    tunnel_test_reader.response_pending = exercise_drain;
    TunnelTest_SetPairedSensor(SENSOR_SS3_PIN, 1U, 1U);
    CHECK_U32("raw SS3 stops conveyor", 0U, tunnel_test_motors[CONVEYOR_DRIVER].running);
    CHECK_U32("raw SS3 pauses reader", 1U, tunnel_test_reader.paused);
    AdvanceAndProcess(SENSOR_DEBOUNCE_MS + 10U);

    if (exercise_drain != 0U) {
        CHECK_TRUE("pending inventory response delays result",
                   (Tunnel_GetState() == TUNNEL_CONVEYOR_INVENTORY_TO_SS3 ||
                    Tunnel_GetState() == TUNNEL_RESCAN_TO_SS3));
        tunnel_test_reader.response_pending = 0U;
    }
    if (WaitForState(expected, 250U) == 0U) {
        return 0U;
    }
    CHECK_U32("displayed unique quantity", quantity, tunnel_test_last_quantity);
    return 1U;
}

static void FinishOpenMoveToWaitRearm(TunnelState opening_state) {
    CHECK_U32("opening state", opening_state, Tunnel_GetState());
    CHECK_U32("open direction", STEP_OPEN_DIRECTION, tunnel_test_motors[STEP_DRIVER].direction);
    CHECK_U32("open budget exactly 5100", STEP_OPEN_PULSE_COUNT,
              tunnel_test_motors[STEP_DRIVER].pulse_target);
    SetSensorDebounced(SENSOR_SS2_PIN, 0U);
    TunnelTest_CompleteGatePair();
    (void)WaitForState(TUNNEL_WAIT_REARM, 100U);
}

static uint8_t StartRescanAndReachForward(void) {
    uint32_t scan_starts;
    uint32_t close_starts;
    uint32_t reverse_stop_tick;

    scan_starts = tunnel_test_reader.start_calls;
    close_starts = tunnel_test_motors[STEP_DRIVER].start_profile_calls;
    QueueAndPump(TEST_VP_RESCAN, 1U);
    if (WaitForState(TUNNEL_RESCAN_TO_SS1, 100U) == 0U) {
        return 0U;
    }
    if (WaitForMotorRunning(CONVEYOR_DRIVER, 100U) == 0U) {
        return 0U;
    }
    CHECK_U32("rescan conveyor reverses", CONVEYOR_REVERSE_DIRECTION,
              tunnel_test_motors[CONVEYOR_DRIVER].direction);
    CHECK_U32("reverse conveyor uses medium speed", 10500U,
              tunnel_test_motors[CONVEYOR_DRIVER].period_ns);
    CHECK_U32("held SS3 does not stop reverse", 1U, tunnel_test_motors[CONVEYOR_DRIVER].running);
    AdvanceAndProcess(HMI_STATUS_PERIOD_MS + 5U);
    CHECK_U32("Rescan republishes control page after DWIN local page jump", 1U,
              tunnel_test_last_page);
    CHECK_U32("held SS3 remains ignored throughout reverse", 1U,
              tunnel_test_motors[CONVEYOR_DRIVER].running);

    TunnelTest_SetPairedSensor(SENSOR_SS1_PIN, 1U, 1U);
    CHECK_U32("raw SS1 stops reverse conveyor", 0U, tunnel_test_motors[CONVEYOR_DRIVER].running);
    reverse_stop_tick = tunnel_test_motors[CONVEYOR_DRIVER].last_stop_tick;
    AdvanceAndProcess(SENSOR_SS1_DEBOUNCE_MS + 10U);
    CHECK_U32("SS1 on reverse never starts automatic close", close_starts,
              tunnel_test_motors[STEP_DRIVER].start_profile_calls);
    if (WaitForState(TUNNEL_RESCAN_WAIT_READER, 100U) == 0U) {
        return 0U;
    }

    /* The old SS3 LOW is deliberately not a new end-of-scan edge. */
    tunnel_test_reader.ready = 1U;
    AdvanceAndProcess(30U);
    CHECK_U32("forward waits until old SS3 clears", TUNNEL_RESCAN_WAIT_READER, Tunnel_GetState());
    CHECK_U32("reader not restarted under held SS3", scan_starts, tunnel_test_reader.start_calls);

    SetSensorDebounced(SENSOR_SS3_PIN, 0U);
    if (WaitForState(TUNNEL_RESCAN_TO_SS3, 200U) == 0U) {
        return 0U;
    }
    CHECK_U32("fresh rescan session", scan_starts + 1U, tunnel_test_reader.start_calls);
    CHECK_U32("fresh rescan clears unique count", 0U, tunnel_test_reader.unique_count);
    CHECK_U32("rescan conveyor forward", CONVEYOR_DIRECTION,
              tunnel_test_motors[CONVEYOR_DRIVER].direction);
    CHECK_TRUE("direction reversal delay honored",
               (tunnel_test_motors[CONVEYOR_DRIVER].last_start_tick - reverse_stop_tick) >=
                   DIRECTION_REVERSAL_DELAY_MS);

    SetSensorDebounced(SENSOR_SS1_PIN, 0U);
    CHECK_U32("clearing SS1 leaves rescan forward state", TUNNEL_RESCAN_TO_SS3, Tunnel_GetState());
    return 1U;
}

static void TestInventoryContinuesWhileConveyorTravels(void) {
    uint32_t starts;
    uint32_t pauses;
    uint32_t cancels;

    current_test = "continuous inventory during longer conveyor travel";
    if ((BootToIdle() == 0U) || (DriveCycleToInventory() == 0U)) {
        return;
    }
    starts = tunnel_test_reader.start_calls;
    pauses = tunnel_test_reader.pause_calls;
    cancels = tunnel_test_reader.cancel_calls;
    tunnel_test_reader.unique_count = 12U;
    /* One session across travel, reversal, dwell and the final response. */
    AdvanceAndProcess(5000U);
    CHECK_U32("still transferring before SS3", TUNNEL_CONVEYOR_INVENTORY_TO_SS3, Tunnel_GetState());
    CHECK_U32("conveyor still moving after 5s", 1U, tunnel_test_motors[CONVEYOR_DRIVER].running);
    CHECK_U32("reader remains active", 1U, tunnel_test_reader.scan_active);
    CHECK_U32("no session reset during motion", starts, tunnel_test_reader.start_calls);
    CHECK_U32("no reader pause during motion", pauses, tunnel_test_reader.pause_calls);
    CHECK_U32("no cancel during motion", cancels, tunnel_test_reader.cancel_calls);
    CHECK_U32("count retained for whole travel", 12U, tunnel_test_reader.unique_count);
    (void)FinishInventoryAtSs3(TUNNEL_OPEN_STEP, 15U, 1U);
}

static void TestStartupAndAutoCycle(void) {
    current_test = "startup + AUTO cycle";
    if ((BootToIdle() == 0U) || (DriveCycleToInventory() == 0U)) {
        return;
    }

    if (FinishInventoryAtSs3(TUNNEL_OPEN_STEP, 3U, 1U) == 0U) {
        return;
    }
    CHECK_U32("AUTO does not wait for Confirm", TUNNEL_OPEN_STEP, Tunnel_GetState());
    CHECK_U32("AUTO remains on control page", 1U, tunnel_test_last_page);
    FinishOpenMoveToWaitRearm(TUNNEL_OPEN_STEP);
}

static void TestSemiConfirmAndRepeatedRescan(void) {
    uint32_t open_starts;

    current_test = "SEMI Confirm + repeated Rescan";
    if (BootToIdle() == 0U) {
        return;
    }
    QueueAndPump(TEST_VP_SEMI, 1U);
    CHECK_U32("SEMI selected", TUNNEL_MODE_SEMI_AUTO, Tunnel_GetMode());

    /* Confirm outside WAIT_CONFIRM is inert. */
    open_starts = tunnel_test_motors[STEP_DRIVER].start_profile_calls;
    QueueAndPump(TEST_VP_CONFIRM, 1U);
    CHECK_U32("early Confirm does not move gate", open_starts,
              tunnel_test_motors[STEP_DRIVER].start_profile_calls);

    if ((DriveCycleToInventory() == 0U) ||
        (FinishInventoryAtSs3(TUNNEL_WAIT_CONFIRM, 2U, 0U) == 0U)) {
        return;
    }
    CHECK_U32("SEMI shows result page", 2U, tunnel_test_last_page);
    CHECK_U32("SEMI waits with gate stopped", 0U, tunnel_test_motors[STEP_DRIVER].running);

    if (StartRescanAndReachForward() == 0U) {
        return;
    }
    if (FinishInventoryAtSs3(TUNNEL_WAIT_CONFIRM, 5U, 1U) == 0U) {
        return;
    }

    /* A second complete Rescan proves the loop is not one-shot. */
    if (StartRescanAndReachForward() == 0U) {
        return;
    }
    if (FinishInventoryAtSs3(TUNNEL_WAIT_CONFIRM, 7U, 0U) == 0U) {
        return;
    }

    open_starts = tunnel_test_motors[STEP_DRIVER].start_profile_calls;
    QueueAndPump(TEST_VP_CONFIRM, 1U);
    if (WaitForState(TUNNEL_OPEN_STEP, 100U) == 0U) {
        return;
    }
    CHECK_U32("Confirm starts one opening profile", open_starts + 1U,
              tunnel_test_motors[STEP_DRIVER].start_profile_calls);
    QueueAndPump(TEST_VP_CONFIRM, 1U);
    CHECK_U32("repeated Confirm while opening is ignored", open_starts + 1U,
              tunnel_test_motors[STEP_DRIVER].start_profile_calls);
    FinishOpenMoveToWaitRearm(TUNNEL_OPEN_STEP);
}

static void TestBusyOwnershipAndModeLatch(void) {
    uint32_t conveyor_starts;
    uint32_t profile_starts;
    uint32_t reader_starts;

    current_test = "busy ownership + mode latch";
    if (BootToIdle() == 0U) {
        return;
    }

    /* AUTO's physical toggle reports either 0 or 1; both select AUTO. */
    QueueAndPump(TEST_VP_AUTO, 0U);
    CHECK_U32("AUTO value 0 still selects AUTO", TUNNEL_MODE_AUTO, Tunnel_GetMode());
    QueueAndPump(TEST_VP_SEMI, 1U);
    CHECK_U32("SEMI selected before cycle", TUNNEL_MODE_SEMI_AUTO, Tunnel_GetMode());

    SetSensorDebounced(SENSOR_SS1_PIN, 1U);
    if (WaitForState(TUNNEL_CONVEYOR_TO_SS1_HIGH, 100U) == 0U) {
        return;
    }
    conveyor_starts = tunnel_test_motors[CONVEYOR_DRIVER].start_continuous_calls;
    profile_starts = tunnel_test_motors[STEP_DRIVER].start_profile_calls;
    reader_starts = tunnel_test_reader.start_calls;

    TunnelTest_QueueEvent(TEST_VP_CONVEYOR, 0U);
    TunnelTest_QueueEvent(TEST_VP_GATE, 1U);
    TunnelTest_QueueEvent(TEST_VP_SCAN, 1U);
    TunnelTest_QueueEvent(TEST_VP_CONFIRM, 1U);
    TunnelTest_QueueEvent(TEST_VP_RESCAN, 1U);
    TunnelTest_QueueEvent(TEST_VP_AUTO, 0U);
    PumpQueuedEvents();

    CHECK_U32("busy manual commands do not change state", TUNNEL_CONVEYOR_TO_SS1_HIGH,
              Tunnel_GetState());
    CHECK_U32("busy conveyor command ignored", conveyor_starts,
              tunnel_test_motors[CONVEYOR_DRIVER].start_continuous_calls);
    CHECK_U32("busy conveyor remains owned by cycle", 1U,
              tunnel_test_motors[CONVEYOR_DRIVER].running);
    CHECK_U32("busy gate command ignored", profile_starts,
              tunnel_test_motors[STEP_DRIVER].start_profile_calls);
    CHECK_U32("busy scan command ignored", reader_starts, tunnel_test_reader.start_calls);
    CHECK_U32("busy mode event cannot change latched cycle mode", TUNNEL_MODE_SEMI_AUTO,
              Tunnel_GetMode());
}

static void TestManualScanToggle(void) {
    current_test = "manual scan toggle";
    if (BootToIdle() == 0U) {
        return;
    }

    QueueAndPump(TEST_VP_SCAN, 0U);
    AdvanceAndProcess(20U);
    CHECK_U32("Scan value 0 is not a physical key press", 0U, tunnel_test_reader.start_calls);
    CHECK_U32("Scan value 0 leaves idle unchanged", TUNNEL_WAIT_SS1_LOW, Tunnel_GetState());

    QueueAndPump(TEST_VP_SCAN, 1U);
    AdvanceAndProcess(20U);
    CHECK_U32("standalone scan starts with open gates", 1U, tunnel_test_reader.start_calls);
    CHECK_U32("independent scan has no sensor fault", TUNNEL_FAULT_NONE, Tunnel_GetFault());
    if (WaitForState(TUNNEL_MANUAL, 100U) == 0U) {
        return;
    }
    if (tunnel_test_reader.scan_active == 0U) {
        AdvanceAndProcess(20U);
    }
    CHECK_U32("first identical Scan key starts manual scan", 1U, tunnel_test_reader.start_calls);
    CHECK_U32("manual scan active", 1U, tunnel_test_reader.scan_active);

    QueueAndPump(TEST_VP_SCAN, 0U);
    AdvanceAndProcess(10U);
    CHECK_U32("Scan value 0 cannot toggle an active scan off", 0U, tunnel_test_reader.paused);

    tunnel_test_reader.unique_count = 4U;
    tunnel_test_reader.response_pending = 1U;
    QueueAndPump(TEST_VP_SCAN, 1U);
    CHECK_U32("second identical Scan key pauses immediately", 1U, tunnel_test_reader.paused);
    AdvanceAndProcess(100U);
    CHECK_U32("manual stop drains in-flight response", 1U, tunnel_test_reader.scan_active);
    tunnel_test_reader.response_pending = 0U;
    AdvanceAndProcess(10U);
    CHECK_U32("manual scan canceled after drain", 0U, tunnel_test_reader.scan_active);
    CHECK_TRUE("manual scan cancel recorded", tunnel_test_reader.cancel_calls != 0U);
}

static void TestGateReopensWhileWaitingForReader(void) {
    current_test = "gate opens while manual scan waits for reader";
    if (BootToIdle() == 0U) {
        return;
    }
    SetSensorDebounced(SENSOR_SS2_PIN, 1U);
    tunnel_test_reader.ready = 0U;
    QueueAndPump(TEST_VP_SCAN, 1U);
    CHECK_U32("unready reader leaves machine idle", TUNNEL_WAIT_SS1_LOW, Tunnel_GetState());
    CHECK_U32("no request before reader ready", 0U, tunnel_test_reader.start_calls);
    /* A rejected request is not queued to run when the reader becomes ready. */
    TunnelTest_SetPairedSensor(SENSOR_SS2_PIN, 0U, 1U);
    tunnel_test_reader.ready = 1U;
    AdvanceAndProcess(5U);
    CHECK_U32("reader recovery cannot replay old request", 0U, tunnel_test_reader.start_calls);
    CHECK_U32("gate position is irrelevant to isolated RF", TUNNEL_FAULT_NONE, Tunnel_GetFault());
    QueueAndPump(TEST_VP_SCAN, 1U);
    AdvanceAndProcess(20U);
    CHECK_U32("fresh request scans with open gates", 1U, tunnel_test_reader.start_calls);
    CHECK_U32("standalone RF never starts belt", 0U,
              tunnel_test_motors[CONVEYOR_DRIVER].start_continuous_calls);
}

static void TestLiveHmiDetectionRepublishesState(void) {
    uint32_t indicator_count;
    uint32_t page_count;
    uint32_t quantity_count;

    current_test = "late HMI boot republish";
    if (BootToIdle() == 0U) {
        return;
    }
    CHECK_TRUE("pre-detection quantity was attempted", tunnel_test_quantity_count != 0U);
    CHECK_TRUE("pre-detection page was attempted", tunnel_test_page_count != 0U);
    CHECK_TRUE("pre-detection indicators were attempted", tunnel_test_indicator_count != 0U);
    quantity_count = tunnel_test_quantity_count;
    page_count = tunnel_test_page_count;
    indicator_count = tunnel_test_indicator_count;

    /* A valid config reply is stronger evidence than local HAL TX success. */
    g_hmi_debug.config_known = 1U;
    AdvanceAndProcess(HMI_STATUS_PERIOD_MS + 5U);
    CHECK_U32("live link republishes quantity", quantity_count + 1U, tunnel_test_quantity_count);
    CHECK_U32("live link republishes page", page_count + 1U, tunnel_test_page_count);
    CHECK_U32("live link republishes indicators", indicator_count + 1U,
              tunnel_test_indicator_count);
    CHECK_U32("republished control page", 1U, tunnel_test_last_page);
    CHECK_U32("republished AUTO indicator", 1U, tunnel_test_last_indicators[3]);
}

static void TestStaleConfirmBeforePopupIsIgnored(void) {
    uint32_t profile_starts;

    current_test = "stale Confirm before popup";
    if (BootToIdle() == 0U) {
        return;
    }
    QueueAndPump(TEST_VP_SEMI, 1U);
    if (DriveCycleToInventory() == 0U) {
        return;
    }

    tunnel_test_reader.unique_count = 9U;
    if (DriveThroughCenterToFinal(1000U) == 0U) {
        return;
    }
    TunnelTest_SetPairedSensor(SENSOR_SS3_PIN, 1U, 1U);
    profile_starts = tunnel_test_motors[STEP_DRIVER].start_profile_calls;
    QueueAndPump(TEST_VP_CONFIRM, 1U);
    AdvanceAndProcess(SENSOR_DEBOUNCE_MS + 10U);
    if (WaitForState(TUNNEL_WAIT_CONFIRM, 200U) == 0U) {
        return;
    }
    CHECK_U32("pre-popup Confirm did not open", profile_starts,
              tunnel_test_motors[STEP_DRIVER].start_profile_calls);
    AdvanceAndProcess(50U);
    CHECK_U32("WAIT_CONFIRM requires a fresh Confirm", TUNNEL_WAIT_CONFIRM, Tunnel_GetState());
    QueueAndPump(TEST_VP_CONFIRM, 1U);
    CHECK_U32("fresh Confirm opens", TUNNEL_OPEN_STEP, Tunnel_GetState());
}

static void TestResultPopupRefreshDoesNotConfirm(void) {
    uint32_t page_count;
    uint32_t quantity_count;
    uint32_t profile_starts;

    current_test = "result popup retries without gate movement";
    if (BootToIdle() == 0U) {
        return;
    }
    QueueAndPump(TEST_VP_SEMI, 1U);
    if ((DriveCycleToInventory() == 0U) ||
        (FinishInventoryAtSs3(TUNNEL_WAIT_CONFIRM, 6U, 0U) == 0U)) {
        return;
    }
    page_count = tunnel_test_page_count;
    quantity_count = tunnel_test_quantity_count;
    profile_starts = tunnel_test_motors[STEP_DRIVER].start_profile_calls;
    AdvanceAndProcess(HMI_RESULT_REFRESH_MS + HMI_STATUS_PERIOD_MS);
    CHECK_TRUE("result page is periodically resent", tunnel_test_page_count > page_count);
    CHECK_TRUE("frozen result quantity is periodically resent",
               tunnel_test_quantity_count > quantity_count);
    CHECK_U32("retry uses result page", HMI_PAGE_RESULT, tunnel_test_last_page);
    CHECK_U32("retry preserves frozen quantity", 6U, tunnel_test_last_quantity);
    CHECK_U32("page retry never automatically confirms", TUNNEL_WAIT_CONFIRM, Tunnel_GetState());
    CHECK_U32("page retry leaves gate stopped", profile_starts,
              tunnel_test_motors[STEP_DRIVER].start_profile_calls);

    QueueAndPump(TEST_VP_CONFIRM, 1U);
    FinishOpenMoveToWaitRearm(TUNNEL_OPEN_STEP);
    AdvanceAndProcess(HMI_STATUS_PERIOD_MS);
    page_count = tunnel_test_page_count;
    AdvanceAndProcess(HMI_RESULT_REFRESH_MS + HMI_STATUS_PERIOD_MS);
    CHECK_U32("control page is not repeatedly reloaded", page_count, tunnel_test_page_count);
    CHECK_U32("Confirm returns to control page", HMI_PAGE_CONTROL, tunnel_test_last_page);
}

static void TestRescanReaderReadyTimeout(void) {
    current_test = "Rescan reader-ready timeout";
    if (BootToIdle() == 0U) {
        return;
    }
    QueueAndPump(TEST_VP_SEMI, 1U);
    if ((DriveCycleToInventory() == 0U) ||
        (FinishInventoryAtSs3(TUNNEL_WAIT_CONFIRM, 2U, 0U) == 0U)) {
        return;
    }

    QueueAndPump(TEST_VP_RESCAN, 1U);
    if ((WaitForState(TUNNEL_RESCAN_TO_SS1, 100U) == 0U) ||
        (WaitForMotorRunning(CONVEYOR_DRIVER, 100U) == 0U)) {
        return;
    }
    SetSensorDebounced(SENSOR_SS3_PIN, 0U);
    SetSensorDebounced(SENSOR_SS1_PIN, 1U);
    if (WaitForState(TUNNEL_RESCAN_WAIT_READER, 100U) == 0U) {
        return;
    }
    CHECK_U32("cancelled reader remains unavailable", 0U, tunnel_test_reader.ready);
    TunnelTest_AdvanceTick(TUNNEL_READER_READY_TIMEOUT_MS);
    ProcessNow();
    if (WaitForState(TUNNEL_WAIT_CONFIRM, 100U) == 0U) {
        return;
    }
    CHECK_U32("unready reader returns rescan result", TUNNEL_WAIT_CONFIRM, Tunnel_GetState());
    CHECK_U32("unstarted rescan cannot reuse old count", 0U, tunnel_test_last_quantity);
    CHECK_U32("rescan reports reader-not-ready fault", TUNNEL_FAULT_READER_NOT_READY,
              Tunnel_GetFault());
    CHECK_U32("timeout keeps conveyor stopped", 0U, tunnel_test_motors[CONVEYOR_DRIVER].running);
}

static void TestEmergencyAtClosedGateNeedsFreshMode(void) {
    uint32_t profile_starts;

    current_test = "Emergency priority at closed gate";
    if (BootToIdle() == 0U) {
        return;
    }
    QueueAndPump(TEST_VP_SEMI, 1U);
    if (DriveCycleToInventory() == 0U) {
        return;
    }
    profile_starts = tunnel_test_motors[STEP_DRIVER].start_profile_calls;

    TunnelTest_QueueEvent(TEST_VP_CONFIRM, 1U);
    TunnelTest_QueueEvent(TEST_VP_EMERGENCY, 1U);
    TunnelTest_QueueEvent(TEST_VP_AUTO, 0U);
    ProcessNow();
    if (WaitForState(TUNNEL_STOPPED, 100U) == 0U) {
        return;
    }
    CHECK_U32("Emergency wins over ordinary queued commands", TUNNEL_STOPPED, Tunnel_GetState());
    CHECK_U32("already-low SS2 emits no home pulses", profile_starts,
              tunnel_test_motors[STEP_DRIVER].start_profile_calls);
    CHECK_U32("Emergency stops conveyor", 0U, tunnel_test_motors[CONVEYOR_DRIVER].running);
    CHECK_U32("Emergency cancels inventory", 0U, tunnel_test_reader.scan_active);
    CHECK_U32("post-Emergency queued AUTO was discarded", TUNNEL_MODE_SEMI_AUTO, Tunnel_GetMode());

    QueueAndPump(TEST_VP_AUTO, 0U);
    CHECK_U32("AUTO alone cannot release emergency", TUNNEL_STOPPED, Tunnel_GetState());
    QueueAndPump(TEST_VP_RESET, 1U);
    if (!WaitForState(TUNNEL_STOPPED, 200U)) {
        return;
    }
    QueueAndPump(TEST_VP_AUTO, 0U);
    if (WaitForState(TUNNEL_MODE_OPEN_GATE, 100U) == 0U) {
        return;
    }
    CHECK_U32("fresh AUTO releases stopped gate", TUNNEL_MODE_AUTO, Tunnel_GetMode());
    profile_starts = tunnel_test_motors[STEP_DRIVER].start_profile_calls;
    QueueAndPump(TEST_VP_SEMI, 1U);
    CHECK_U32("mode change ignored while release opening", TUNNEL_MODE_AUTO, Tunnel_GetMode());
    CHECK_U32("mode event cannot start second opening", profile_starts,
              tunnel_test_motors[STEP_DRIVER].start_profile_calls);
    FinishOpenMoveToWaitRearm(TUNNEL_MODE_OPEN_GATE);
}

static void TestResetHomeBounceAndRelease(void) {
    uint32_t profile_starts;

    current_test = "Reset home bounce + release";
    if (BootToIdle() == 0U) {
        return;
    }
    SetSensorDebounced(SENSOR_SS1_PIN, 1U);
    if (WaitForState(TUNNEL_CONVEYOR_TO_SS1_HIGH, 100U) == 0U) {
        return;
    }

    TunnelTest_QueueEvent(TEST_VP_RESET, 1U);
    TunnelTest_QueueEvent(TEST_VP_SEMI, 1U);
    ProcessNow();
    if (WaitForState(TUNNEL_RETURN_GATE_TO_SS2, 100U) == 0U) {
        return;
    }
    if (WaitForMotorRunning(STEP_DRIVER, 100U) == 0U) {
        return;
    }
    CHECK_U32("Reset immediately stops conveyor", 0U, tunnel_test_motors[CONVEYOR_DRIVER].running);
    CHECK_U32("Reset homes in close direction", STEP_CLOSE_DIRECTION,
              tunnel_test_motors[STEP_DRIVER].direction);
    CHECK_U32("Reset home is bounded to 6000", STEP_CLOSE_PULSE_COUNT,
              tunnel_test_motors[STEP_DRIVER].pulse_target);
    CHECK_U32("mode queued with Reset is discarded", TUNNEL_MODE_AUTO, Tunnel_GetMode());

    tunnel_test_motors[STEP_DRIVER].pulse_count = 123U;
    tunnel_test_motors[STEP2_DRIVER].pulse_count = 123U;
    TunnelTest_SetPairedSensor(SENSOR_SS2_PIN, 1U, 1U);
    CHECK_U32("raw home limit stops profile", 0U, tunnel_test_motors[STEP_DRIVER].running);
    AdvanceAndProcess(20U);
    TunnelTest_SetPairedSensor(SENSOR_SS2_PIN, 0U, 1U);
    AdvanceAndProcess(SENSOR_DEBOUNCE_MS + 10U);
    CHECK_U32("false SS2 bounce resumes home", 1U, tunnel_test_motors[STEP_DRIVER].running);
    CHECK_U32("home bounce preserves pulse budget", 123U,
              tunnel_test_motors[STEP_DRIVER].last_profile_completed);
    CHECK_U32("resumed home still capped at 6000", STEP_CLOSE_PULSE_COUNT,
              tunnel_test_motors[STEP_DRIVER].pulse_target);

    tunnel_test_motors[STEP_DRIVER].pulse_count = 321U;
    tunnel_test_motors[STEP2_DRIVER].pulse_count = 321U;
    SetSensorDebounced(SENSOR_SS2_PIN, 1U);
    if (WaitForState(TUNNEL_STOPPED, 100U) == 0U) {
        return;
    }
    CHECK_U32("homed Reset remains stopped", 0U, tunnel_test_motors[STEP_DRIVER].running);

    profile_starts = tunnel_test_motors[STEP_DRIVER].start_profile_calls;
    QueueAndPump(TEST_VP_SEMI, 1U);
    if (WaitForState(TUNNEL_MODE_OPEN_GATE, 100U) == 0U) {
        return;
    }
    CHECK_U32("fresh SEMI releases reset stop", TUNNEL_MODE_SEMI_AUTO, Tunnel_GetMode());
    CHECK_U32("release starts once", profile_starts + 1U,
              tunnel_test_motors[STEP_DRIVER].start_profile_calls);
    FinishOpenMoveToWaitRearm(TUNNEL_MODE_OPEN_GATE);
}

static void TestEmergencyDuringOpeningRehomes(void) {
    uint32_t starts;
    current_test = "Emergency during opening stops all outputs until Reset";
    if (!BootToIdle() || !DriveCycleToInventory() ||
        !FinishInventoryAtSs3(TUNNEL_OPEN_STEP, 3U, 0U)) {
        return;
    }
    starts = tunnel_test_motors[STEP_DRIVER].start_profile_calls;
    (void)MotorPwm_StartContinuous(MOTOR_PWM_DRIVER_2, 0U, 1000U);
    (void)MotorPwm_StartContinuous(MOTOR_PWM_DRIVER_3, 0U, 1000U);
    QueueAndPump(TEST_VP_EMERGENCY, 1U);
    CHECK_U32("stopped immediately", TUNNEL_STOPPED, Tunnel_GetState());
    CHECK_U32("all-driver emergency called", 1U, tunnel_test_emergency_stop_calls);
    for (uint8_t i = 0U; i < MOTOR_PWM_DRIVER_COUNT; ++i)
        CHECK_U32("all four drivers stopped", 0U, tunnel_test_motors[i].running);
    QueueAndPump(TEST_VP_AUTO, 1U);
    QueueAndPump(TEST_VP_GATE, 1U);
    QueueAndPump(TEST_VP_SCAN, 1U);
    AdvanceAndProcess(40000U);
    CHECK_U32("no automatic motion after emergency", starts,
              tunnel_test_motors[STEP_DRIVER].start_profile_calls);
    CHECK_U32("no scan", 0U, tunnel_test_reader.scan_active);
    CHECK_U32("manual request blocked until Reset", 2U, Tunnel_RemoteCommand(0x11U, 1U));
}

static void TestHomeNeverExceedsBudgetAndTimesOut(void) {
    uint32_t starts;

    current_test = "home pulse bound + timeout";
    if (BootToIdle() == 0U) {
        return;
    }
    SetSensorDebounced(SENSOR_SS1_PIN, 1U);
    if (WaitForState(TUNNEL_CONVEYOR_TO_SS1_HIGH, 100U) == 0U) {
        return;
    }
    QueueAndPump(TEST_VP_RESET, 1U);
    if (WaitForState(TUNNEL_RETURN_GATE_TO_SS2, 100U) == 0U) {
        return;
    }
    if (WaitForMotorRunning(STEP_DRIVER, 100U) == 0U) {
        return;
    }

    starts = tunnel_test_motors[STEP_DRIVER].start_profile_calls;
    TunnelTest_CompleteGatePair();
    AdvanceAndProcess(10U);
    CHECK_U32("home emitted at most configured budget", STEP_CLOSE_PULSE_COUNT,
              tunnel_test_motors[STEP_DRIVER].pulse_count);
    CHECK_U32("home does not restart another 6000 pulses", starts,
              tunnel_test_motors[STEP_DRIVER].start_profile_calls);

    if (Tunnel_GetState() != TUNNEL_FAULT) {
        TunnelTest_AdvanceTick(STEP_CLOSE_TIMEOUT_MS);
        ProcessNow();
    }
    CHECK_U32("missing SS2 ends in fault by timeout", TUNNEL_FAULT, Tunnel_GetState());
    CHECK_U32("fault leaves gate stopped", 0U, tunnel_test_motors[STEP_DRIVER].running);
}

static void TestRepeatedHomePreservesBudgetAndDeadline(void) {
    uint32_t repeat_tick;

    current_test = "repeated home preserves pulse budget + deadline";
    if (BootToIdle() == 0U) {
        return;
    }
    QueueAndPump(TEST_VP_RESET, 1U);
    if (WaitForMotorRunning(STEP_DRIVER, 100U) == 0U) {
        return;
    }
    AdvanceAndProcess(SENSOR_DEBOUNCE_MS);
    tunnel_test_motors[STEP_DRIVER].pulse_count = 123U;
    tunnel_test_motors[STEP2_DRIVER].pulse_count = 123U;

    repeat_tick = tunnel_test_tick;
    QueueAndPump(TEST_VP_RESET, 1U);
    CHECK_U32("repeated Reset stops immediately", 0U, tunnel_test_motors[STEP_DRIVER].running);
    AdvanceAndProcess(DIRECTION_REVERSAL_DELAY_MS - 5U);
    CHECK_U32("repeated home keeps reversal guard", 0U, tunnel_test_motors[STEP_DRIVER].running);
    if (WaitForMotorRunning(STEP_DRIVER, 100U) == 0U) {
        return;
    }
    CHECK_TRUE("repeat restart waits at least 20 ms",
               (tunnel_test_motors[STEP_DRIVER].last_start_tick - repeat_tick) >=
                   DIRECTION_REVERSAL_DELAY_MS);
    CHECK_U32("repeated Reset resumes existing pulse offset", 123U,
              tunnel_test_motors[STEP_DRIVER].last_profile_completed);
    CHECK_U32("repeated Reset retains original 6000-pulse limit", STEP_CLOSE_PULSE_COUNT,
              tunnel_test_motors[STEP_DRIVER].pulse_target);

    tunnel_test_motors[STEP_DRIVER].pulse_count = 321U;
    tunnel_test_motors[STEP2_DRIVER].pulse_count = 321U;
    QueueAndPump(TEST_VP_EMERGENCY, 1U);
    AdvanceAndProcess(40000U);
    CHECK_U32("Emergency during home stays stopped", TUNNEL_STOPPED, Tunnel_GetState());
    CHECK_U32("home does not resume", 0U, tunnel_test_motors[STEP_DRIVER].running);
}

static void TestSs1OneSecondDebounce(void) {
    current_test = "SS1 needs 1 second on both edges; SS2 remains 50 ms";
    if (BootToIdle() == 0U) {
        return;
    }
    CHECK_U32("SS1 debounce setting", 1000U, SENSOR_SS1_DEBOUNCE_MS);
    CHECK_U32("other sensor debounce unchanged", 50U, SENSOR_DEBOUNCE_MS);
    TunnelTest_SetPairedSensor(SENSOR_SS1_PIN, 1U, 1U);
    AdvanceAndProcess(600U);
    CHECK_U32("short SS1 LOW cannot start belt", TUNNEL_WAIT_SS1_LOW, Tunnel_GetState());
    SetSensorDebounced(SENSOR_SS1_PIN, 0U);
    CHECK_U32("rejected LOW leaves belt stopped", 0U, tunnel_test_motors[CONVEYOR_DRIVER].running);
    TunnelTest_SetPairedSensor(SENSOR_SS1_PIN, 1U, 1U);
    AdvanceAndProcess(5U); /* First poll observes the new level. */
    AdvanceAndProcess(SENSOR_SS1_DEBOUNCE_MS - 1U);
    CHECK_U32("no start before a full second of LOW", TUNNEL_WAIT_SS1_LOW, Tunnel_GetState());
    AdvanceAndProcess(6U);
    CHECK_U32("stable LOW starts belt", TUNNEL_CONVEYOR_TO_SS1_HIGH, Tunnel_GetState());
    TunnelTest_SetPairedSensor(SENSOR_SS1_PIN, 0U, 1U);
    CHECK_U32("raw HIGH still cuts PWM immediately", 0U,
              tunnel_test_motors[CONVEYOR_DRIVER].running);
    AdvanceAndProcess(5U);
    AdvanceAndProcess(SENSOR_SS1_DEBOUNCE_MS - 1U);
    CHECK_U32("no close before a full second of HIGH", TUNNEL_CONVEYOR_TO_SS1_HIGH,
              Tunnel_GetState());
    CHECK_U32("gate remains stopped while SS1 debounces", 0U,
              tunnel_test_motors[STEP_DRIVER].running);
    AdvanceAndProcess(6U);
    CHECK_U32("stable HIGH starts closing", TUNNEL_CLOSE_STEP_TO_SS2, Tunnel_GetState());
    SetSensorDebounced(SENSOR_SS2_PIN, 1U);
    (void)WaitForState(TUNNEL_CONVEYOR_INVENTORY_TO_SS3, 20U);
}

static void TestManualThirtySecondLimit(void) {
    uint32_t start;
    current_test = "manual scan stops at 30s, next ON gets fresh budget";
    if (BootToIdle() == 0U) {
        return;
    }
    SetSensorDebounced(SENSOR_SS2_PIN, 1U);
    QueueAndPump(TEST_VP_SCAN, 1U);
    AdvanceAndProcess(10U);
    CHECK_U32("manual scan starts", 1U, tunnel_test_reader.scan_active);
    start = tunnel_test_reader.scan_start_tick;
    tunnel_test_reader.unique_count = 88U;
    tunnel_test_reader.response_pending = 1U;
    TunnelTest_AdvanceTick(30000U - (tunnel_test_tick - start));
    CHECK_U32("manual ISR pauses RF", 1U, tunnel_test_reader.paused);
    AdvanceAndProcess(10U);
    CHECK_U32("manual cannot drain past deadline", 0U, tunnel_test_reader.scan_active);
    CHECK_U32("manual keeps valid EPCs", 88U, tunnel_test_last_quantity);
    tunnel_test_reader.ready = 1U;
    QueueAndPump(TEST_VP_SCAN, 1U);
    AdvanceAndProcess(10U);
    CHECK_U32("new manual ON starts fresh", 1U, tunnel_test_reader.scan_active);
    CHECK_U32("new manual ON resets quantity", 0U, tunnel_test_reader.unique_count);
}

static void TestSinglePassLimits(void) {
    uint32_t starts;
    current_test = "SS3 first edge stops, bounce resumes forward, missing SS3 faults";
    if (!BootToIdle() || !DriveCycleToInventory()) {
        return;
    }
    starts = tunnel_test_motors[CONVEYOR_DRIVER].start_continuous_calls;
    TunnelTest_SetPairedSensor(SENSOR_SS3_PIN, 1U, 1U);
    CHECK_U32("raw stop without main loop", 0U, tunnel_test_motors[CONVEYOR_DRIVER].running);
    CHECK_U32("raw RF pause", 1U, tunnel_test_reader.paused);
    AdvanceAndProcess(10U);
    TunnelTest_SetPairedSensor(SENSOR_SS3_PIN, 0U, 1U);
    AdvanceAndProcess(70U);
    CHECK_U32("bounce resumes same direction", CONVEYOR_DIRECTION,
              tunnel_test_motors[CONVEYOR_DRIVER].direction);
    CHECK_U32("one forward resume", starts + 1U,
              tunnel_test_motors[CONVEYOR_DRIVER].start_continuous_calls);
    if (!FinishInventoryAtSs3(TUNNEL_OPEN_STEP, 4U, 0U)) {
        return;
    }
    starts = tunnel_test_motors[CONVEYOR_DRIVER].start_continuous_calls;
    AdvanceAndProcess(5000U);
    CHECK_U32("no midpoint reverse or later belt restart", starts,
              tunnel_test_motors[CONVEYOR_DRIVER].start_continuous_calls);
    if (!BootToIdle() || !DriveCycleToInventory()) {
        return;
    }
    TunnelTest_AdvanceTick(30000U);
    CHECK_U32("watchdog stops without main loop", 0U, tunnel_test_motors[CONVEYOR_DRIVER].running);
    ProcessNow();
    CHECK_U32("missing SS3 is fault", TUNNEL_FAULT, Tunnel_GetState());
    CHECK_U32("missing SS3 cannot open gate", 0U, tunnel_test_motors[STEP_DRIVER].running);
}

int main(void) {
    TestSinglePassLimits();
    TestStartupAndAutoCycle();
    TestInventoryContinuesWhileConveyorTravels();
    TestSemiConfirmAndRepeatedRescan();
    TestBusyOwnershipAndModeLatch();
    TestManualScanToggle();
    TestGateReopensWhileWaitingForReader();
    TestLiveHmiDetectionRepublishesState();
    TestStaleConfirmBeforePopupIsIgnored();
    TestResultPopupRefreshDoesNotConfirm();
    TestRescanReaderReadyTimeout();
    TestEmergencyAtClosedGateNeedsFreshMode();
    TestResetHomeBounceAndRelease();
    TestEmergencyDuringOpeningRehomes();
    TestHomeNeverExceedsBudgetAndTimesOut();
    TestRepeatedHomePreservesBudgetAndDeadline();
    TestSs1OneSecondDebounce();
    TestManualThirtySecondLimit();

    if (failure_count != 0U) {
        (void)printf("tunnel HMI state machine: %u failure(s)\n", failure_count);
        return 1;
    }

    (void)printf("tunnel HMI state machine: PASS "
                 "(AUTO, SEMI/Confirm, repeated Rescan, manual ownership, "
                 "single pass, SS3 bounce/stop, missing SS3 fault, Emergency latch/all four "
                 "outputs, Reset recovery)\n");
    return 0;
}
