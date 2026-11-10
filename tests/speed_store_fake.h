#ifndef SPEED_STORE_FAKE_H
#define SPEED_STORE_FAKE_H
#include <stdint.h>
void SpeedStoreTest_Reset(void);
void SpeedStoreTest_PowerCycle(void);
void SpeedStoreTest_Corrupt(uint32_t offset, uint32_t mask);
void SpeedStoreTest_FillUnused(uint8_t bank);
extern int speed_test_fail_after;
extern uint8_t speed_test_fail_erase, speed_test_fail_unlock;
extern uint32_t speed_test_writes, speed_test_erases, speed_test_locks;
#endif
