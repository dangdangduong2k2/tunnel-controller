/* Real DWIN parser + PC parser + coordinator + cycle + reader.
 * Simulate only physical UARTs, USB and motors. No serial port is opened. */
#include "test_pc_control.c"
#include "EspPlatform.h"
#include "dual_control.h"
#ifdef SHARED_UART_TEST
#include "SharedUart.h"
#endif
extern uint8_t TunnelTest_IsEmergencyLatched(void);
static uint8_t stop_request, lcd_bytes[512];
static unsigned lcd_count;
static uint32_t lcd_tx_count, fences;
static uint16_t lcd_quantity;

void EspRequestSafetyStop(uint8_t reason) {
    stop_request = 1U;
    MotorPwm_SafetyStop(reason);
}
void EspRequestEmergency(void) {
    stop_request = 1U;
    MotorPwm_EmergencyStop();
}
uint8_t EspTakeEmergencyRequest(void) {
    uint8_t r = stop_request;
    stop_request = 0U;
    return r;
}
uint8_t EspEmergencyLatched(void) {
    return TunnelTest_IsEmergencyLatched();
}
void EspHmiEmergency(void) {
    EspRequestEmergency();
}
uint8_t EspHmiTransmit(const uint8_t *p, uint16_t n) {
    assert(n >= 6U && p[0] == 0x5AU && p[1] == 0xA5U);
    lcd_tx_count++;
    if (n >= 8U && p[3] == 0x82U && (((uint16_t)p[4] << 8U) | p[5]) == HMI_VP_QUANTITY)
        lcd_quantity = ((uint16_t)p[6] << 8U) | p[7];
    return 1U;
}
void EspHmiCapture(void) {
    unsigned i, n = lcd_count;
    lcd_count = 0U;
    for (i = 0U; i < n; i++) {
#ifdef SHARED_UART_TEST
        SharedUart_ReceiveByte(lcd_bytes[i]);
#else
        Hmi_ReceiveByte(lcd_bytes[i]);
#endif
    }
}
void EspPcPoll(void) {
    PcControl_Process();
}
void EspFenceSerialInput(void) {
    lcd_count = 0U;
    fences++;
#ifdef SHARED_UART_TEST
    SharedUart_Fence();
#endif
}
void EspPollSensorEdges(void) {}

