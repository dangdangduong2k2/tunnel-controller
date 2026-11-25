/* Production driver, simulated CP210x. Every transmitted byte is checked:
 * only SDK START 50/Target A and STOP 51 are allowed on every code path. */
#include "zk7182.h"
#include "usb_host.h"
#include "usbh_cp210x.h"
#include "debug_state.h"
#include "app_config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static unsigned allocated_blocks, allocation_calls, fail_allocation;
void *ZK7182_TestAlloc(size_t size) {
    void *p;
    allocation_calls++;
    if (fail_allocation && allocation_calls == fail_allocation)
        return NULL;
    p = malloc(size);
    if (p)
        allocated_blocks++;
    return p;
}
void ZK7182_TestFree(void *p) {
    assert(p && allocated_blocks);
    allocated_blocks--;
    free(p);
}

volatile DebugStateTypeDef g_debug;
USBH_HandleTypeDef hUsbHostFS;
static uint32_t tick, tx_count, recoveries, scan_start;
uint32_t tunnel_test_primask;
#define primask tunnel_test_primask
static uint8_t last_command, transmit_busy;
static uint8_t *rx_buffer;
static uint16_t rx_length;

uint32_t HAL_GetTick(void) {
    return tick;
}
uint32_t __get_PRIMASK(void) {
    return primask;
}
void __disable_irq(void) {
    primask = 1U;
}
void __enable_irq(void) {
    primask = 0U;
}
void USB_HOST_RequestRecovery(void) {
    recoveries++;
}
uint8_t USBH_CP210X_IsReady(USBH_HandleTypeDef *host) {
    (void)host;
    return 1U;
}

static uint16_t Crc(const uint8_t *data, uint16_t length) {
    uint16_t crc = 0xFFFFU, i;
    uint8_t bit;
    for (i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; ++bit)
            crc = (uint16_t)((crc >> 1U) ^ ((crc & 1U) ? 0x8408U : 0U));
    }
    return crc;
}

USBH_StatusTypeDef USBH_CP210X_Transmit(USBH_HandleTypeDef *host, uint8_t *buffer,
                                        uint32_t length) {
    uint16_t crc;
    (void)host;
    assert((buffer[2] == 0x50U) || (buffer[2] == 0x51U));
    assert(length == ((buffer[2] == 0x50U) ? 6U : 5U));
    assert(buffer[0] + 1U == length);
    assert(buffer[1] == 0U);
    if (buffer[2] == 0x50U) {
        assert(buffer[3] == 0U);
    }
    crc = Crc(buffer, (uint16_t)(length - 2U));
    assert(buffer[length - 2U] == (uint8_t)crc);
    assert(buffer[length - 1U] == (uint8_t)(crc >> 8U));
    if (transmit_busy != 0U) {
        return USBH_BUSY;
    }
    last_command = buffer[2];
    tx_count++;
    return USBH_OK;
}
USBH_StatusTypeDef USBH_CP210X_Receive(USBH_HandleTypeDef *host, uint8_t *buffer, uint32_t length) {
    (void)host;
    assert(length == 64U);
    rx_buffer = buffer;
    return USBH_OK;
}
uint16_t USBH_CP210X_GetLastReceivedDataSize(USBH_HandleTypeDef *host) {
    (void)host;
    return rx_length;
}

