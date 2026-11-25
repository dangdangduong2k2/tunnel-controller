#include "zk7182.h"
#include "../config/app_config.h"
#include "../platform/compat/usb_host.h"
#include "../platform/compat/usbh_cp210x.h"
#include "../platform/compat/stm32f4xx_hal.h"
#include "../common/debug_state.h"
#include <string.h>
#include <stdlib.h>

#ifdef TUNNEL_READER_MEMORY_TEST
extern void *ZK7182_TestAlloc(size_t size);
extern void ZK7182_TestFree(void *ptr);
#define EPC_ALLOC ZK7182_TestAlloc
#define EPC_FREE ZK7182_TestFree
#else
#define EPC_ALLOC malloc
#define EPC_FREE free
#endif

/* ZK SDK manual V2.20, 8.4.41/42 (Ex10):
 * START 50 + Target A, STOP 51 without payload; tags are reported as EE.
 * Q/Session, dwell, antenna selection, power, RF profile and frequency are
 * owned by the reader. There are deliberately NO configuration commands.
 * Target A is the mandatory START operand, not a saved-parameter write. */
#define ZK_CMD_START 0x50U
#define ZK_CMD_STOP 0x51U
#define ZK_CMD_TAG 0xEEU
#define ZK_RX_SIZE 64U
#define ZK_FRAME_SIZE 256U

typedef struct {
    uint8_t length;
    uint8_t data[ZK7182_MAX_EPC_BYTES];
} ZK7182_UniqueEpc;

static uint8_t rx_packet[ZK_RX_SIZE];
static uint8_t tx_frame[10];
static uint8_t pc_pending, pc_done, pc_reply_status, pc_reply_length;
static uint8_t pc_reply[64];
static uint8_t parse_frame[ZK_FRAME_SIZE];
static uint16_t parse_used;
static uint32_t parse_tick;
static uint8_t transport_ready;
static uint8_t rx_armed;
static uint8_t tx_busy;
static uint8_t expected_command;
static uint8_t response_armed;
static uint32_t command_deadline;
static uint32_t boot_deadline;
static uint8_t boot_done;
/* Keep this uncertainty through cancellation/disconnection until STOP ACK. */
static uint8_t reader_may_run;
static uint8_t reader_started;
static uint8_t force_stop;
static uint8_t stop_attempts;
static uint8_t recovery_requested;
static uint32_t stop_retry_tick;
static uint8_t quiet;
static uint32_t quiet_deadline;
static volatile uint8_t scan_active;
static volatile uint8_t scan_paused;
static uint8_t scan_complete;
static uint8_t scan_started_once;
static uint8_t time_stop;
static uint8_t start_failures;
static uint8_t silence_restarts;
static uint32_t last_inventory_tick;
static uint32_t scan_deadline;
static uint32_t stop_deadline;
static volatile uint16_t unique_count;
static uint32_t total_count;
static ZK7182_ScanResult scan_result;
static ZK7182_ScanResult scan_error;
/* Deduplication needs 65 KiB only while a scan is active. Reserve every
 * chunk before START, keeping full capacity even with a fragmented heap.
 * Only the owner task allocates/frees; ISR-safe pause/cancel just set flags.
 * Completed counts and the last EPC remain in their existing scalar/debug
 * fields. Late reports cannot access this store once scan_active is false. */
#define EPC_CHUNK_TAGS 32U
#define EPC_CHUNKS ((ZK7182_MAX_UNIQUE_TAGS + EPC_CHUNK_TAGS - 1U) / EPC_CHUNK_TAGS)
static ZK7182_UniqueEpc *epc_chunks[EPC_CHUNKS];
static void ReleaseEpcStore(void) {
    uint16_t i;
    for (i = 0U; i < EPC_CHUNKS; ++i) {
        if (epc_chunks[i] != NULL) {
            EPC_FREE(epc_chunks[i]);
            epc_chunks[i] = NULL;
        }
    }
}
static uint8_t ReserveEpcStore(void) {
    uint16_t i;
    for (i = 0U; i < EPC_CHUNKS; ++i) {
        if (epc_chunks[i] == NULL) {
            epc_chunks[i] = (ZK7182_UniqueEpc *)EPC_ALLOC(
                EPC_CHUNK_TAGS * sizeof(ZK7182_UniqueEpc));
            if (epc_chunks[i] == NULL) {
                ReleaseEpcStore();
                return 0U; /* No RF START and no change to previous counts. */
            }
        }
    }
    return 1U;
}
static ZK7182_UniqueEpc *EpcAt(uint16_t i) {
    return &epc_chunks[i / EPC_CHUNK_TAGS][i % EPC_CHUNK_TAGS];
}

