#include "hmi.h"
#include "debug_state.h"
#include "EspPlatform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* HMI v2 UI events (VP 0x1090) on the real hmi.c stream parser. */
volatile DebugStateTypeDef g_debug;
static uint32_t tick, stops, faults;
static uint8_t tx[140];
static unsigned txSize;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
uint32_t HAL_GetTick(void) {
    return tick;
}
void EspHmiEmergency(void) {
    ++stops;
}
void EspRequestSafetyStop(uint8_t reason) {
    CHECK(reason == STOP_UART);
    ++faults;
}
uint8_t EspHmiTransmit(const uint8_t *p, uint16_t n) {
    CHECK(n <= sizeof(tx));
    memcpy(tx, p, n);
    txSize = n;
    return 1;
}
void EspHmiCapture(void) {}
static uint16_t crc16(const uint8_t *p, unsigned n) {
    uint16_t crc = 0xffff;
    while (n--) {
        crc ^= *p++;
        for (unsigned b = 0; b < 8; ++b)
            crc = (crc & 1) ? (crc >> 1) ^ 0xa001 : crc >> 1;
    }
    return crc;
}
static void send(uint16_t vp, uint16_t value, int crc) {
    uint8_t p[11] = {0x5a, 0xa5, 6, 0x83, 0, 0, 1, 0, 0};
    unsigned n = 9;
    p[4] = (uint8_t)(vp >> 8);
    p[5] = (uint8_t)vp;
    p[7] = (uint8_t)(value >> 8);
    p[8] = (uint8_t)value;
    if (crc) {
        uint16_t c = crc16(p + 3, 6);
        p[2] = 8;
        p[9] = (uint8_t)c;
        p[10] = (uint8_t)(c >> 8);
        n = 11;
    }
    for (unsigned i = 0; i < n; ++i)
        Hmi_ReceiveByte(p[i]);
}
static void ui(uint16_t expected) {
    uint16_t value = 0;
    CHECK(Hmi_PopUiEvent(&value) && value == expected);
}
static void noUi(void) {
    uint16_t value;
    CHECK(!Hmi_PopUiEvent(&value));
}
static void machine(uint16_t vp, uint16_t value) {
    HmiEvent e;
    CHECK(Hmi_PopEvent(&e) && e.vp == vp && e.value == value);
}
static void noMachine(void) {
    HmiEvent e;
    CHECK(!Hmi_PopEvent(&e));
}
int main(void) {
    Hmi_Init();
    CHECK(!Hmi_PopUiEvent(0));
    noUi();
    /* Accepted, in order, and never seen by the machine queue. */
    send(HMI_VP_UI_EVENT, 0x0103, 0);
    send(HMI_VP_UI_EVENT, 0x0206, 0);
    CHECK(g_hmi_debug.ui_event_count == 2 && g_hmi_debug.ui_queue_depth == 2);
    CHECK(g_hmi_debug.rx_event_count == 0 && g_hmi_debug.event_queue_depth == 0);
    noMachine();
    ui(0x0103);
    ui(0x0206);
    noUi();
    /* Typing "11": identical codes are separate key presses, with and without CRC. */
    send(HMI_VP_UI_EVENT, 0x0431, 0);
    send(HMI_VP_UI_EVENT, 0x0431, 0);
    send(HMI_VP_UI_EVENT, 0x0431, 1);
    ui(0x0431);
    ui(0x0431);
    ui(0x0431);
    noUi();
    /* A key value (possibly a password character) is not copied to diagnostics. */
    send(HMI_VP_BUTTON_AUTO, 1, 0);
    send(HMI_VP_UI_EVENT, 0x0470, 0);
    CHECK(g_hmi_debug.last_vp == HMI_VP_BUTTON_AUTO && g_hmi_debug.last_value == 1);
    machine(HMI_VP_BUTTON_AUTO, 1);
    ui(0x0470);
    /* Machine buttons interleaved with UI codes keep their own order and queue. */
    send(HMI_VP_UI_EVENT, 0x0701, 0);
    send(HMI_VP_BUTTON_CONVEYOR, 1, 0);
    send(HMI_VP_UI_EVENT, 0x0702, 0);
    send(HMI_VP_BUTTON_CONVEYOR, 0, 0);
    machine(HMI_VP_BUTTON_CONVEYOR, 1);
    machine(HMI_VP_BUTTON_CONVEYOR, 0);
    noMachine();
    ui(0x0701);
    ui(0x0702);
    /* A machine state transition (flush fence) drops stale machine touches only. */
    send(HMI_VP_BUTTON_AUTO, 1, 0);
    send(HMI_VP_UI_EVENT, 0x010C, 0);
    Hmi_FlushEvents();
    noMachine();
    ui(0x010C);
    /* Remote ownership locks machine buttons; viewing/navigation still arrives. */
    Hmi_SetControlLocked(1);
    send(HMI_VP_BUTTON_RESET, 1, 0);
    send(HMI_VP_UI_EVENT, 0x0104, 0);
    noMachine();
    ui(0x0104);
    Hmi_SetControlLocked(0);
    /* Emergency priority is unchanged: it cuts at once, is delivered first, and UI
     * touches from before or during the stop are not executed afterwards. */
    for (int crc = 0; crc <= 1; crc++) {
        uint32_t before = stops;
        send(HMI_VP_UI_EVENT, 0x0702, crc);
        send(HMI_VP_BUTTON_AUTO, 1, crc);
        send(HMI_VP_BUTTON_EMERGENCY, 1, crc);
        CHECK(stops == before + 1 && g_hmi_debug.emergency_pending == 1);
        send(HMI_VP_UI_EVENT, 0x0702, crc);
        send(HMI_VP_BUTTON_RESET, 1, crc);
        noUi();
        machine(HMI_VP_BUTTON_EMERGENCY, 1);
        noMachine();
        noUi();
    }
    /* Reset priority is unchanged and does not disturb UI codes. */
    send(HMI_VP_UI_EVENT, 0x0501, 0);
    send(HMI_VP_BUTTON_AUTO, 1, 0);
    send(HMI_VP_BUTTON_RESET, 1, 0);
    send(HMI_VP_BUTTON_AUTO, 1, 0);
    machine(HMI_VP_BUTTON_RESET, 1);
    noMachine();
    ui(0x0501);
    /* The STOP fence and a UART error empty the UI queue too. */
    send(HMI_VP_UI_EVENT, 0x0702, 0);
    Hmi_DiscardActionsAfterStop();
    noUi();
    send(HMI_VP_UI_EVENT, 0x0702, 0);
    Hmi_ReceiveError();
    CHECK(faults == 1);
    noUi();
    /* Corrupt, timed-out and unknown frames never become UI events. */
    {
        uint8_t bad[11] = {0x5a, 0xa5, 8, 0x83, 0x10, 0x90, 1, 0x07, 0x02, 0, 0};
        uint8_t good[9] = {0x5a, 0xa5, 6, 0x83, 0x10, 0x90, 1, 0x07, 0x02};
        uint16_t c = crc16(bad + 3, 6);
        bad[9] = (uint8_t)c;
        bad[10] = (uint8_t)((c >> 8) ^ 1);
        for (unsigned i = 0; i < 11; ++i)
            Hmi_ReceiveByte(bad[i]);
        noUi();
        for (unsigned i = 0; i < 5; ++i)
            Hmi_ReceiveByte(good[i]);
        tick += 51;
        for (unsigned i = 5; i < 9; ++i)
            Hmi_ReceiveByte(good[i]);
        noUi();
    }
    send(0x1091, 0x0702, 0);
    send(0x2FF0, 0x0702, 0);
    noUi();
    noMachine();
    /* Overflow keeps the oldest 32 in order and counts the rest. */
    for (uint16_t i = 0; i < 40; ++i)
        send(HMI_VP_UI_EVENT, (uint16_t)(0x0430 + (i % 10)), 0);
    CHECK(g_hmi_debug.ui_queue_depth == 32 && g_hmi_debug.ui_queue_overflow_count == 8);
    for (uint16_t i = 0; i < 32; ++i)
        ui((uint16_t)(0x0430 + (i % 10)));
    noUi();
    send(HMI_VP_UI_EVENT, 0x0503, 0);
    ui(0x0503);
    /* Page writes are counted so the UI can follow pages forced by the machine. */
    {
        uint32_t count = 99;
        Hmi_Init();
        CHECK(Hmi_GetLastPage(&count) == 0 && count == 0);
        CHECK(Hmi_SendPage(2) && txSize == 10 && tx[5] == 0x84 && tx[9] == 2);
        CHECK(Hmi_GetLastPage(&count) == 2 && count == 1);
        CHECK(Hmi_SendPage(13) && Hmi_GetLastPage(0) == 13);
        CHECK(Hmi_GetLastPage(&count) == 13 && count == 2);
    }
    printf("HMI UI event queue passed: 0x1090 accepted, repeats delivered, separate from machine "
           "queue, Emergency/Reset priority unchanged, fences, overflow, page tracking\n");
    return 0;
}