static void Sent(void) {
    USBH_CP210X_TransmitCallback(&hUsbHostFS);
}
static void Advance(uint32_t ms) {
    tick += ms;
    ZK7182_Process();
}
static void Feed(const uint8_t *bytes, uint16_t length) {
    uint16_t chunk;
    while (length != 0U) {
        if (rx_buffer == NULL) {
            ZK7182_Process();
        }
        assert(rx_buffer != NULL);
        chunk = (length > 64U) ? 64U : length;
        memcpy(rx_buffer, bytes, chunk);
        rx_buffer = NULL;
        rx_length = chunk;
        USBH_CP210X_ReceiveCallback(&hUsbHostFS);
        bytes += chunk;
        length -= chunk;
    }
}
static uint16_t Frame(uint8_t *frame, uint8_t address, uint8_t command, uint8_t status,
                      const uint8_t *data, uint8_t length) {
    uint16_t crc;
    frame[0] = (uint8_t)(length + 5U);
    frame[1] = address;
    frame[2] = command;
    frame[3] = status;
    if (length != 0U) {
        memcpy(frame + 4U, data, length);
    }
    crc = Crc(frame, (uint16_t)(length + 4U));
    frame[length + 4U] = (uint8_t)crc;
    frame[length + 5U] = (uint8_t)(crc >> 8U);
    return (uint16_t)(length + 6U);
}
static void Reply(uint8_t command, uint8_t status) {
    uint8_t frame[6];
    Feed(frame, Frame(frame, 0U, command, status, NULL, 0U));
}
static uint16_t TagFrame(uint8_t *frame, uint16_t id, uint8_t antenna, uint8_t size) {
    uint8_t payload[67];
    memset(payload, 0xAB, sizeof(payload));
    payload[0] = antenna;
    payload[1] = size;
    payload[2] = (uint8_t)(id >> 8U);
    payload[3] = (uint8_t)id;
    payload[size + 2U] = 55U;
    return Frame(frame, 0U, 0xEEU, 0U, payload, (uint8_t)(size + 3U));
}
static void Tag(uint16_t id, uint8_t antenna, uint8_t size) {
    uint8_t frame[73];
    Feed(frame, TagFrame(frame, id, antenna, size));
}
static void BootAt(uint32_t time) {
    DebugStateTypeDef empty = {0};
    g_debug = empty;
    tick = time;
    primask = 0U;
    tx_count = 0U;
    recoveries = 0U;
    rx_buffer = NULL;
    transmit_busy = 0U;
    last_command = 0U;
    ZK7182_Init();
    ZK7182_TransportReady();
    ZK7182_Process();
    assert(ZK7182_IsReady() == 0U);
    assert(ZK7182_StartScan(30000U) == 0U);
    Advance(ZK7182_BOOT_DELAY_MS);
    assert(ZK7182_IsReady() == 1U);
    Advance(5000U);
    assert(tx_count == 0U); /* Boot and idle do not query or set anything. */
    assert(g_debug.reader_start_only == 1U);
    assert(g_debug.rf_profile_verified == 0U);
}
static void Start(void) {
    uint32_t before = tx_count;
    scan_start = tick;
    assert(ZK7182_StartScan(60000U) == 1U);
    assert(g_debug.scan_duration_ms == 30000U);
    ZK7182_Process();
    assert(tx_count == before + 1U && last_command == 0x50U);
    assert(ZK7182_IsInventoryResponsePending() == 1U);
    Sent();
    Reply(0x50U, 0U);
}
static void Stop(void) {
    uint32_t before = tx_count;
    ZK7182_PauseScan();
    ZK7182_Process();
    assert(tx_count == before + 1U && last_command == 0x51U);
    Sent();
    Reply(0x51U, 0U);
    assert(ZK7182_IsInventoryResponsePending() == 0U);
}
static void TestUnder100AndLongEpc(void) {
    uint16_t i;
    uint8_t antenna;
    uint8_t combined[63];
    BootAt(0U);
    Start();
    for (antenna = 1U; antenna <= 4U; antenna <<= 1U)
        for (i = 1U; i <= 99U; ++i) {
            Tag(i, antenna, 12U);
        }
    assert(ZK7182_GetUniqueEpcCount() == 99U);
    assert(ZK7182_GetTotalReadCount() == 297U);
    assert(g_debug.scan_antenna_mask == 7U);
    Tag(100U, 8U, 64U); /* Fragmented across 64 byte USB receives, antenna 4. */
    Tag(100U, 8U, 64U);
    assert(ZK7182_GetUniqueEpcCount() == 100U);
    assert(g_debug.last_epc_length == 64U);
    for (i = 0U; i < 3U; ++i) {
        assert(TagFrame(combined + i * 21U, (uint16_t)(101U + i), 5U, 12U) == 21U);
    }
    Feed(combined, sizeof(combined));
    assert(ZK7182_GetUniqueEpcCount() == 103U);
    assert(tx_count == 1U); /* Continuous stream needs no per-antenna commands. */
    Stop();
    ZK7182_CancelScan();
    Tag(200U, 1U, 12U);
    assert(ZK7182_GetUniqueEpcCount() == 103U);
    Advance(ZK7182_STOP_QUIET_MS);
    assert(ZK7182_IsReady() == 1U);
}
static void TestDeadline(uint32_t time) {
    uint8_t i;
    BootAt(time);
    Start();
    for (i = 0U; i < 7U; ++i) {
        Advance(4000U);
        Tag(1U, 8U, 12U);
    }
    assert(tx_count == 1U); /* A reporting reader is not restarted. */
    tick = scan_start + 29199U;
    ZK7182_Process();
    assert(tx_count == 1U);
    Advance(1U);
    assert(last_command == 0x51U && tx_count == 2U);
    Sent();
    Tag(1U, 8U, 12U);
    Reply(0x51U, 0U);
    assert(ZK7182_IsScanComplete() == 0U);
    assert(ZK7182_IsScanActive() == 1U);
    ZK7182_ResumeScan();
    Advance(799U);
    assert(tx_count == 2U);
    assert(ZK7182_IsScanActive() == 1U);
    Advance(1U);
    assert(ZK7182_IsScanComplete() == 1U);
    assert(ZK7182_GetScanResult() == ZK7182_SCAN_RESULT_OK);
    Tag(2U, 8U, 12U);
    assert(ZK7182_GetUniqueEpcCount() == 1U);
}
static void TestPauseResumeAndCancel(void) {
    BootAt(0U);
    Start();
    Tag(1U, 1U, 12U);
    Advance(1000U);
    Stop();
    assert(ZK7182_IsScanActive() == 1U);
    ZK7182_ResumeScan();
    ZK7182_Process();
    assert(tx_count == 2U);
    Advance(ZK7182_STOP_QUIET_MS);
    assert(last_command == 0x50U && tx_count == 3U);
    Sent();
    Reply(0x50U, 0U);
    Tag(1U, 2U, 12U);
    Tag(2U, 4U, 12U);
    assert(ZK7182_GetUniqueEpcCount() == 2U);
    tick = scan_start + 29200U;
    ZK7182_Process();
    assert(last_command == 0x51U && tx_count == 4U); /* Original deadline. */
    Sent();
    Reply(0x51U, 0U);
    ZK7182_CancelScanAtUniqueCount(1U);
    assert(ZK7182_GetUniqueEpcCount() == 1U);
    Advance(ZK7182_STOP_QUIET_MS);
    assert(ZK7182_IsReady() == 1U);
    assert(ZK7182_StartScan(0U) == 1U);
    ZK7182_Process();
    ZK7182_CancelScan(); /* START in flight: cancellation must still STOP. */
    assert(ZK7182_StartScan(0U) == 0U);
    Sent();
    Reply(0x50U, 0U);
    Tag(9U, 1U, 12U);
    assert(ZK7182_GetUniqueEpcCount() == 0U);
    ZK7182_Process();
    assert(last_command == 0x51U);
    Sent();
    Reply(0x51U, 0U);
    Advance(ZK7182_STOP_QUIET_MS);
    assert(ZK7182_IsReady() == 1U);
}
static void TestFailureAndReconnect(void) {
    uint32_t before;
    BootAt(0U);
    assert(ZK7182_StartScan(0U) == 1U);
    scan_start = tick;
    transmit_busy = 1U;
    ZK7182_Process();
    assert(tx_count == 0U);
    transmit_busy = 0U;
    ZK7182_Process();
    assert(last_command == 0x50U);
    Tag(1U, 1U, 12U);
    assert(ZK7182_GetUniqueEpcCount() == 0U); /* No TX completion. */
    Sent();
    Advance(ZK7182_COMMAND_TIMEOUT_MS);
    assert(ZK7182_IsScanActive() == 1U && ZK7182_IsScanComplete() == 0U);
    assert(last_command == 0x51U);
    Sent();
    Reply(0x51U, 0U);
    Advance(ZK7182_STOP_QUIET_MS);
    assert(last_command == 0x50U); /* Retry same scan, no reset of deadline. */
    Sent();
    Tag(3U, 4U, 12U); /* EPC itself confirms the lost Start ACK. */
    assert(g_debug.inventory_start_data_confirm_count == 1U);
    assert(ZK7182_GetUniqueEpcCount() == 1U);
    USBH_CP210X_ErrorCallback(&hUsbHostFS);
    assert(recoveries == 1U);
    assert(ZK7182_IsScanActive() == 1U);
    before = tx_count;
    ZK7182_TransportReady();
    Advance(ZK7182_BOOT_DELAY_MS);
    assert(tx_count == before + 1U && last_command == 0x51U);
    assert(ZK7182_IsReady() == 0U);
    Sent();
    Reply(0x51U, 0U);
    Advance(ZK7182_STOP_QUIET_MS);
    assert(last_command == 0x50U);
    Sent();
    Reply(0x50U, 0U);
    Tag(3U, 2U, 12U);
    Tag(4U, 1U, 12U);
    assert(ZK7182_GetUniqueEpcCount() == 2U);
    ZK7182_PauseScan();
    ZK7182_Process();
    Sent();
    Reply(0x51U, 0xF0U);
    assert(ZK7182_IsScanActive() == 1U);
    Advance(ZK7182_COMMAND_TIMEOUT_MS);
    Sent();
    Advance(ZK7182_COMMAND_TIMEOUT_MS);
    Sent();
    before = tx_count;
    Advance(ZK7182_COMMAND_TIMEOUT_MS);
    assert(recoveries == 2U && tx_count == before);
    tick = scan_start + 30000U;
    ZK7182_Process();
    assert(ZK7182_IsScanComplete() == 1U && ZK7182_IsScanActive() == 0U);
}
static void TestRejectAndStalledUsb(void) {
    uint8_t i;
    uint32_t before;
    BootAt(0U);
    scan_start = tick;
    assert(ZK7182_StartScan(0U) == 1U);
    ZK7182_Process();
    for (i = 0U; i < ZK7182_START_MAX_FAILURES; ++i) {
        assert(last_command == 0x50U);
        Sent();
        Reply(0x50U, 0xF0U);
        assert(ZK7182_IsScanActive() == 1U);
        ZK7182_Process();
        assert(last_command == 0x51U);
        Sent();
        Reply(0x51U, 0U);
        Advance(ZK7182_STOP_QUIET_MS);
    }
    before = tx_count;
    Advance(10000U);
    assert(tx_count == before);
    assert(ZK7182_IsScanActive() == 1U); /* No early mechanical abort. */
    tick = scan_start + 30000U;
    ZK7182_Process();
    assert(ZK7182_GetScanResult() == ZK7182_SCAN_RESULT_READER_ERROR);
    BootAt(0U);
    assert(ZK7182_StartScan(0U) == 1U);
    ZK7182_Process();
    Reply(0x50U, 0U); /* Must not accept before USB completion. */
    assert(g_debug.inventory_start_ack_count == 0U);
    Advance(ZK7182_COMMAND_TIMEOUT_MS);
    Advance(1000U);
    assert(tx_count == 1U && recoveries == 1U);
    assert(g_debug.inventory_timeout_count == 1U);
    assert(ZK7182_IsScanActive() == 1U);
}
static void TestLostAckAndSilence(void) {
    uint8_t i;
    uint32_t before;
    BootAt(0U);
    scan_start = tick;
    assert(ZK7182_StartScan(0U) == 1U);
    ZK7182_Process();
    Sent();
    Tag(1U, 1U, 12U);
    assert(ZK7182_GetUniqueEpcCount() == 1U);
    assert(g_debug.inventory_start_ack_count == 0U);
    assert(g_debug.inventory_start_data_confirm_count == 1U);
    Advance(ZK7182_COMMAND_TIMEOUT_MS);
    assert(tx_count == 1U && ZK7182_IsScanComplete() == 0U);
    for (i = 0U; i < ZK7182_SILENCE_MAX_RESTARTS; ++i) {
        Advance(ZK7182_SILENCE_RESTART_MS);
        assert(last_command == 0x51U);
        Sent();
        Reply(0x51U, 0U);
        Advance(ZK7182_STOP_QUIET_MS);
        assert(last_command == 0x50U);
        Sent();
        Reply(0x50U, 0U);
    }
    before = tx_count;
    Advance(ZK7182_SILENCE_RESTART_MS);
    assert(tx_count == before);
    assert(g_debug.inventory_silence_restart_count == ZK7182_SILENCE_MAX_RESTARTS);
    assert(ZK7182_GetUniqueEpcCount() == 1U);
    Stop();
    ZK7182_CancelScan();
    Advance(ZK7182_STOP_QUIET_MS);
    before = tx_count;
    for (i = 0U; i < 5U; ++i) {
        Reply(0xEEU, 0x28U);
        Advance(100U);
    }
    assert(ZK7182_IsReady() == 1U && tx_count == before);
}
static void TestParserAndCutoff(void) {
    uint8_t frame[73], garbage[6] = {0xFFU, 0U, 0xEEU, 0U, 0U, 0U};
    uint16_t length;
    BootAt(0U);
    Start();
    length = TagFrame(frame, 1U, 1U, 12U);
    frame[length - 1U] ^= 1U;
    Feed(frame, length);
    assert(ZK7182_GetUniqueEpcCount() == 0U);
    assert(g_debug.checksum_error_count != 0U);
    Advance(ZK7182_COMMAND_TIMEOUT_MS);
    length = TagFrame(frame, 2U, 1U, 12U);
    Feed(frame, 3U);
    Feed(frame + 3U, (uint16_t)(length - 3U));
    assert(ZK7182_GetUniqueEpcCount() == 1U);
    Feed(frame, Frame(frame, 1U, 0xEEU, 0U, NULL, 0U)); /* Wrong reader address. */
    Feed(frame, Frame(frame, 0U, 0x7FU, 0U, NULL, 0U)); /* Unsolicited config ACK. */
    assert(tx_count == 1U && ZK7182_GetUniqueEpcCount() == 1U);
    Feed(garbage, sizeof(garbage));
    Advance(ZK7182_COMMAND_TIMEOUT_MS);
    Tag(3U, 5U, 12U);
    assert(ZK7182_GetUniqueEpcCount() == 2U);
    /* Callback at deadline, without a preceding main Process, must freeze EPCs. */
    ZK7182_Process();
    tick = scan_start + 30000U;
    Tag(4U, 1U, 12U);
    assert(ZK7182_IsScanComplete() == 1U);
    assert(ZK7182_GetUniqueEpcCount() == 2U);
    assert(ZK7182_GetScanResult() == ZK7182_SCAN_RESULT_PROTOCOL_ERROR);
    ZK7182_Process();
    assert(last_command == 0x51U);
}
static void TestEmptyOverflowAndUnsolicited(void) {
    uint16_t i;
    BootAt(0U);
    Start();
    Reply(0xEEU, 0x28U); /* Heartbeat is not an EPC. */
    tick = scan_start + 29200U;
    ZK7182_Process();
    Sent();
    Reply(0x51U, 0U);
    Advance(800U);
    assert(ZK7182_GetScanResult() == ZK7182_SCAN_RESULT_OK);
    assert(ZK7182_GetUniqueEpcCount() == 0U);
    BootAt(0U);
    Start();
    for (i = 0U; i <= ZK7182_MAX_UNIQUE_TAGS; ++i) {
        Tag(i, 1U, ZK7182_MAX_EPC_BYTES);
    }
    assert(ZK7182_GetUniqueEpcCount() == ZK7182_MAX_UNIQUE_TAGS);
    assert(ZK7182_GetScanResult() == ZK7182_SCAN_RESULT_CAPACITY_OVERFLOW);
    ZK7182_Process();
    assert(last_command == 0x51U);
    BootAt(0U);
    Tag(1U, 1U, 12U);
    ZK7182_Process();
    assert(tx_count == 1U && last_command == 0x51U);
    assert(ZK7182_GetUniqueEpcCount() == 0U);
    Sent();
    Reply(0x51U, 0U);
    Advance(ZK7182_STOP_QUIET_MS);
    assert(ZK7182_IsReady() == 1U);
}
static void TestScanMemoryLifecycle(void) {
    unsigned chunks, failure, before;
    BootAt(0U);
    assert(allocated_blocks == 0U);
    Start();
    chunks = allocated_blocks;
    assert(chunks > 0U);
    Tag(71U, 1U, 64U);
    ZK7182_PauseScan();
    ZK7182_Process();
    assert(allocated_blocks == chunks); /* A pause must retain dedup state. */
    Sent();
    Reply(0x51U, 0U);
    Advance(ZK7182_STOP_QUIET_MS);
    ZK7182_ResumeScan();
    ZK7182_Process();
    Sent();
    Reply(0x50U, 0U);
    Tag(71U, 1U, 64U);
    assert(ZK7182_GetUniqueEpcCount() == 1U);
    assert(ZK7182_GetTotalReadCount() == 2U);
    Stop();
    ZK7182_CancelScan();
    Advance(ZK7182_STOP_QUIET_MS);
    assert(allocated_blocks == 0U);
    assert(ZK7182_GetUniqueEpcCount() == 1U);
    assert(ZK7182_GetTotalReadCount() == 2U);
    before = tx_count;
    for (failure = 1U; failure <= chunks; ++failure) {
        allocation_calls = 0U;
        fail_allocation = failure;
        assert(ZK7182_StartScan(30000U) == 0U);
        ZK7182_Process();
        assert(allocated_blocks == 0U); /* Partial reservations are unwound. */
        assert(tx_count == before && ZK7182_IsScanActive() == 0U);
        assert(ZK7182_GetUniqueEpcCount() == 1U);
        assert(ZK7182_GetTotalReadCount() == 2U);
    }
    fail_allocation = 0U;
    Start();
    Tag(71U, 1U, 64U); /* Next scan counts it again. */
    assert(ZK7182_GetUniqueEpcCount() == 1U);
    tick = scan_start + 30000U;
    ZK7182_Process();
    assert(allocated_blocks == 0U && ZK7182_GetUniqueEpcCount() == 1U);
    Tag(72U, 1U, 64U); /* Late report after release must be ignored. */
    assert(allocated_blocks == 0U && ZK7182_GetUniqueEpcCount() == 1U);
    BootAt(0U);
    Start();
    ZK7182_TransportLost();
    tick = scan_start + 30000U;
    ZK7182_Process();
    assert(allocated_blocks == 0U); /* Cleanup even without USB. */
}
int main(void) {
    TestUnder100AndLongEpc();
    TestDeadline(0U);
    TestDeadline(0xFFFFB000U);
    TestPauseResumeAndCancel();
    TestFailureAndReconnect();
    TestRejectAndStalledUsb();
    TestLostAckAndSilence();
    TestParserAndCutoff();
    TestEmptyOverflowAndUnsolicited();
    TestScanMemoryLifecycle();
    puts("PASS reader START-only: no config TX; 99 tags/297 reads; long/fragmented EPC; "
         "pause/resume; 30 s/wrap; cancel/reconnect; lost ACK/data confirmation; bounded retries; "
         "overflow; idle memory release; allocation failure at every chunk");
    return 0;
}