static uint8_t TimeReached(uint32_t deadline) {
    return ((int32_t)(HAL_GetTick() - deadline) >= 0) ? 1U : 0U;
}

static uint16_t Crc16(const uint8_t *data, uint16_t length) {
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t bit;
    for (i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; ++bit) {
            crc = (uint16_t)((crc >> 1U) ^ ((crc & 1U) ? 0x8408U : 0U));
        }
    }
    return crc;
}

static void ClearExpected(void) {
    expected_command = 0U;
    response_armed = 0U;
    g_debug.reader_expected_command = 0U;
}

static void Terminal(ZK7182_ScanResult result) {
    scan_active = 0U;
    scan_complete = 1U;
    scan_paused = 0U;
    scan_result = result;
    if (reader_may_run != 0U) {
        force_stop = 1U;
    }
    g_debug.scan_active = 0U;
    g_debug.scan_complete = 1U;
    g_debug.scan_result = (uint8_t)result;
    g_debug.tunnel_scan_paused = 0U;
    if (result != ZK7182_SCAN_RESULT_OK) {
        g_debug.scan_failure_count++;
    }
}

static void ExpireScan(void) {
    ZK7182_ScanResult result;
    if ((scan_active == 0U) || (TimeReached(scan_deadline) == 0U)) {
        return;
    }
    result = scan_error;
    if (result == ZK7182_SCAN_RESULT_IDLE) {
        result = ((scan_started_once == 0U) || (reader_may_run != 0U))
                     ? ZK7182_SCAN_RESULT_NO_RESPONSE
                     : ZK7182_SCAN_RESULT_OK;
    }
    /* Freeze EPCs at 30 s, but keep STOP/recovery alive outside the scan. */
    Terminal(result);
}

/* A serial failure must not end the parcel's mechanical session. Recover
 * within its original deadline; terminal results are reserved for deadline
 * and EPC capacity. No recovery path writes a reader setting. */
static void RecoverScan(ZK7182_ScanResult error) {
    if (scan_active != 0U) {
        scan_error = error;
    }
}

static void ConfirmStart(uint8_t by_data) {
    reader_started = 1U;
    scan_started_once = 1U;
    last_inventory_tick = HAL_GetTick();
    g_debug.inventory_running = 1U;
    if (by_data != 0U) {
        g_debug.inventory_start_data_confirm_count++;
    }
}

static void Malformed(void) {
    g_debug.inventory_malformed_frame_count++;
    if ((scan_active != 0U) && (scan_error == ZK7182_SCAN_RESULT_IDLE)) {
        scan_error = ZK7182_SCAN_RESULT_PROTOCOL_ERROR;
    }
}

static void StoreEpc(const uint8_t *epc, uint8_t length) {
    static const char digits[] = "0123456789ABCDEF";
    uint16_t i;
    total_count++;
    g_debug.epc_count = total_count;
    g_debug.last_epc_length = length;
    for (i = 0U; i < length; ++i) {
        g_debug.last_epc[i] = epc[i];
        g_debug.last_epc_text[i * 2U] = digits[epc[i] >> 4U];
        g_debug.last_epc_text[i * 2U + 1U] = digits[epc[i] & 15U];
    }
    g_debug.last_epc_text[length * 2U] = '\0';
    for (i = 0U; i < unique_count; ++i) {
        const ZK7182_UniqueEpc *stored = EpcAt(i);
        if ((stored->length == length) && (memcmp(stored->data, epc, length) == 0)) {
            g_debug.duplicate_epc_count++;
            return;
        }
    }
    if (unique_count == ZK7182_MAX_UNIQUE_TAGS) {
        g_debug.unique_epc_overflow_count++;
        Terminal(ZK7182_SCAN_RESULT_CAPACITY_OVERFLOW);
        return;
    }
    EpcAt(unique_count)->length = length;
    (void)memcpy(EpcAt(unique_count)->data, epc, length);
    unique_count++;
    g_debug.unique_epc_count = unique_count;
}

