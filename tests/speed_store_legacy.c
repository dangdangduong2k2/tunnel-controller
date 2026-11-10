#include "speed_store.h"

#define RECORD_MAGIC 0x53504400UL
#define COMMIT_MAGIC 0x4E575631UL
#define EMPTY_WORD 0xFFFFFFFFUL

typedef struct {
    uint32_t sequence;
    uint32_t next[2];
    uint8_t found, bank, level;
} StoreScan;

static uint32_t RecordCrc(uint32_t sequence, uint32_t value) {
    uint32_t crc = EMPTY_WORD;
    uint8_t byte, bit;
    for (byte = 0U; byte < 8U; ++byte) {
        crc ^= ((byte < 4U) ? sequence : value) >> ((byte % 4U) * 8U) & 0xFFU;
        for (bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
        }
    }
    return ~crc;
}

static StoreScan Scan(void) {
    StoreScan result = {0U, {0U, 0U}, 0U, 0U, 1U};
    uint8_t bank;
    uint32_t pos, offset, sequence, value, crc, commit;
    for (bank = 0U; bank < 2U; ++bank) {
        for (pos = 0U; pos < SPEED_STORE_BANK_BYTES; pos += SPEED_STORE_RECORD_BYTES) {
            offset = (uint32_t)bank * SPEED_STORE_BANK_BYTES + pos;
            sequence = SpeedStore_FlashRead(offset);
            value = SpeedStore_FlashRead(offset + 4U);
            crc = SpeedStore_FlashRead(offset + 8U);
            commit = SpeedStore_FlashRead(offset + 12U);
            /* Skip even a partially written record on the next append. */
            if ((sequence != EMPTY_WORD) || (value != EMPTY_WORD) || (crc != EMPTY_WORD) ||
                (commit != EMPTY_WORD)) {
                result.next[bank] = pos + SPEED_STORE_RECORD_BYTES;
            }
            if ((commit != COMMIT_MAGIC) || ((value & 0xFFFFFF00UL) != RECORD_MAGIC) ||
                ((value & 0xFFU) < 1U) || ((value & 0xFFU) > 5U) ||
                (crc != RecordCrc(sequence, value))) {
                continue;
            }
            if ((result.found == 0U) || ((int32_t)(sequence - result.sequence) > 0)) {
                result.found = 1U;
                result.bank = bank;
                result.sequence = sequence;
                result.level = (uint8_t)value;
            }
        }
    }
    return result;
}

uint8_t SpeedStore_Load(void) {
    return Scan().level;
}

uint8_t SpeedStore_Save(uint8_t level) {
    StoreScan old, check;
    uint8_t bank, ok = 0U;
    uint32_t pos, offset, sequence, value;
    if ((level < 1U) || (level > 5U)) {
        return 0U;
    }
    old = Scan();
    if (level == old.level) {
        return 1U;
    }
    bank = old.bank;
    pos = old.next[bank];
    if (pos >= SPEED_STORE_BANK_BYTES) {
        bank ^= 1U;
        pos = 0U;
    }
    if (SpeedStore_FlashBegin() == 0U) {
        return 0U;
    }
    /* The bank containing the last committed value is NEVER erased before a
       newer record is committed in the other bank. Power loss preserves it. */
    if ((pos == 0U) && (old.next[bank] != 0U)) {
        if (SpeedStore_FlashErase(bank) == 0U) {
            goto done;
        }
    }
    sequence = old.found ? old.sequence + 1U : 1U;
    value = RECORD_MAGIC | level;
    offset = (uint32_t)bank * SPEED_STORE_BANK_BYTES + pos;
    if ((SpeedStore_FlashWrite(offset, sequence) == 0U) ||
        (SpeedStore_FlashWrite(offset + 4U, value) == 0U) ||
        (SpeedStore_FlashWrite(offset + 8U, RecordCrc(sequence, value)) == 0U)) {
        goto done;
    }
    if ((SpeedStore_FlashRead(offset) != sequence) ||
        (SpeedStore_FlashRead(offset + 4U) != value) ||
        (SpeedStore_FlashRead(offset + 8U) != RecordCrc(sequence, value))) {
        goto done;
    }
    if (SpeedStore_FlashWrite(offset + 12U, COMMIT_MAGIC) == 0U) {
        goto done;
    }
    check = Scan();
    ok = (check.found && check.sequence == sequence && check.level == level) ? 1U : 0U;
done:
    SpeedStore_FlashEnd();
    return ok;
}
