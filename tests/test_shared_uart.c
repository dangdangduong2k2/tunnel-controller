#include "test_dual_control.c"

static void WireQueue(uint8_t cmd, const uint8_t *data, uint8_t len) {
    uint8_t p[48], encoded[102];
    uint16_t n, i;
    sequence++;
    n = MakeRequest(p, cmd, data, len);
    n = SharedUart_Encode(p, n, encoded, sizeof(encoded));
    assert(n != 0U);
    for (i = 0U; i < n; i++)
        assert(encoded[i] != 0x5AU && encoded[i] != 0xA5U);
    assert(lcd_count + n <= sizeof(lcd_bytes));
    memcpy(lcd_bytes + lcd_count, encoded, n);
    lcd_count += n;
}
static void TestSharedPackets(void) {
    uint32_t init_count, events, before;
    uint16_t stop_seq;
    uint8_t run = 1U;
    DualBoot();
    events = g_hmi_debug.rx_event_count;
    before = reply_count;
    WireQueue(1U, NULL, 0U);
    /* Fragmented ASCII packet waits for newline and is not an LCD touch. */
    {
        uint8_t tail = lcd_bytes[lcd_count - 1U];
        lcd_count--;
        DualRun(5U);
        assert(reply_count == before);
        lcd_bytes[lcd_count++] = tail;
        DualRun(5U);
    }
    assert(reply_count == before + 1U && g_hmi_debug.rx_event_count == events);
    Lcd(HMI_VP_BUTTON_CONVEYOR, 1U);
    DualRun(5U);
    assert(tunnel_test_motors[CONVEYOR_DRIVER].running);
    init_count = tunnel_test_motor_init_calls;
    WireQueue(0x18U, NULL, 0U);
    WireQueue(0x11U, &run, 1U);
    WireQueue(0x17U, NULL, 0U);
    stop_seq = sequence;
    Lcd(HMI_VP_BUTTON_RESET, 1U);
    DualRun(20U);
    CheckStopped();
    assert(tunnel_test_motor_init_calls == init_count);
    assert(reply[5] == 0x97U && reply[3] == (uint8_t)stop_seq &&
           reply[4] == (uint8_t)(stop_seq >> 8U));
    WireQueue(0x18U, NULL, 0U);
    DualRun(10U);
    assert(!EspEmergencyLatched() && tunnel_test_motor_init_calls == init_count + 1U);
    DualBoot();
    init_count = tunnel_test_motor_init_calls;
    WireQueue(0x18U, NULL, 0U);
    Lcd(HMI_VP_BUTTON_EMERGENCY, 1U);
    DualRun(20U);
    CheckStopped();
    assert(tunnel_test_motor_init_calls == init_count);

    /* CRC-damaged PC envelope stops safely and never executes its command. */
    DualBoot();
    before = reply_count;
    WireQueue(0x11U, &run, 1U);
    lcd_bytes[lcd_count - 2U] = lcd_bytes[lcd_count - 2U] == '0' ? '1' : '0';
    DualRun(10U);
    CheckStopped();
    assert(reply_count == before);
    assert(!MotorPwm_IsReleased() && MotorPwm_GetStopReason() == STOP_BAD_PACKET);
    /* An incomplete PC envelope cannot swallow a subsequent DWIN Emergency. */
    DualBoot();
    memcpy(lcd_bytes, "@TUN:A55A", 9U);
    lcd_count = 9U;
    Lcd(HMI_VP_BUTTON_EMERGENCY, 1U);
    DualRun(10U);
    CheckStopped();
}
static void TestSharedReader(void) {
    uint8_t lease = 1U, get = 0x94U;
    uint32_t before;
    DualBoot();
    WireQueue(0x20U, &lease, 1U);
    DualRun(5U);
    before = reply_count;
    WireQueue(0x21U, &get, 1U);
    Lcd(HMI_VP_BUTTON_CONVEYOR, 1U);
    DualRun(50U);
    assert(reply_count == before + 1U && reply[5] == 0xA1U && reply[9] == 30U && reply[12] == 30U);
    assert(!tunnel_test_motors[CONVEYOR_DRIVER].running);
}
static void TestIndependentDoorStatus(void) {
    DualBoot();
    Lcd(HMI_VP_BUTTON_GATE, 1U);
    DualRun(100U);
    assert(MotorPwm_IsRunning(STEP_DRIVER) && MotorPwm_IsRunning(STEP2_DRIVER));
    TunnelTest_SetSensor(SENSOR_SS2_PIN, 1U, 1U);
    DualRun(80U);
    WireQueue(1U, NULL, 0U);
    DualRun(10U);
    assert(reply[7] == 8U && (reply[11] & 1U) && (reply[11] & 2U));
    assert((reply[12] & 4U) && !(reply[12] & 1U) && !(reply[12] & 2U));
    assert(!MotorPwm_IsRunning(STEP_DRIVER) && MotorPwm_IsRunning(STEP2_DRIVER));
    TunnelTest_SetSensor(SENSOR_SS7_PIN, 1U, 1U);
    DualRun(80U);
    WireQueue(1U, NULL, 0U);
    DualRun(10U);
    assert((reply[11] & 1U) && (reply[12] & 1U) && !(reply[11] & 2U));
    Lcd(HMI_VP_BUTTON_EMERGENCY, 1U);
    DualRun(10U);
    CheckStopped();
}
int main(void) {
    DualRegressionMain();
    TestSharedPackets();
    TestSharedReader();
    TestIndependentDoorStatus();
    puts("PASS shared UART: native DWIN + ASCII PC, fragmentation, no raw DWIN header in PC TX, "
         "STOP preempts queued Reset with ACK, CRC damage stops, partial PC cannot hide LCD STOP, "
         "reader bridge");
    return 0;
}