static void FrameReceived(const uint8_t *frame, uint16_t length) {
    uint8_t command = frame[2];
    uint8_t status = frame[3];
    uint8_t epc_length;
    ExpireScan();
    if ((pc_pending != 0U) && (response_armed != 0U) &&
        ((command == expected_command) || ((command == 0U) && (status == 0xFEU)))) {
        if ((length >= 6U) && (length - 4U <= sizeof(pc_reply))) {
            pc_reply_length = (uint8_t)(length - 4U);
            (void)memcpy(pc_reply, frame + 2U, pc_reply_length);
            pc_reply_status = 0U;
        } else {
            pc_reply_length = 0U;
            pc_reply_status = 5U;
        }
        pc_pending = 0U;
        pc_done = 1U;
        ClearExpected();
        g_debug.last_status = status;
        return;
    }
    if (command == ZK_CMD_TAG) {
        /* Heartbeats carry no tag. They cannot prove a new scan started or
         * turn a successfully stopped reader back into "may be running". */
        if ((status == 0x28U) && ((length == 6U) || (length == 10U))) {
            return;
        }
        if ((status != 0U) || (length < 10U)) {
            Malformed();
            return;
        }
        epc_length = frame[5];
        if ((epc_length == 0U) || (epc_length > ZK7182_MAX_EPC_BYTES) ||
            (length != (uint16_t)epc_length + 9U)) {
            Malformed();
            return;
        }
        if (quiet != 0U) {
            quiet_deadline = HAL_GetTick() + ZK7182_STOP_QUIET_MS;
        } else if (reader_may_run == 0U) {
            reader_may_run = 1U;
            force_stop = 1U;
            stop_attempts = 0U;
            stop_retry_tick = HAL_GetTick();
        }
        /* A CRC-checked tag after OUR transmitted START is positive evidence
         * that inventory is running, even when the separate START ACK is lost.
         * Never use idle/stale tags or a frame before TX completion as evidence. */
        if ((scan_active != 0U) && (quiet == 0U) && (expected_command == ZK_CMD_START) &&
            (response_armed != 0U)) {
            ClearExpected();
            ConfirmStart(1U);
        }
        if ((scan_active == 0U) || (reader_started == 0U) || (quiet != 0U)) {
            g_debug.inventory_stale_frame_count++;
            g_debug.inventory_rejected_frame_count++;
            return;
        }
        last_inventory_tick = HAL_GetTick();
        g_debug.inventory_rx_frame_count++;
        g_debug.inventory_last_total++;
        g_debug.last_antenna = frame[4];
        /* EE reports a bitmap. Do not assume that A1/A2/A3 are enabled. */
        g_debug.scan_antenna_mask |= frame[4];
        g_debug.last_rssi = frame[6U + epc_length];
        StoreEpc(frame + 6U, epc_length);
        return;
    }
    if ((command != expected_command) || (response_armed == 0U) || (length != 6U)) {
        g_debug.inventory_rejected_frame_count++;
        return;
    }
    ClearExpected();
    g_debug.last_status = status;
    g_debug.inventory_last_error = status;
    if (command == ZK_CMD_START) {
        if (status == 0U) {
            ConfirmStart(0U);
            g_debug.inventory_start_ack_count++;
        } else {
            g_debug.inventory_error_count++;
            start_failures++;
            RecoverScan(ZK7182_SCAN_RESULT_READER_ERROR);
            force_stop = 1U;
        }
    } else if (command == ZK_CMD_STOP) {
        if (status == 0U) {
            reader_may_run = 0U;
            reader_started = 0U;
            force_stop = 0U;
            quiet = 1U;
            quiet_deadline = HAL_GetTick() + ZK7182_STOP_QUIET_MS;
            stop_attempts = 0U;
            g_debug.inventory_running = 0U;
            g_debug.inventory_stop_ack_count++;
            g_debug.inventory_round_count++;
        } else {
            force_stop = 1U;
            stop_retry_tick = HAL_GetTick() + ZK7182_COMMAND_TIMEOUT_MS;
            g_debug.inventory_error_count++;
            RecoverScan(ZK7182_SCAN_RESULT_READER_ERROR);
        }
    }
}

static void DropParserByte(void) {
    --parse_used;
    (void)memmove(parse_frame, parse_frame + 1U, parse_used);
}

