#include "SharedUart.h"
#include "../../hmi/hmi.h"
#include "pc_control.h"
#include "../../platform/esp32/EspPlatform.h"
#include "../../platform/compat/stm32f4xx_hal.h"
#include <string.h>

/* PC wire envelope: @TUN:<uppercase hexadecimal of binary PC v5 packet>\n
 * It never contains raw 5A/A5, so a PC reply cannot become a DWIN write.
 * Complete DWIN frames retain their native byte-by-byte generation fencing.
 * This framing separates protocols, not electrically colliding transmitters. */
enum { IDLE, DWIN_SECOND, DWIN_LENGTH, DWIN_BODY, PC_PREFIX, PC_HEX };
static uint8_t state, remaining, prefix_index, nibble, half, packet[96];
static uint16_t size;
static uint32_t stamp;
static const char prefix[] = "@TUN:";
void SharedUart_Init(void) {
    state = IDLE;
    size = 0U;
    half = 0U;
    stamp = 0U;
}
void SharedUart_Fence(void) {
    SharedUart_Init();
}
static int Hex(uint8_t v) {
    if (v >= '0' && v <= '9')
        return v - '0';
    if (v >= 'A' && v <= 'F')
        return v - 'A' + 10;
    return -1;
}
void SharedUart_ReceiveByte(uint8_t v) {
    uint32_t now = HAL_GetTick();
    if (state != IDLE && now - stamp >= 50U)
        state = IDLE;
    stamp = now;
    if (state == DWIN_SECOND) {
        Hmi_ReceiveByte(v);
        state = v == 0xA5U ? DWIN_LENGTH : (v == 0x5AU ? DWIN_SECOND : IDLE);
        return;
    }
    if (state == DWIN_LENGTH) {
        Hmi_ReceiveByte(v);
        remaining = v;
        state = v >= 3U && v <= 32U ? DWIN_BODY : IDLE;
        return;
    }
    if (state == DWIN_BODY) {
        Hmi_ReceiveByte(v);
        if (--remaining == 0U)
            state = IDLE;
        return;
    }
    /* A native DWIN header can interrupt a partial ASCII PC envelope. */
    if (v == 0x5AU) {
        state = DWIN_SECOND;
        Hmi_ReceiveByte(v);
        return;
    }
    if (v == '@') {
        state = PC_PREFIX;
        prefix_index = 1U;
        size = 0U;
        half = 0U;
        return;
    }
    if (state == PC_PREFIX) {
        if (v != (uint8_t)prefix[prefix_index]) {
            state = IDLE;
            return;
        }
        if (++prefix_index == 5U)
            state = PC_HEX;
        return;
    }
    if (state == PC_HEX) {
        int h = Hex(v);
        if (v == '\n') {
            state = IDLE;
            if (half || !PcControl_ReceivePacket(packet, size))
                EspRequestSafetyStop(STOP_BAD_PACKET);
            return;
        }
        if (h < 0 || size >= sizeof(packet)) {
            state = IDLE;
            EspRequestSafetyStop(STOP_BAD_PACKET);
            return;
        }
        if (!half) {
            nibble = (uint8_t)h;
            half = 1U;
        } else {
            packet[size++] = (uint8_t)((nibble << 4U) | h);
            half = 0U;
        }
    }
}
uint16_t SharedUart_Encode(const uint8_t *bytes, uint16_t length, uint8_t *out, uint16_t capacity) {
    static const char digits[] = "0123456789ABCDEF";
    uint16_t i, n = (uint16_t)(length * 2U + 6U);
    if (!length || length > 96U || capacity < n)
        return 0U;
    memcpy(out, prefix, 5U);
    for (i = 0U; i < length; i++) {
        out[5U + 2U * i] = (uint8_t)digits[bytes[i] >> 4U];
        out[6U + 2U * i] = (uint8_t)digits[bytes[i] & 15U];
    }
    out[n - 1U] = '\n';
    return n;
}