static void DualRun(uint32_t ms) {
    while (ms--) {
        TunnelTest_AdvanceTick(1U);
        DualControl_Process();
        ServiceUsb();
        ZK7182_Process();
    }
}
static void QueuePc(uint8_t cmd, const uint8_t *data, uint8_t len) {
    uint8_t p[48];
    uint16_t n;
    sequence++;
    n = MakeRequest(p, cmd, data, len);
    FeedPc(p, n);
}
static uint8_t DualRequest(uint8_t cmd, const uint8_t *data, uint8_t len) {
    uint32_t before = reply_count, timeout = 2300U;
    QueuePc(cmd, data, len);
    while (reply_count == before && timeout--)
        DualRun(1U);
    assert(reply_count == before + 1U && reply[5] == (uint8_t)(cmd | 0x80U));
    return reply[6];
}
static void Lcd(uint16_t vp, uint16_t value) {
    uint8_t p[] = {0x5AU, 0xA5U, 6U, 0x83U, 0U, 0U, 1U, 0U, 0U};
    p[4] = (uint8_t)(vp >> 8U);
    p[5] = (uint8_t)vp;
    p[7] = (uint8_t)(value >> 8U);
    p[8] = (uint8_t)value;
    assert(lcd_count + sizeof(p) <= sizeof(lcd_bytes));
    memcpy(lcd_bytes + lcd_count, p, sizeof(p));
    lcd_count += sizeof(p);
}
static void DualBoot(void) {
    stop_request = 0U;
    lcd_count = 0U;
    fences = 0U;
    lcd_tx_count = 0U;
    lcd_quantity = 0U;
#ifdef SHARED_UART_TEST
    SharedUart_Init();
#endif
    Hmi_Init();
    PcBoot();
    Hmi_Init();
    assert(DualRequest(1U, NULL, 0U) == 0U && reply[7] == 8U);
    assert(PcControl_HasStatusRequest() && !g_hmi_debug.auto_upload_enabled);
}
static void CheckStopped(void) {
    assert(Tunnel_GetState() == TUNNEL_STOPPED && EspEmergencyLatched());
    assert(!tunnel_test_motors[STEP_DRIVER].running);
    assert(!tunnel_test_motors[STEP2_DRIVER].running);
    assert(!tunnel_test_motors[CONVEYOR_DRIVER].running);
}
static void TestCrossControllerStop(void) {
    uint8_t run = 1U;
    uint32_t init_count;
    DualBoot();
    Lcd(HMI_VP_BUTTON_CONVEYOR, 1U);
    DualRun(10U);
    assert(tunnel_test_motors[CONVEYOR_DRIVER].running);
    assert(DualRequest(1U, NULL, 0U) == 0U);
    init_count = tunnel_test_motor_init_calls;
    /* A queued PC Reset must not undo a simultaneous DWIN Emergency. */
    QueuePc(0x18U, NULL, 0U);
    QueuePc(0x11U, &run, 1U);
    Lcd(HMI_VP_BUTTON_EMERGENCY, 1U);
    DualRun(30U);
    CheckStopped();
    assert(tunnel_test_motor_init_calls == init_count && fences == 1U);
    assert(DualRequest(1U, NULL, 0U) == 0U);
    CheckStopped();
    /* A newly issued Reset is allowed, unlike the queued pre-STOP Reset. */
    assert(DualRequest(0x18U, NULL, 0U) == 0U);
    assert(tunnel_test_motor_init_calls == init_count + 1U && !EspEmergencyLatched());

    DualBoot();
    init_count = tunnel_test_motor_init_calls;
    Lcd(HMI_VP_BUTTON_RESET, 1U);
    Lcd(HMI_VP_BUTTON_CONVEYOR, 1U);
    assert(DualRequest(0x17U, NULL, 0U) == 0U);
    DualRun(30U);
    CheckStopped();
    assert(tunnel_test_motor_init_calls == init_count);
    Lcd(HMI_VP_BUTTON_RESET, 1U);
    DualRun(10U);
    assert(tunnel_test_motor_init_calls == init_count + 1U && !EspEmergencyLatched());
}
static void TestReaderWithLcd(void) {
    uint8_t lease = 1U, get = 0x94U, set[] = {0x2FU, 0x9EU, 0x9BU, 0x9CU, 0x9DU};
    uint32_t i;
    DualBoot();
    assert(DualRequest(0x20U, &lease, 1U) == 0U);
    Lcd(HMI_VP_BUTTON_CONVEYOR, 1U);
    DualRun(10U);
    assert(!tunnel_test_motors[CONVEYOR_DRIVER].running);
    assert(DualRequest(0x21U, &get, 1U) == 0U && reply[9] == 30U && reply[12] == 30U);
    assert(DualRequest(0x21U, set, sizeof(set)) == 0U);
    assert(DualRequest(0x21U, &get, 1U) == 0U && reply[10] == 27U && reply[12] == 29U);
    /* Reader traffic cannot suppress a stop from the other UART. */
    config_drop = 1U;
    QueuePc(0x21U, &get, 1U);
    DualRun(2U);
    Lcd(HMI_VP_BUTTON_EMERGENCY, 1U);
    DualRun(20U);
    CheckStopped();
    DualRun(2300U);
    CheckStopped();
    assert(lcd_tx_count != 0U);
    /* A damaged/overflowed PC receive stream fails closed. */
    DualBoot();
    for (i = 0U; i < 300U; i++)
        PcControl_ReceiveByte(0x55U);
    DualRun(20U);
    CheckStopped();
    assert(!MotorPwm_IsReleased() && MotorPwm_GetStopReason() == STOP_RX_OVERFLOW);
}
static void TestSharedCycle(void) {
    uint32_t remaining;
    DualBoot();
    Lcd(HMI_VP_BUTTON_SEMI, 1U);
    DualRun(10U);
    assert(Tunnel_GetMode() == TUNNEL_MODE_SEMI_AUTO);
    TunnelTest_SetPairedSensor(SENSOR_SS1_PIN, 1U, 1U);
    DualRun(1100U);
    assert(Tunnel_GetState() == TUNNEL_CONVEYOR_TO_SS1_HIGH);
    TunnelTest_SetPairedSensor(SENSOR_SS1_PIN, 0U, 1U);
    DualRun(1100U);
    assert(Tunnel_GetState() == TUNNEL_CLOSE_STEP_TO_SS2);
    TunnelTest_SetPairedSensor(SENSOR_SS2_PIN, 1U, 1U);
    DualRun(400U);
    assert(Tunnel_GetState() == TUNNEL_CONVEYOR_INVENTORY_TO_SS3);
    assert(tunnel_test_motors[CONVEYOR_DRIVER].running && rf_on);
    TunnelTest_SetPairedSensor(SENSOR_SS3_PIN, 1U, 1U);
    assert(!tunnel_test_motors[CONVEYOR_DRIVER].running);
    remaining = 6500U;
    while (Tunnel_GetState() != TUNNEL_WAIT_CONFIRM && remaining--)
        DualRun(1U);
    assert(Tunnel_GetState() == TUNNEL_WAIT_CONFIRM);
    DualRun(200U);
    assert(DualRequest(1U, NULL, 0U) == 0U);
    assert(reply[13] == 1U && reply[14] == 0U && lcd_quantity == 1U);
    assert(reply[8] == TUNNEL_WAIT_CONFIRM);    /* LCD and laptop see the same cycle. */
    assert(DualRequest(0x15U, NULL, 0U) == 0U); /* PC confirms a DWIN-started SEMI cycle. */
    assert(Tunnel_GetState() == TUNNEL_OPEN_STEP);
    TunnelTest_SetPairedSensor(SENSOR_SS2_PIN, 0U, 1U);
    TunnelTest_CompleteGatePair();
    DualRun(100U);
    assert(Tunnel_GetState() == TUNNEL_WAIT_REARM);
}
static void TestControlOwnership(void) {
    uint8_t on = 1U, off = 0U;
    DualBoot();
    Lcd(HMI_VP_BUTTON_RESET, 1U);
    assert(DualRequest(0x22U, &on, 1U) == 0U);
    DualRun(5U);
    assert(Hmi_IsControlLocked() && (Tunnel_GetStopFlags() & 8U));
    CheckStopped();
    assert(!MotorPwm_IsReleased());
    Lcd(HMI_VP_BUTTON_RESET, 1U);
    Lcd(HMI_VP_BUTTON_CONVEYOR, 1U);
    DualRun(20U);
    CheckStopped();
    assert(!MotorPwm_IsReleased());
    Lcd(HMI_VP_BUTTON_EMERGENCY, 1U);
    DualRun(2U);
    CheckStopped();
    assert(MotorPwm_IsReleased() && Hmi_IsControlLocked());
    Lcd(HMI_VP_BUTTON_RESET, 1U);
    DualRun(2U);
    CheckStopped();
    assert(MotorPwm_IsReleased());
    assert(DualRequest(0x17U, NULL, 0U) == 0U && MotorPwm_IsReleased());
    assert(Hmi_IsControlLocked());
    assert(DualRequest(0x22U, &off, 1U) == 0U);
    DualRun(10U);
    assert(!Hmi_IsControlLocked());
    CheckStopped();
    Lcd(HMI_VP_BUTTON_RESET, 1U);
    DualRun(10U);
    assert(!EspEmergencyLatched());
    DualBoot();
    assert(DualRequest(0x22U, &on, 1U) == 0U);
    DualRun(3100U);
    CheckStopped();
    assert(!Hmi_IsControlLocked() && !MotorPwm_IsReleased());
    Lcd(HMI_VP_BUTTON_CONVEYOR, 1U);
    DualRun(10U);
    CheckStopped();
    Lcd(HMI_VP_BUTTON_RESET, 1U);
    DualRun(10U);
    assert(!EspEmergencyLatched());
    DualBoot();
    assert(DualRequest(0x22U, &on, 1U) == 0U);
    assert(DualRequest(0x18U, NULL, 0U) == 0U);
    TunnelTest_SetPairedSensor(SENSOR_SS2_PIN, 1U, 1U);
    DualRun(100U);
    assert((Tunnel_GetStopFlags() & 3U) == 2U); /* Homed, not latched. */
    assert(DualRequest(0x13U, &off, 1U) == 0U);
    TunnelTest_SetPairedSensor(SENSOR_SS2_PIN, 0U, 1U);
    TunnelTest_CompleteGatePair();
    DualRun(1100U);
    TunnelTest_SetPairedSensor(SENSOR_SS1_PIN, 1U, 1U);
    DualRun(1100U);
    assert(MotorPwm_IsRunning(CONVEYOR_DRIVER));
    assert(DualRequest(1U, NULL, 0U) == 0U);
    DualRun(3100U);
    CheckStopped();
    assert(!MotorPwm_IsReleased() && MotorPwm_GetStopReason() == STOP_PC_TIMEOUT);
    assert(!Hmi_IsControlLocked());
    DualBoot();
    Lcd(HMI_VP_BUTTON_RESET, 1U);
    Hmi_ReceiveError();
    DualRun(10U);
    CheckStopped();
    assert(!MotorPwm_IsReleased() && MotorPwm_GetStopReason() == STOP_UART);
    puts("PASS PC/DWIN ownership: LCD motion/reset locked, LCD Emergency releases during PC "
         "session, disconnect/timeout unlock with Reset required; real sensor AUTO timeout holds; "
         "UART error holds");
}
#ifdef SHARED_UART_TEST
int DualRegressionMain(void)
#else
int main(void)
#endif
{
    TestControlOwnership();
    TestCrossControllerStop();
    TestReaderWithLcd();
    TestSharedCycle();
    puts("PASS dual control: real PC/DWIN/Tunnel/ZK, shared SEMI cycle/EPC/Confirm, PC without "
         "LCD, LCD motion, both cross-port STOP/Reset races, fresh Reset, reader power readback, "
         "maintenance, USB timeout, RX overflow");
    return 0;
}