static void ParseBytes(const uint8_t *data, uint16_t length) {
    uint16_t i, frame_length, crc;
    for (i = 0U; i < length; ++i) {
        parse_frame[parse_used++] = data[i];
        while (parse_used != 0U) {
            if ((parse_frame[0] < 5U) ||
                ((parse_used >= 2U) && (parse_frame[1] != ZK7182_READER_ADDRESS)) ||
                ((parse_used >= 3U) && (parse_frame[2] != ZK_CMD_START) &&
                 (parse_frame[2] != ZK_CMD_STOP) && (parse_frame[2] != ZK_CMD_TAG) &&
                 !((pc_pending != 0U) &&
                   ((parse_frame[2] == expected_command) || (parse_frame[2] == 0U))))) {
                DropParserByte();
                continue;
            }
            frame_length = (uint16_t)parse_frame[0] + 1U;
            if ((parse_used >= 3U) &&
                (((parse_frame[2] != ZK_CMD_TAG) && (frame_length != 6U) &&
                  !((pc_pending != 0U) && (parse_frame[2] == expected_command) &&
                    (frame_length <= 68U))) ||
                 ((parse_frame[2] == ZK_CMD_TAG) && (frame_length > ZK7182_MAX_EPC_BYTES + 9U)))) {
                DropParserByte();
                continue;
            }
            if ((parse_used >= 6U) && (parse_frame[2] == ZK_CMD_TAG) && (parse_frame[3] == 0U) &&
                ((parse_frame[5] == 0U) || (frame_length != parse_frame[5] + 9U))) {
                DropParserByte();
                continue;
            }
            if (parse_used < frame_length) {
                break;
            }
            crc = Crc16(parse_frame, frame_length - 2U);
            if ((parse_frame[frame_length - 2U] != (uint8_t)crc) ||
                (parse_frame[frame_length - 1U] != (uint8_t)(crc >> 8U))) {
                g_debug.checksum_error_count++;
                g_debug.last_error = DEBUG_ERROR_ZK_CHECKSUM;
                Malformed();
                DropParserByte();
                continue;
            }
            g_debug.valid_frame_count++;
            g_debug.last_frame_length = frame_length;
            g_debug.last_command = parse_frame[2];
            g_debug.reader_address = parse_frame[1];
            (void)memcpy((void *)g_debug.last_frame, parse_frame, frame_length);
            FrameReceived(parse_frame, frame_length);
            parse_used -= frame_length;
            (void)memmove(parse_frame, parse_frame + frame_length, parse_used);
        }
    }
    parse_tick = HAL_GetTick();
}

static USBH_StatusTypeDef SendCommand(uint8_t command) {
    uint16_t length, crc;
    if ((command != ZK_CMD_START) && (command != ZK_CMD_STOP)) {
        return USBH_FAIL;
    }
    if ((transport_ready == 0U) || (tx_busy != 0U) || (expected_command != 0U)) {
        return USBH_BUSY;
    }
    length = (command == ZK_CMD_START) ? 6U : 5U;
    tx_frame[0] = (uint8_t)(length - 1U);
    tx_frame[1] = ZK7182_READER_ADDRESS;
    tx_frame[2] = command;
    if (command == ZK_CMD_START) {
        tx_frame[3] = 0U;
    } /* Mandatory Target A. */
    crc = Crc16(tx_frame, length - 2U);
    tx_frame[length - 2U] = (uint8_t)crc;
    tx_frame[length - 1U] = (uint8_t)(crc >> 8U);
    if (USBH_CP210X_Transmit(&hUsbHostFS, tx_frame, length) != USBH_OK) {
        return USBH_BUSY;
    }
    tx_busy = 1U;
    expected_command = command;
    response_armed = 0U;
    command_deadline = HAL_GetTick() + ZK7182_COMMAND_TIMEOUT_MS;
    g_debug.tx_busy = 1U;
    g_debug.reader_expected_command = command;
    g_debug.last_tx_frame_length = length;
    (void)memcpy((void *)g_debug.last_tx_frame, tx_frame, length);
    g_debug.tx_frame_count++;
    g_debug.tx_bytes_total += length;
    if (command == ZK_CMD_START) {
        reader_may_run = 1U;
        g_debug.inventory_tx_count++;
    } else {
        stop_attempts++;
        g_debug.inventory_stop_tx_count++;
    }
    return USBH_OK;
}

void ZK7182_Init(void) {
    ReleaseEpcStore();
    pc_pending = 0U;
    pc_done = 0U;
    pc_reply_length = 0U;
    pc_reply_status = 0U;
    parse_used = 0U;
    transport_ready = 0U;
    rx_armed = 0U;
    tx_busy = 0U;
    ClearExpected();
    boot_done = 0U;
    reader_may_run = 0U;
    reader_started = 0U;
    force_stop = 0U;
    stop_attempts = 0U;
    recovery_requested = 0U;
    quiet = 0U;
    scan_active = 0U;
    scan_paused = 0U;
    scan_complete = 0U;
    scan_started_once = 0U;
    time_stop = 0U;
    start_failures = 0U;
    silence_restarts = 0U;
    unique_count = 0U;
    total_count = 0U;
    scan_result = ZK7182_SCAN_RESULT_IDLE;
    scan_error = ZK7182_SCAN_RESULT_IDLE;
    g_debug.rf_profile_requested = 0xFFU;
    g_debug.rf_profile_current = 0xFFU;
    g_debug.rf_profile_verified = 0U;
    g_debug.rf_sweep_enabled = 0U;
    g_debug.reader_start_only = 1U;
    g_debug.reader_config_state = 0U;
}

