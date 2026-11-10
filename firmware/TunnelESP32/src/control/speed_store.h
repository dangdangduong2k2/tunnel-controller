#ifndef SPEED_STORE_H
#define SPEED_STORE_H
#include <stdint.h>

/* F407VE sectors 6 and 7; both GCC and IAR reserve this range. */
#define SPEED_STORE_BASE 0x08040000UL
#define SPEED_STORE_BANK_BYTES 0x00020000UL
#define SPEED_STORE_BYTES (2UL * SPEED_STORE_BANK_BYTES)
#define SPEED_STORE_RECORD_BYTES 16UL

uint8_t SpeedStore_Load(void);
/* Acknowledges only a verified committed record. No write if unchanged. */
uint8_t SpeedStore_Save(uint8_t level);

/* Flash backend. Offsets are relative to SPEED_STORE_BASE. */
uint32_t SpeedStore_FlashRead(uint32_t offset);
uint8_t SpeedStore_FlashBegin(void);
void SpeedStore_FlashEnd(void);
uint8_t SpeedStore_FlashWrite(uint32_t offset, uint32_t word);
uint8_t SpeedStore_FlashErase(uint8_t bank);
#endif
