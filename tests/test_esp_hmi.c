#include "hmi.h"
#include "debug_state.h"
#include "EspPlatform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
volatile DebugStateTypeDef g_debug;
static uint32_t tick, stops, faults;
static uint8_t buffered[128], tx[140];
static uint8_t txReady = 1;
static unsigned bufferedSize, txSize;
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
    if (!txReady)
        return 0;
    CHECK(n <= sizeof(tx));
    memcpy(tx, p, n);
    txSize = n;
    return 1;
}
void EspHmiCapture(void) {
    unsigned n = bufferedSize;
    bufferedSize = 0;
    for (unsigned i = 0; i < n; ++i)
        Hmi_ReceiveByte(buffered[i]);
}
static uint16_t crc16(const uint8_t *p, unsigned n) {
    uint16_t crc = 0xffff;
    while (n--) {
        crc ^= *p++;
        for (unsigned b = 0; b < 8; ++b)
            crc = (crc & 1) ? (crc >> 1) ^ 0xa001 : crc >> 1;
    }
    return crc;
}
static unsigned frame(uint8_t *p, uint16_t vp, int crc) {
    const uint8_t b[] = {0x5a, 0xa5, 6, 0x83, 0, 0, 1, 0, 1};
    memcpy(p, b, 9);
    p[4] = vp >> 8;
    p[5] = vp;
    if (crc) {
        p[2] = 8;
        uint16_t c = crc16(p + 3, 6);
        p[9] = c;
        p[10] = c >> 8;
        return 11;
    }
    return 9;
}
static void inject(const uint8_t *p, unsigned n) {
    while (n--)
        Hmi_ReceiveByte(*p++);
}
static HmiEvent pop(uint16_t vp) {
    HmiEvent e;
    CHECK(Hmi_PopEvent(&e));
    CHECK(e.vp == vp);
    return e;
}
static void empty(void) {
    HmiEvent e;
    CHECK(!Hmi_PopEvent(&e));
}
int main(void) {
    uint8_t f[16];
    unsigned n;
    Hmi_Init();
    Hmi_Process();
    CHECK(txSize == 7 && tx[3] == 0x83 && tx[5] == 0x80);
    const uint8_t cfg[] = {0x5a, 0xa5, 8, 0x83, 0, 0x80, 2, 0, 0x14, 0x10, 0x38};
    inject(cfg, sizeof(cfg));
    CHECK(g_hmi_debug.config_known && g_hmi_debug.auto_upload_enabled);
    n = frame(f, HMI_VP_BUTTON_AUTO, 0);
    inject(f, n);
    pop(HMI_VP_BUTTON_AUTO);
    empty();
    n = frame(f, HMI_VP_BUTTON_AUTO, 0);
    inject(f, 5);
    Hmi_FlushEvents();
    inject(f + 5, n - 5);
    empty();
    // Bytes sitting in the ESP32 UART before a state transition are stale too.
    bufferedSize = frame(buffered, HMI_VP_BUTTON_AUTO, 0);
    Hmi_FlushEvents();
    empty();
    n = frame(f, HMI_VP_BUTTON_EMERGENCY, 0);
    inject(f, 5);
    Hmi_FlushEvents();
    inject(f + 5, n - 5);
    CHECK(stops == 1);
    pop(HMI_VP_BUTTON_EMERGENCY);
    empty();
    // Valid emergency cuts immediately, before the event consumer executes.
    n = frame(f, HMI_VP_BUTTON_AUTO, 0);
    for (unsigned i = 0; i < 40; ++i)
        inject(f, n);
    n = frame(f, HMI_VP_BUTTON_EMERGENCY, 1);
    inject(f, n);
    CHECK(stops == 2);
    n = frame(f, HMI_VP_BUTTON_RESET, 0);
    inject(f, n);
    Hmi_FlushEvents();
    pop(HMI_VP_BUTTON_EMERGENCY);
    empty();
    n = frame(f, HMI_VP_BUTTON_EMERGENCY, 1);
    f[n - 1] ^= 1;
    inject(f, n);
    CHECK(stops == 2);
    empty();
    // A fresh reset after STOP is retained; an earlier reset at RX failure isn't.
    n = frame(f, HMI_VP_BUTTON_RESET, 0);
    inject(f, n);
    Hmi_FlushEvents();
    pop(HMI_VP_BUTTON_RESET);
    inject(f, n);
    Hmi_ReceiveError();
    CHECK(stops == 2 && faults == 1);
    empty();
    // Timed-out fragments cannot become a fabricated touch.
    n = frame(f, HMI_VP_BUTTON_AUTO, 0);
    inject(f, 4);
    tick += 51;
    inject(f + 4, n - 4);
    empty();
    inject(f, n);
    pop(HMI_VP_BUTTON_AUTO);
    CHECK(Hmi_SendQuantity(123));
    CHECK(tx[3] == 0x82 && tx[4] == 0x20 && tx[6] == 0 && tx[7] == 123);
    // Remote ownership must block motion/reset, but never a valid DWIN stop.
    for (int crc = 0; crc <= 1; crc++) {
        uint32_t before = stops;
        Hmi_Init();
        Hmi_SetControlLocked(1);
        n = frame(f, HMI_VP_BUTTON_RESET, crc);
        inject(f, n);
        empty();
        n = frame(f, HMI_VP_BUTTON_CONVEYOR, crc);
        inject(f, n);
        empty();
        n = frame(f, HMI_VP_BUTTON_EMERGENCY, crc);
        inject(f, n);
        CHECK(stops == before + 1);
        pop(HMI_VP_BUTTON_EMERGENCY);
        empty();
        CHECK(Hmi_IsControlLocked());
        n = frame(f, HMI_VP_BUTTON_RESET, crc);
        inject(f, n);
        empty();
    }
    Hmi_Init();
    Hmi_SetControlLocked(1);
    n = frame(f, HMI_VP_BUTTON_EMERGENCY, 1);
    f[n - 1] ^= 1;
    uint32_t before = stops;
    inject(f, n);
    CHECK(stops == before);
    empty();
    Hmi_Init();
    CHECK(Hmi_SendText(0x2100, "192.168.1.211", 32));
    CHECK(txSize == 38 && tx[2] == 35 && tx[3] == 0x82 && tx[4] == 0x21 && tx[5] == 0);
    CHECK(!memcmp(tx + 6, "192.168.1.211", 13) && tx[19] == 0xff && tx[20] == 0xff);
    for (unsigned i = 21; i < txSize; i++)
        CHECK(tx[i] == 0);
    CHECK(Hmi_SendText(0x2100, "OFF", 32));
    CHECK(tx[9] == 0xff && tx[10] == 0xff);
    for (unsigned i = 11; i < txSize; i++)
        CHECK(tx[i] == 0);
    char longText[140];
    memset(longText, 'X', sizeof(longText));
    longText[139] = 0;
    CHECK(Hmi_SendText(0x2180, longText, 128));
    CHECK(txSize == 134 && tx[132] == 0xff && tx[133] == 0xff);
    for (unsigned i = 6; i < 132; i++)
        CHECK(tx[i] == 'X');
    unsigned unchanged = txSize;
    CHECK(!Hmi_SendText(0x2100, 0, 16));
    CHECK(!Hmi_SendText(0x2100, "x", 0));
    CHECK(!Hmi_SendText(0x2100, "x", 17));
    CHECK(!Hmi_SendText(0x2100, "x", 130));
    CHECK(!Hmi_SendText(0xffff, "x", 4));
    CHECK(txSize == unchanged);
    CHECK(Hmi_SendText(0x2100, "A\nB", 16));
    CHECK(!memcmp(tx + 6, "A?B", 3));
    txReady = 0;
    CHECK(!Hmi_SendText(0x2100, "BUSY", 16));
    txReady = 1;
    uint8_t cfgCrc[13];
    memcpy(cfgCrc, cfg, sizeof(cfg));
    cfgCrc[2] = 10;
    uint16_t c = crc16(cfgCrc + 3, 8);
    cfgCrc[11] = (uint8_t)c;
    cfgCrc[12] = (uint8_t)(c >> 8);
    inject(cfgCrc, sizeof(cfgCrc));
    CHECK(g_hmi_debug.crc_mode == 2);
    CHECK(Hmi_SendText(0x2180, longText, 128));
    CHECK(txSize == 136 && tx[2] == 133);
    c = crc16(tx + 3, 131);
    CHECK(tx[134] == (uint8_t)c && tx[135] == (uint8_t)(c >> 8));
    printf("ESP32 DWIN stream passed: Emergency priority, CRC, bounded text, truncation, padding, "
           "busy transport\n");
    return 0;
}