void ZK7182_TransportReady(void) {
    transport_ready = 1U;
    rx_armed = 0U;
    tx_busy = 0U;
    ClearExpected();
    parse_used = 0U;
    boot_done = 0U;
    boot_deadline = HAL_GetTick() + ZK7182_BOOT_DELAY_MS;
    quiet = 0U;
    stop_attempts = 0U;
    recovery_requested = 0U;
    stop_retry_tick = HAL_GetTick();
    if (reader_may_run != 0U) {
        force_stop = 1U;
    }
    g_debug.cp210x_ready = 1U;
    g_debug.rx_armed = 0U;
    g_debug.tx_busy = 0U;
    g_debug.reader_config_state = 1U;
}

void ZK7182_TransportLost(void) {
    if (pc_pending != 0U) {
        pc_pending = 0U;
        pc_done = 1U;
        pc_reply_length = 0U;
        pc_reply_status = 4U;
    }
    transport_ready = 0U;
    rx_armed = 0U;
    tx_busy = 0U;
    ClearExpected();
    parse_used = 0U;
    boot_done = 0U;
    reader_started = 0U;
    if (reader_may_run != 0U) {
        force_stop = 1U;
    }
    RecoverScan(ZK7182_SCAN_RESULT_TRANSPORT_LOST);
    g_debug.cp210x_ready = 0U;
    g_debug.rx_armed = 0U;
    g_debug.tx_busy = 0U;
    g_debug.inventory_running = 0U;
    g_debug.reader_config_state = 0U;
}

void ZK7182_Process(void) {
    uint32_t interrupt_state;
    uint8_t need_stop;
    ExpireScan(); /* Deadline also runs while USB is disconnected. */
    if (scan_active == 0U) {
        ReleaseEpcStore();
    }
    if ((transport_ready == 0U) || (USBH_CP210X_IsReady(&hUsbHostFS) == 0U)) {
        return;
    }
    if ((rx_armed == 0U) &&
        (USBH_CP210X_Receive(&hUsbHostFS, rx_packet, sizeof(rx_packet)) == USBH_OK)) {
        rx_armed = 1U;
        g_debug.rx_armed = 1U;
    }
    if ((parse_used != 0U) && ((HAL_GetTick() - parse_tick) >= ZK7182_COMMAND_TIMEOUT_MS)) {
        parse_used = 0U;
        Malformed();
    }
    ExpireScan();
    if ((boot_done == 0U) && (TimeReached(boot_deadline) != 0U)) {
        boot_done = 1U;
        g_debug.reader_config_state = 8U; /* Transport ready; NO reader setup. */
    }
    if ((quiet != 0U) && (TimeReached(quiet_deadline) != 0U)) {
        quiet = 0U;
        parse_used = 0U;
    }
    if ((scan_active != 0U) && (TimeReached(stop_deadline) != 0U)) {
        time_stop = 1U;
    }
    if (recovery_requested != 0U) {
        return;
    }
    if ((expected_command != 0U) && (TimeReached(command_deadline) != 0U)) {
        if (pc_pending != 0U) {
            pc_pending = 0U;
            pc_done = 1U;
            pc_reply_length = 0U;
            pc_reply_status = 6U;
        }
        g_debug.inventory_timeout_count++;
        if (expected_command == ZK_CMD_START) {
            start_failures++;
        }
        RecoverScan(ZK7182_SCAN_RESULT_NO_RESPONSE);
        force_stop = reader_may_run;
        if (tx_busy != 0U) {
            if (recovery_requested == 0U) {
                recovery_requested = 1U;
                USB_HOST_RequestRecovery();
            }
            return; /* Never overwrite a transmit buffer still owned by USB. */
        }
        ClearExpected();
        parse_used = 0U;
    }
    if ((expected_command != 0U) || (tx_busy != 0U) || (boot_done == 0U)) {
        return;
    }
    /* Some saved reader modes stop reporting by themselves. Try a bounded
     * STOP/START after prolonged silence, without changing those modes. Empty
     * parcels remain valid; this is a retry, not evidence that tags exist. */
    if ((scan_active != 0U) && (scan_paused == 0U) && (time_stop == 0U) && (reader_started != 0U) &&
        (force_stop == 0U) && (silence_restarts < ZK7182_SILENCE_MAX_RESTARTS) &&
        ((HAL_GetTick() - last_inventory_tick) >= ZK7182_SILENCE_RESTART_MS)) {
        force_stop = 1U;
        silence_restarts++;
        g_debug.inventory_silence_restart_count++;
    }
    need_stop = (uint8_t)((reader_may_run != 0U) && ((scan_active == 0U) || (scan_paused != 0U) ||
                                                     (time_stop != 0U) || (force_stop != 0U)));
    if (need_stop != 0U) {
        if (stop_attempts >= ZK7182_STOP_MAX_ATTEMPTS) {
            if (recovery_requested == 0U) {
                recovery_requested = 1U;
                USB_HOST_RequestRecovery();
            }
            return;
        }
        if (TimeReached(stop_retry_tick) != 0U) {
            (void)SendCommand(ZK_CMD_STOP);
        }
        return;
    }
    /* Serialize START with ISR pause, including an SS3 edge in this main pass. */
    interrupt_state = __get_PRIMASK();
    __disable_irq();
    if ((scan_active != 0U) && (scan_paused == 0U) && (time_stop == 0U) && (reader_may_run == 0U) &&
        (force_stop == 0U) && (quiet == 0U) && (start_failures < ZK7182_START_MAX_FAILURES) &&
        (TimeReached(stop_deadline) == 0U)) {
        (void)SendCommand(ZK_CMD_START);
    }
    if (interrupt_state == 0U) {
        __enable_irq();
    }
}

