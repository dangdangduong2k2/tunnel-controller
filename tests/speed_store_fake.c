#include "speed_store.h"
#include "speed_store_fake.h"
#include <assert.h>
#include <string.h>

static uint32_t flash_words[SPEED_STORE_BYTES / 4U];
static uint8_t unlocked;
int speed_test_fail_after;
uint8_t speed_test_fail_erase, speed_test_fail_unlock;
uint32_t speed_test_writes, speed_test_erases, speed_test_locks;

void SpeedStoreTest_PowerCycle(void) {
    unlocked = 0U;
    speed_test_fail_after = -1;
    speed_test_fail_erase = 0U;
    speed_test_fail_unlock = 0U;
}
void SpeedStoreTest_Reset(void) {
    memset(flash_words, 0xFF, sizeof(flash_words));
    SpeedStoreTest_PowerCycle();
    speed_test_writes = speed_test_erases = speed_test_locks = 0U;
}
uint32_t SpeedStore_FlashRead(uint32_t offset) {
    assert(offset < SPEED_STORE_BYTES && (offset & 3U) == 0U);
    return flash_words[offset / 4U];
}
uint8_t SpeedStore_FlashBegin(void) {
    if (speed_test_fail_unlock) {
        return 0U;
    }
    unlocked = 1U;
    return 1U;
}
void SpeedStore_FlashEnd(void) {
    unlocked = 0U;
    speed_test_locks++;
}
uint8_t SpeedStore_FlashWrite(uint32_t offset, uint32_t word) {
    assert(unlocked && offset < SPEED_STORE_BYTES && (offset & 3U) == 0U);
    assert(flash_words[offset / 4U] == 0xFFFFFFFFUL); /* Never program a used slot. */
    if (speed_test_fail_after == 0) {
        flash_words[offset / 4U] &= word | 0xFFFF0000UL; /* Torn programming. */
        return 0U;
    }
    if (speed_test_fail_after > 0) {
        speed_test_fail_after--;
    }
    flash_words[offset / 4U] = word;
    speed_test_writes++;
    return 1U;
}
uint8_t SpeedStore_FlashErase(uint8_t bank) {
    assert(unlocked && bank < 2U);
    speed_test_erases++;
    memset(flash_words + (uint32_t)bank * SPEED_STORE_BANK_BYTES / 4U, 0xFF,
           speed_test_fail_erase ? SPEED_STORE_BANK_BYTES / 2U : SPEED_STORE_BANK_BYTES);
    return speed_test_fail_erase ? 0U : 1U;
}
void SpeedStoreTest_Corrupt(uint32_t offset, uint32_t mask) {
    assert(offset < SPEED_STORE_BYTES);
    flash_words[offset / 4U] ^= mask;
}
void SpeedStoreTest_FillUnused(uint8_t bank) {
    uint32_t i;
    for (i = (uint32_t)bank * SPEED_STORE_BANK_BYTES / 4U;
         i < (uint32_t)(bank + 1U) * SPEED_STORE_BANK_BYTES / 4U; ++i) {
        if (flash_words[i] == 0xFFFFFFFFUL) {
            flash_words[i] = 0U;
        }
    }
}
