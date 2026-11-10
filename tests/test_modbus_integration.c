/* Real Modbus core + real PC/DWIN coordinator + Tunnel + reader.
 * Only transport IO, clock, GPIO, motors and USB are simulated. */
#include "test_dual_control.c"
void MbTest_Init(void);
void MbTest_Queue(uint8_t peer, const uint8_t *p, uint8_t n);
uint16_t MbTest_Reg(uint16_t a);
uint8_t MbTest_Exception(void);
void MbTest_ConfigBusy(uint8_t value);
static void MbSingle(uint8_t peer, uint16_t addr, uint16_t value) {
    uint8_t p[] = {6, (uint8_t)(addr >> 8), (uint8_t)addr, (uint8_t)(value >> 8), (uint8_t)value};
    MbTest_Queue(peer, p, sizeof(p));
}
static void MbCommand(uint16_t seq, uint16_t op, uint16_t a, uint16_t b, uint16_t c, uint16_t d,
                      uint16_t e) {
    uint16_t v[] = {seq, op, a, b, c, d, e, 0};
    uint8_t p[22] = {16, 1, 0x10, 0, 8, 16};
    unsigned i;
    for (i = 0; i < 8; i++) {
        p[6 + i * 2] = (uint8_t)(v[i] >> 8);
        p[7 + i * 2] = (uint8_t)v[i];
    }
    MbTest_Queue(1, p, sizeof(p));
}
static void MbBoot(void) {
    stop_request = 0;
    lcd_count = 0;
    MbTest_Init();
    SharedUart_Init();
    Hmi_Init();
    PcBoot();
    Hmi_Init();
    PcControl_Init();
    MbTest_Init();
    assert(!PcControl_HasStatusRequest() && !g_hmi_debug.rx_event_count);
    MbSingle(1, 0x100, 1);
    DualRun(3);
    assert(MbTest_Reg(16) == 1 && Hmi_IsControlLocked());
    CheckStopped();
}
static void TestModbusOnly(void) {
    uint32_t count;
    MbBoot();
    MbCommand(1, 1, 0, 0, 0, 0, 0);
    DualRun(DIRECTION_REVERSAL_DELAY_MS + 10);
    assert(tunnel_test_motors[STEP_DRIVER].running || tunnel_test_motors[STEP2_DRIVER].running);
    TunnelTest_SetSensor(SENSOR_SS2_PIN, 1, 1);
    TunnelTest_SetSensor(SENSOR_SS7_PIN, 1, 1);
    DualRun(100);
    assert(!tunnel_test_motors[STEP_DRIVER].running && !tunnel_test_motors[STEP2_DRIVER].running);
    assert(!PcControl_HasStatusRequest()); // no DWIN/legacy monitoring needed to run Tunnel_Process
    count = tunnel_test_motor_init_calls;
    MbCommand(1, 1, 0, 0, 0, 0, 0);
    DualRun(5);
    assert(tunnel_test_motor_init_calls == count);
    MbSingle(2, 0x100, 1);
    DualRun(5);
    assert(MbTest_Exception() == 6);
    assert(DualRequest(0x18, NULL, 0) == 2); // legacy Reset cannot seize the machine
    MbSingle(2, 0x101, 1);
    MbCommand(2, 1, 0, 0, 0, 0, 0);
    DualRun(5);
    CheckStopped();
    assert(MotorPwm_IsReleased());
    assert(MbTest_Reg(18) == 1); // queued Reset fenced
    MbCommand(2, 1, 0, 0, 0, 0, 0);
    DualRun(10);
    assert(MbTest_Reg(19) == 0);
    // Foreign monitoring must not extend the Modbus owner's watchdog.
    DualRun(2900);
    assert(DualRequest(1, NULL, 0) == 0);
    DualRun(120);
    CheckStopped();
    assert(MbTest_Reg(16) == 0 && !Hmi_IsControlLocked());
    MbCommand(3, 1, 0, 0, 0, 0, 0);
    DualRun(5);
    assert(MbTest_Exception() == 6);
    CheckStopped();
    puts("PASS Modbus integration: no-DWIN operation, dual-door homing, legacy exclusion, STOP "
         "fences stale Reset, timeout cannot be masked by foreign polling");
}
static void TestModbusReader(void) {
    uint32_t count;
    MbBoot();
    MbCommand(1, 9, 1, 0, 0, 0, 0);
    DualRun(5);
    assert(Tunnel_IsPcMaintenance());
    MbCommand(2, 22, 30, 27, 28, 29, 1);
    DualRun(30);
    assert(MbTest_Reg(20) == 2 && MbTest_Reg(22) == 0 && powers[0] == 30 && powers[1] == 27);
    assert(last_usb[2] == 0x2F && last_usb[3] == 0x9E && last_usb[4] == 0x9B);
    count = config_count;
    MbCommand(2, 22, 30, 27, 28, 29, 1);
    DualRun(10);
    assert(config_count == count);
    MbCommand(3, 21, 0, 0, 0, 0, 0);
    DualRun(30);
    assert(MbTest_Reg(34) == 30 && MbTest_Reg(35) == 27 && MbTest_Reg(36) == 28 &&
           MbTest_Reg(37) == 29);
    MbCommand(4, 23, 5, 1, 0, 0, 0);
    DualRun(30);
    assert(antenna_mask == 5);
    MbCommand(5, 27, 4123, 0, 0, 0, 0);
    DualRun(30);
    assert(rf_profile == 4123);
    MbCommand(6, 26, 0, 0, 0, 0, 0);
    DualRun(30);
    assert(MbTest_Reg(34) == 0x10 && MbTest_Reg(35) == 0x1B);
    // Existing reader allowlist rejects unsupported profiles; no raw arbitrary bridge.
    count = config_count;
    MbCommand(7, 27, 65535, 0, 0, 0, 0);
    DualRun(5);
    assert(MbTest_Reg(19) == 2 && config_count == count);
    config_error = 0xF8;
    MbCommand(8, 21, 0, 0, 0, 0, 0);
    DualRun(30);
    assert(MbTest_Reg(20) == 2 && MbTest_Reg(33) == 0xF8);
    config_error = 0;
    config_drop = 1;
    MbCommand(9, 21, 0, 0, 0, 0, 0);
    DualRun(2200);
    assert(MbTest_Reg(20) == 3 && MbTest_Reg(22) == 6);
    config_drop = 0;
    puts("PASS Modbus real reader bridge: maintenance, four power bytes, repeat suppression, "
         "readback, antenna mask, Gen2X profile, allowlist, reader error and timeout");
}
static void TestLegacyWithModbus(void) {
    uint8_t one = 1, zero = 0;
    stop_request = 0;
    MbTest_Init();
    DualBoot();
    assert(DualRequest(0x22, &one, 1) == 0 && MbTest_Reg(16) == 5);
    MbSingle(1, 0x100, 1);
    DualRun(5);
    assert(MbTest_Exception() == 6);
    DualRun(2000);
    assert(DualRequest(1, NULL, 0) == 0);
    DualRun(2000);
    assert(MbTest_Reg(16) == 5); // legacy owner's own poll keeps its session alive
    assert(DualRequest(0x22, &zero, 1) == 0 && MbTest_Reg(16) == 0);
    MbSingle(1, 0x100, 1);
    DualRun(5);
    assert(MbTest_Reg(16) == 1);
    puts("PASS legacy coexistence with production Modbus hooks: claim exclusion, own heartbeat, "
         "immediate deliberate release");
}
static void TestConfigMaintenance(void) {
    uint32_t count;
    MbBoot();
    MbSingle(1, 256, 0);
    DualRun(5);
    assert(MbTest_Reg(16) == 0);
    MbTest_ConfigBusy(1);
    count = tunnel_test_motor_init_calls;
    Lcd(HMI_VP_BUTTON_RESET, 1);
    QueuePc(0x18, NULL, 0);
    MbSingle(1, 256, 1);
    DualRun(20);
    CheckStopped();
    assert(tunnel_test_motor_init_calls == count);
    // Same fence used on failed-save exit discards every queued interface action.
    EspRequestSafetyStop(STOP_PC_DISCONNECT);
    DualRun(2);
    MbTest_ConfigBusy(0);
    DualRun(20);
    CheckStopped();
    assert(tunnel_test_motor_init_calls == count && MbTest_Reg(16) == 0);
    puts("PASS configuration maintenance: DWIN/legacy/Modbus Reset blocked during save, exit fence "
         "prevents stale motion");
}
static void TestDwinStopWithModbusOwner(void) {
    MbBoot();
    MbCommand(1, 1, 0, 0, 0, 0, 0);
    DualRun(DIRECTION_REVERSAL_DELAY_MS + 10);
    assert(tunnel_test_motors[STEP_DRIVER].running || tunnel_test_motors[STEP2_DRIVER].running);
    // An already queued remote Reset must not undo the local Emergency.
    MbCommand(2, 1, 0, 0, 0, 0, 0);
    Lcd(HMI_VP_BUTTON_EMERGENCY, 1);
    DualRun(5);
    CheckStopped();
    assert(MotorPwm_IsReleased());
    Lcd(HMI_VP_BUTTON_RESET, 1);
    Lcd(HMI_VP_BUTTON_CONVEYOR, 1);
    DualRun(5);
    CheckStopped();
    assert(MotorPwm_IsReleased());
    puts("PASS DWIN Emergency while Modbus owns moving axes; queued remote Reset fenced; local "
         "motion/reset remain locked");
}
static void TestSessionBusy(void) {
    uint32_t count;
    MbBoot();
    /* Owner remains in maintenance: motion must be rejected, never queued. */
    MbCommand(1, 9, 1, 0, 0, 0, 0);
    DualRun(5);
    assert(Tunnel_IsPcMaintenance());
    count = tunnel_test_motors[CONVEYOR_DRIVER].start_continuous_calls;
    MbCommand(2, 3, 1, 0, 0, 0, 0);
    DualRun(5);
    assert(MbTest_Exception() == 0 && MbTest_Reg(19) == 2);
    assert(MbTest_Reg(16) == 1 && Tunnel_IsPcMaintenance());
    assert(tunnel_test_motors[CONVEYOR_DRIVER].start_continuous_calls == count);
    /* Reader is in flight: reject another transaction, keep old sequence/data. */
    config_drop = 1;
    MbCommand(3, 21, 0, 0, 0, 0, 0);
    DualRun(5);
    assert(MbTest_Reg(20) == 1);
    count = config_count;
    MbCommand(4, 22, 20, 20, 20, 20, 1);
    DualRun(5);
    assert(MbTest_Exception() == 6 && config_count == count);
    assert(MbTest_Reg(21) == 3 && MbTest_Reg(18) == 3);
    MbSingle(1, 256, 1); /* Heartbeat still works during BUSY. */
    DualRun(5);
    assert(MbTest_Exception() == 0 && MbTest_Reg(16) == 1);
    MbSingle(2, 258, 1); /* STOP remains available from another interface. */
    DualRun(5);
    CheckStopped();
    assert(MbTest_Reg(16) == 0 && config_count == count);
    config_drop = 0;
    puts("PASS session BUSY: motion blocked in maintenance, pending Set not queued, "
         "owner/reply preserved, status/heartbeat/STOP available");
}
static void TestStandaloneRf(void) {
    unsigned i;
    uint32_t sessions;
    MbBoot(); /* Claim latches STOP, without homing. */
    assert(!(Tunnel_GetStopFlags() & 2U));
    MbCommand(1, 4, 1, 0, 0, 0, 0);
    DualRun(500);
    assert(MbTest_Reg(19) == 0 && ZK7182_IsScanActive() && rf_on);
    assert(ZK7182_GetUniqueEpcCount() > 0 && !(Tunnel_GetStopFlags() & 2U));
    for (i = 0; i < MOTOR_PWM_DRIVER_COUNT; ++i) {
        assert(!MotorPwm_IsRunning((MotorPwmId)i));
        assert(tunnel_test_motors[i].start_continuous_calls == 0);
        assert(tunnel_test_motors[i].start_profile_calls == 0);
    }
    sessions = g_debug.scan_session_count;
    MbCommand(2, 4, 1, 0, 0, 0, 0);
    DualRun(10);
    assert(g_debug.scan_session_count == sessions); /* ON while ON is idempotent. */
    MbCommand(3, 1, 0, 0, 0, 0, 0); /* Reset cannot start homing during RF. */
    DualRun(10);
    assert(MbTest_Reg(19) == 2 && ZK7182_IsScanActive());
    MbCommand(4, 3, 1, 0, 0, 0, 0);
    DualRun(10);
    assert(MbTest_Reg(19) == 2 && !MotorPwm_IsRunning(CONVEYOR_DRIVER));
    MbCommand(5, 9, 1, 0, 0, 0, 0);
    DualRun(10);
    assert(MbTest_Reg(19) == 2 && !Tunnel_IsPcMaintenance());
    MbCommand(6, 4, 0, 0, 0, 0, 0);
    DualRun(1200);
    assert(!ZK7182_IsScanActive() && !rf_on && Tunnel_GetState() == TUNNEL_STOPPED);
    assert(ZK7182_GetUniqueEpcCount() > 0); /* Keep result after STOP. */
    MbCommand(7, 4, 1, 0, 0, 0, 0);
    DualRun(300);
    assert(ZK7182_IsScanActive());
    /* Sensor edges and the RF deadline must not arm conveyor AUTO. */
    TunnelTest_SetSensor(SENSOR_SS1_PIN, 1U, 1U);
    for (i = 0; i < 64; ++i) {
        MbSingle(1, 256, 1);
        DualRun(500);
        assert(!MotorPwm_IsRunning(CONVEYOR_DRIVER));
        assert(!MotorPwm_IsRunning(STEP_DRIVER) && !MotorPwm_IsRunning(STEP2_DRIVER));
    }
    assert(!ZK7182_IsScanActive() && Tunnel_GetState() == TUNNEL_STOPPED);
    MbCommand(8, 4, 1, 0, 0, 0, 0);
    DualRun(300);
    assert(ZK7182_IsScanActive());
    DualRun(3100); /* Missing heartbeat stops RF, never resumes it. */
    assert(!ZK7182_IsScanActive() && MbTest_Reg(16) == 0);
    DualRun(1200);
    MbSingle(1, 256, 1);
    DualRun(10);
    MbCommand(9, 4, 1, 0, 0, 0, 0);
    DualRun(300);
    assert(ZK7182_IsScanActive());
    MbSingle(2, 257, 1);
    DualRun(1200);
    assert(!ZK7182_IsScanActive());
    MbSingle(1, 256, 0);
    DualRun(10);
    MbSingle(1, 256, 1);
    DualRun(10);
    MbCommand(10, 4, 1, 0, 0, 0, 0);
    DualRun(10);
    assert(MbTest_Reg(19) == 2 && !ZK7182_IsScanActive()); /* Claim cannot clear Emergency. */
    puts("PASS standalone RF: no home/motor, EPC, BUSY motion/settings, stop/deadline, "
         "no AUTO after scan, lease and persistent Emergency protection");
}
int main(void) {
    TestStandaloneRf();
    TestSessionBusy();
    TestModbusOnly();
    TestModbusReader();
    TestLegacyWithModbus();
    TestConfigMaintenance();
    TestDwinStopWithModbusOwner();
    return 0;
}