uint8_t ZK7182_IsReady(void) {
    return ((transport_ready != 0U) && (boot_done != 0U) &&
            (USBH_CP210X_IsReady(&hUsbHostFS) != 0U) && (reader_may_run == 0U) && (tx_busy == 0U) &&
            (expected_command == 0U) && (quiet == 0U) && (force_stop == 0U) &&
            (recovery_requested == 0U))
               ? 1U
               : 0U;
}

uint8_t ZK7182_StartScan(uint32_t duration_ms) {
    if ((ZK7182_IsReady() == 0U) || (scan_active != 0U)) {
        return 0U;
    }
    if (ReserveEpcStore() == 0U) {
        return 0U;
    }
    if (duration_ms == 0U) {
        duration_ms = ZK7182_SCAN_DURATION_MS;
    }
    if (duration_ms > ZK7182_SCAN_MAX_DURATION_MS) {
        duration_ms = ZK7182_SCAN_MAX_DURATION_MS;
    }
    parse_used = 0U;
    unique_count = 0U;
    total_count = 0U;
    scan_active = 1U;
    scan_complete = 0U;
    scan_paused = 0U;
    scan_started_once = 0U;
    time_stop = 0U;
    start_failures = 0U;
    silence_restarts = 0U;
    last_inventory_tick = HAL_GetTick();
    scan_error = ZK7182_SCAN_RESULT_IDLE;
    scan_result = ZK7182_SCAN_RESULT_IN_PROGRESS;
    scan_deadline = HAL_GetTick() + duration_ms;
    stop_deadline = scan_deadline;
    if (duration_ms > ZK7182_INVENTORY_END_GUARD_MS) {
        stop_deadline -= ZK7182_INVENTORY_END_GUARD_MS;
    }
    stop_attempts = 0U;
    stop_retry_tick = HAL_GetTick();
    g_debug.epc_count = 0U;
    g_debug.unique_epc_count = 0U;
    g_debug.duplicate_epc_count = 0U;
    g_debug.unique_epc_overflow_count = 0U;
    g_debug.scan_antenna_mask = 0U;
    g_debug.scan_duration_ms = duration_ms;
    g_debug.scan_session_count++;
    g_debug.scan_active = 1U;
    g_debug.scan_complete = 0U;
    g_debug.scan_result = (uint8_t)scan_result;
    g_debug.tunnel_scan_paused = 0U;
    g_debug.inventory_last_total = 0U;
    return 1U;
}

uint8_t ZK7182_IsInventoryRunning(void) {
    return (scan_active && !scan_paused && reader_started) ? 1U : 0U;
}
uint8_t ZK7182_IsScanActive(void) {
    return scan_active;
}
uint8_t ZK7182_IsScanComplete(void) {
    return scan_complete;
}
uint8_t ZK7182_IsInventoryResponsePending(void) {
    return reader_may_run;
}
ZK7182_ScanResult ZK7182_GetScanResult(void) {
    return scan_result;
}
uint16_t ZK7182_GetUniqueEpcCount(void) {
    return unique_count;
}
uint32_t ZK7182_GetTotalReadCount(void) {
    return total_count;
}

void ZK7182_PauseScan(void) {
    if (scan_active != 0U) {
        scan_paused = 1U;
        g_debug.tunnel_scan_paused = 1U;
    }
}

void ZK7182_ResumeScan(void) {
    if (scan_active != 0U) {
        scan_paused = 0U;
        g_debug.tunnel_scan_paused = 0U;
    }
}

void ZK7182_CancelScan(void) {
    scan_active = 0U;
    scan_complete = 0U;
    scan_paused = 0U;
    scan_result = ZK7182_SCAN_RESULT_IDLE;
    if (reader_may_run != 0U) {
        force_stop = 1U;
    }
    g_debug.scan_active = 0U;
    g_debug.scan_complete = 0U;
    g_debug.tunnel_scan_paused = 0U;
    /* STOP is sent by Process even after the caller freezes/releases results. */
}

void ZK7182_CancelScanAtUniqueCount(uint16_t count) {
    if (count < unique_count) {
        unique_count = count;
    }
    g_debug.unique_epc_count = unique_count;
    ZK7182_CancelScan();
}

void ZK7182_ClearScanComplete(void) {
    scan_complete = 0U;
    g_debug.scan_complete = 0U;
    if (scan_active == 0U) {
        scan_result = ZK7182_SCAN_RESULT_IDLE;
    }
}

void USBH_CP210X_TransmitCallback(USBH_HandleTypeDef *host) {
    (void)host;
    tx_busy = 0U;
    g_debug.tx_busy = 0U;
    if (expected_command != 0U) {
        response_armed = 1U;
        command_deadline = HAL_GetTick() + ZK7182_COMMAND_TIMEOUT_MS;
    }
}

void USBH_CP210X_ReceiveCallback(USBH_HandleTypeDef *host) {
    uint16_t received = USBH_CP210X_GetLastReceivedDataSize(host);
    rx_armed = 0U;
    g_debug.rx_armed = 0U;
    if (transport_ready == 0U) {
        return;
    }
    if (received > sizeof(rx_packet)) {
        Malformed();
        return;
    }
    g_debug.rx_bytes_total += received;
    if (received == 0U) {
        return;
    }
    if ((parse_used != 0U) && ((HAL_GetTick() - parse_tick) >= ZK7182_COMMAND_TIMEOUT_MS)) {
        parse_used = 0U;
        Malformed();
    }
    g_debug.raw_rx_packet_count++;
    g_debug.last_raw_rx_length = received;
    (void)memcpy((void *)g_debug.last_raw_rx, rx_packet, received);
    ParseBytes(rx_packet, received);
}

void USBH_CP210X_ErrorCallback(USBH_HandleTypeDef *host) {
    (void)host;
    ZK7182_TransportLost();
    USB_HOST_RequestRecovery();
}

uint8_t ZK7182_BeginPcCommand(uint8_t command, const uint8_t *data, uint8_t length) {
    uint16_t crc, frame_length;
    uint32_t frequency;
    uint8_t valid = 0U, profile, i;
    if ((length != 0U) && (data == NULL)) {
        return 0U;
    }
    if ((command == 0x21U) && (length == 0U)) {
        valid = 1U;
    }
    /* UHFReader288Demo: GetAntennaPower 94; SetAntennaPower 2F.
       Four bytes are ANT1..ANT4. Bit7 selects volatile RAM per port.
       Limit writes to the documented 0..30 dBm range. Inventory never calls this. */
    if ((command == 0x94U) && (length == 0U)) {
        valid = 1U;
    }
    if ((command == 0x2FU) && (length == 4U)) {
        valid = 1U;
        for (i = 0U; i < 4U; ++i) {
            if ((data[i] & 0x7FU) > 30U) {
                valid = 0U;
            }
        }
    }
    if ((command == 0x3FU) && (length == 1U) && ((data[0] & 0x0FU) != 0U) &&
        ((data[0] & 0x70U) == 0U)) {
        valid = 1U;
    }
    if ((command == 0x7FU) && (length == 1U)) {
        profile = data[0] & 0x3FU;
        if (data[0] == 0U) {
            valid = 1U;
        } else if (((data[0] & 0x80U) != 0U) &&
                   ((profile <= 3U) || (profile == 5U) || (profile == 7U) || (profile == 11U) ||
                    (profile == 12U) || (profile == 13U) || (profile == 15U) ||
                    ((profile >= 50U) && (profile <= 53U)))) {
            valid = 1U;
        }
    }
    /* UHFReader288Demo GetEx10Version: read-only EC 00. */
    if ((command == 0xECU) && (length == 1U) && (data[0] == 0U)) {
        valid = 1U;
    }
    /* Gen2X SetExtProfile uses 7F + action(0 Get / 1 Set) + ID big endian.
       A bounded list matches the supplied demo; no arbitrary configuration. */
    if ((command == 0x7FU) && (length == 3U)) {
        uint16_t profile_id = ((uint16_t)data[1] << 8U) | data[2];
        if ((data[0] == 0U) && (profile_id == 0U)) {
            valid = 1U;
        }
        if (data[0] == 1U) {
            switch (profile_id) {
            case 1U:
            case 3U:
            case 5U:
            case 7U:
            case 11U:
            case 12U:
            case 13U:
            case 15U:
            case 102U:
            case 103U:
            case 104U:
            case 120U:
            case 123U:
            case 124U:
            case 125U:
            case 126U:
            case 141U:
            case 146U:
            case 147U:
            case 148U:
            case 185U:
            case 202U:
            case 203U:
            case 205U:
            case 222U:
            case 223U:
            case 224U:
            case 225U:
            case 226U:
            case 241U:
            case 244U:
            case 285U:
            case 302U:
            case 323U:
            case 324U:
            case 325U:
            case 326U:
            case 342U:
            case 343U:
            case 344U:
            case 345U:
            case 382U:
            case 4123U:
            case 4124U:
            case 4141U:
            case 4146U:
            case 4148U:
            case 4185U:
            case 5123U:
            case 5124U:
            case 5141U:
            case 5146U:
            case 5148U:
            case 5185U:
                valid = 1U;
                break;
            default:
                break;
            }
        }
    }
    if ((command == 0x91U) && (length == 5U) && (data[4] <= 3U)) {
        frequency = ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) |
                    ((uint32_t)data[2] << 8U) | data[3];
        if ((frequency >= 840000UL) && (frequency <= 960000UL) &&
            (((frequency % 125UL) == 0U) || ((frequency % 100UL) == 0U))) {
            valid = 1U;
        }
    }
    if ((valid == 0U) || (ZK7182_IsReady() == 0U) || (scan_active != 0U) || (pc_pending != 0U)) {
        return 0U;
    }
    frame_length = (uint16_t)length + 5U;
    tx_frame[0] = (uint8_t)(frame_length - 1U);
    tx_frame[1] = ZK7182_READER_ADDRESS;
    tx_frame[2] = command;
    if (length != 0U) {
        (void)memcpy(tx_frame + 3U, data, length);
    }
    crc = Crc16(tx_frame, frame_length - 2U);
    tx_frame[frame_length - 2U] = (uint8_t)crc;
    tx_frame[frame_length - 1U] = (uint8_t)(crc >> 8U);
    if (USBH_CP210X_Transmit(&hUsbHostFS, tx_frame, frame_length) != USBH_OK) {
        return 0U;
    }
    tx_busy = 1U;
    expected_command = command;
    response_armed = 0U;
    pc_pending = 1U;
    pc_done = 0U;
    pc_reply_length = 0U;
    pc_reply_status = 0U;
    command_deadline = HAL_GetTick() + ZK7182_COMMAND_TIMEOUT_MS;
    g_debug.tx_busy = 1U;
    g_debug.reader_expected_command = command;
    g_debug.tx_frame_count++;
    g_debug.tx_bytes_total += frame_length;
    g_debug.last_tx_frame_length = frame_length;
    (void)memcpy((void *)g_debug.last_tx_frame, tx_frame, frame_length);
    return 1U;
}

uint8_t ZK7182_TakePcReply(uint8_t *data, uint8_t *length, uint8_t *status) {
    if ((pc_done == 0U) || (data == NULL) || (length == NULL) || (status == NULL)) {
        return 0U;
    }
    *status = pc_reply_status;
    *length = pc_reply_length;
    (void)memcpy(data, pc_reply, pc_reply_length);
    pc_done = 0U;
    return 1U;
}
