#include "hmi.h"
#include "hmi_hal_stub.h"
#include "debug_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #condition);          \
            exit(EXIT_FAILURE);                                                                    \
        }                                                                                          \
    } while (0)

static uint16_t ReferenceCrc16(const uint8_t *data, uint16_t length) {
    uint16_t crc;
    uint16_t index;
    uint8_t bit;

    crc = 0xFFFFU;
    for (index = 0U; index < length; index++) {
        crc ^= data[index];
        for (bit = 0U; bit < 8U; bit++) {
            if ((crc & 1U) != 0U) {
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            } else {
                crc >>= 1U;
            }
        }
    }
    return crc;
}

static void ExpectTx(uint32_t index, const uint8_t *expected, uint16_t expected_length) {
    CHECK(index < hmi_hal_stub_tx_count);
    CHECK(hmi_hal_stub_tx[index].length == expected_length);
    CHECK(memcmp(hmi_hal_stub_tx[index].data, expected, expected_length) == 0);
}

static uint16_t BuildEvent(uint8_t *frame, uint16_t vp, uint16_t value, uint8_t with_crc) {
    uint16_t crc;

    frame[0] = 0x5AU;
    frame[1] = 0xA5U;
    frame[2] = (with_crc != 0U) ? 0x08U : 0x06U;
    frame[3] = 0x83U;
    frame[4] = (uint8_t)(vp >> 8U);
    frame[5] = (uint8_t)(vp & 0xFFU);
    frame[6] = 0x01U;
    frame[7] = (uint8_t)(value >> 8U);
    frame[8] = (uint8_t)(value & 0xFFU);
    if (with_crc == 0U) {
        return 9U;
    }

    crc = ReferenceCrc16(&frame[3], 6U);
    frame[9] = (uint8_t)(crc & 0xFFU);
    frame[10] = (uint8_t)(crc >> 8U);
    return 11U;
}

static void StartWithAutoUploadEnabled(void) {
    static const uint8_t config_response[] = {0x5AU, 0xA5U, 0x08U, 0x83U, 0x00U, 0x80U,
                                              0x02U, 0x00U, 0x14U, 0x10U, 0x38U};

    HmiHalStub_Reset();
    Hmi_Init();
    CHECK(hmi_hal_stub_rx_active != 0U);
    Hmi_Process();
    HmiHalStub_Inject(config_response, (uint16_t)sizeof(config_response));
    Hmi_Process();
    CHECK(g_hmi_debug.config_known == 1U);
    CHECK(g_hmi_debug.auto_upload_enabled == 1U);
    Hmi_FlushEvents();
    HmiHalStub_ClearTx();
}

static void TestTransmitFrames(void) {
    static const uint8_t word_frame[] = {0x5AU, 0xA5U, 0x05U, 0x82U, 0x12U, 0x34U, 0xABU, 0xCDU};
    static const uint8_t quantity_frame[] = {0x5AU, 0xA5U, 0x05U, 0x82U,
                                             0x20U, 0x00U, 0x00U, 0x19U};
    static const uint8_t page_frame[] = {0x5AU, 0xA5U, 0x07U, 0x82U, 0x00U,
                                         0x84U, 0x5AU, 0x01U, 0x00U, 0x02U};
    static const uint8_t indicator_frame[] = {0x5AU, 0xA5U, 0x0DU, 0x82U, 0x30U, 0x01U,
                                              0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x01U,
                                              0x00U, 0x01U, 0x00U, 0x00U};

    HmiHalStub_Reset();
    Hmi_Init();
    HmiHalStub_ClearTx();

    CHECK(Hmi_SendWord(0x1234U, 0xABCDU) == 1U);
    CHECK(Hmi_SendQuantity(25U) == 1U);
    CHECK(Hmi_SendPage(HMI_PAGE_RESULT) == 1U);
    CHECK(Hmi_SendIndicators(1U, 0U, 1U, 1U, 0U) == 1U);
    CHECK(hmi_hal_stub_tx_count == 4U);
    ExpectTx(0U, word_frame, (uint16_t)sizeof(word_frame));
    ExpectTx(1U, quantity_frame, (uint16_t)sizeof(quantity_frame));
    ExpectTx(2U, page_frame, (uint16_t)sizeof(page_frame));
    ExpectTx(3U, indicator_frame, (uint16_t)sizeof(indicator_frame));

    hmi_hal_stub_tx_status = HAL_ERROR;
    CHECK(Hmi_SendWord(0x1234U, 1U) == 0U);
    CHECK(g_hmi_debug.tx_error_count == 1U);
    CHECK(g_debug.hmi_tx_error_count == 1U);
}

static void TestConfigNegotiation(void) {
    static const uint8_t read_without_crc[] = {0x5AU, 0xA5U, 0x04U, 0x83U, 0x00U, 0x80U, 0x02U};
    static const uint8_t read_with_crc[] = {0x5AU, 0xA5U, 0x06U, 0x83U, 0x00U,
                                            0x80U, 0x02U, 0xC9U, 0xA1U};
    static const uint8_t response_upload_off[] = {0x5AU, 0xA5U, 0x08U, 0x83U, 0x00U, 0x80U,
                                                  0x02U, 0x00U, 0x14U, 0x10U, 0x28U};
    static const uint8_t enable_upload[] = {0x5AU, 0xA5U, 0x07U, 0x82U, 0x00U,
                                            0x80U, 0x5AU, 0x00U, 0x00U, 0x38U};
    static const uint8_t response_upload_on[] = {0x5AU, 0xA5U, 0x08U, 0x83U, 0x00U, 0x80U,
                                                 0x02U, 0x00U, 0x14U, 0x10U, 0x38U};

    HmiHalStub_Reset();
    Hmi_Init();
    Hmi_Process();
    CHECK(hmi_hal_stub_tx_count == 1U);
    ExpectTx(0U, read_without_crc, (uint16_t)sizeof(read_without_crc));

    HmiHalStub_AdvanceTick(250U);
    Hmi_Process();
    CHECK(hmi_hal_stub_tx_count == 2U);
    ExpectTx(1U, read_with_crc, (uint16_t)sizeof(read_with_crc));

    HmiHalStub_Inject(response_upload_off, (uint16_t)sizeof(response_upload_off));
    Hmi_Process();
    CHECK(hmi_hal_stub_tx_count == 3U);
    ExpectTx(2U, enable_upload, (uint16_t)sizeof(enable_upload));
    CHECK(g_hmi_debug.config_read_count == 2U);
    CHECK(g_hmi_debug.config_response_count == 1U);
    CHECK(g_hmi_debug.config_write_count == 1U);
    CHECK(g_hmi_debug.config_known == 1U);
    CHECK(g_hmi_debug.auto_upload_enabled == 0U);
    CHECK(g_hmi_debug.config_write_pending == 1U);
    CHECK(g_hmi_debug.crc_mode == 1U);

    HmiHalStub_AdvanceTick(50U);
    Hmi_Process();
    CHECK(hmi_hal_stub_tx_count == 4U);
    ExpectTx(3U, read_without_crc, (uint16_t)sizeof(read_without_crc));
    HmiHalStub_Inject(response_upload_on, (uint16_t)sizeof(response_upload_on));
    Hmi_Process();
    CHECK(g_hmi_debug.config_read_count == 3U);
    CHECK(g_hmi_debug.config_response_count == 2U);
    CHECK(g_hmi_debug.auto_upload_enabled == 1U);
    CHECK(g_hmi_debug.config_write_pending == 0U);
}

static void TestEventsAndFiltering(void) {
    uint8_t frame[16];
    uint16_t length;
    HmiEvent event;
    static const uint8_t ack[] = {0x5AU, 0xA5U, 0x03U, 0x82U, 0x4FU, 0x4BU};
    static const uint8_t bad_count[] = {0x5AU, 0xA5U, 0x06U, 0x83U, 0x10U,
                                        0x20U, 0x02U, 0x00U, 0x01U};
    static const uint8_t two_words[] = {0x5AU, 0xA5U, 0x08U, 0x83U, 0x10U, 0x20U,
                                        0x02U, 0x00U, 0x01U, 0x00U, 0x02U};
    static const uint8_t noise[] = {0x00U, 0x5AU, 0x00U, 0x5AU, 0xA5U, 0x00U};

    StartWithAutoUploadEnabled();
    length = BuildEvent(frame, HMI_VP_BUTTON_GATE, 1U, 0U);
    HmiHalStub_Inject(frame, length);
    Hmi_Process();
    CHECK(Hmi_PopEvent(&event) == 1U);
    CHECK(event.vp == HMI_VP_BUTTON_GATE);
    CHECK(event.value == 1U);
    CHECK(Hmi_PopEvent(&event) == 0U);

    HmiHalStub_Inject(ack, (uint16_t)sizeof(ack));
    length = BuildEvent(frame, 0x2222U, 1U, 0U);
    HmiHalStub_Inject(frame, length);
    HmiHalStub_Inject(bad_count, (uint16_t)sizeof(bad_count));
    HmiHalStub_Inject(two_words, (uint16_t)sizeof(two_words));
    Hmi_Process();
    CHECK(Hmi_PopEvent(&event) == 0U);
    CHECK(g_hmi_debug.rx_ack_count == 1U);
    CHECK(g_hmi_debug.rx_malformed_frame_count >= 1U);
    CHECK(g_hmi_debug.rx_ignored_frame_count >= 2U);

    HmiHalStub_Inject(noise, (uint16_t)sizeof(noise));
    length = BuildEvent(frame, HMI_VP_BUTTON_SCAN, 1U, 0U);
    HmiHalStub_Inject(frame, length);
    Hmi_Process();
    CHECK(Hmi_PopEvent(&event) == 1U);
    CHECK(event.vp == HMI_VP_BUTTON_SCAN);
}

static void TestTimeoutAndOverflowRecovery(void) {
    uint8_t frame[16];
    uint16_t length;
    uint32_t index;
    HmiEvent event;
    static const uint8_t partial[] = {0x5AU, 0xA5U, 0x06U, 0x83U, 0x10U};

    StartWithAutoUploadEnabled();
    HmiHalStub_Inject(partial, (uint16_t)sizeof(partial));
    Hmi_Process();
    HmiHalStub_AdvanceTick(51U);
    Hmi_Process();
    CHECK(g_hmi_debug.rx_timeout_count == 1U);

    length = BuildEvent(frame, HMI_VP_BUTTON_RESET, 1U, 0U);
    HmiHalStub_Inject(frame, length);
    Hmi_Process();
    CHECK(Hmi_PopEvent(&event) == 1U);
    CHECK(event.vp == HMI_VP_BUTTON_RESET);

    for (index = 0U; index < 140U; index++) {
        HmiHalStub_InjectByte(0x11U);
    }
    CHECK(g_hmi_debug.rx_ring_overflow_count != 0U);
    Hmi_Process();
    length = BuildEvent(frame, HMI_VP_BUTTON_CONFIRM, 1U, 0U);
    HmiHalStub_Inject(frame, length);
    Hmi_Process();
    CHECK(Hmi_PopEvent(&event) == 1U);
    CHECK(event.vp == HMI_VP_BUTTON_CONFIRM);
}

static void TestLateDisplayConfigResponse(void) {
    static const uint8_t response_upload_on[] = {0x5AU, 0xA5U, 0x08U, 0x83U, 0x00U, 0x80U,
                                                 0x02U, 0x00U, 0x14U, 0x10U, 0x38U};
    uint32_t index;
    uint32_t tx_count_after_link;

    HmiHalStub_Reset();
    Hmi_Init();
    Hmi_Process();
    for (index = 1U; index < 16U; index++) {
        HmiHalStub_AdvanceTick(250U);
        Hmi_Process();
    }
    CHECK(hmi_hal_stub_tx_count == 16U);

    HmiHalStub_AdvanceTick(1000U);
    Hmi_Process();
    HmiHalStub_AdvanceTick(1000U);
    Hmi_Process();
    CHECK(HAL_GetTick() > 5000U);
    CHECK(hmi_hal_stub_tx_count == 18U);
    CHECK(hmi_hal_stub_tx[16].data[2] == 0x04U);
    CHECK(hmi_hal_stub_tx[17].data[2] == 0x06U);

    HmiHalStub_Inject(response_upload_on, (uint16_t)sizeof(response_upload_on));
    Hmi_Process();
    CHECK(g_hmi_debug.config_known == 1U);
    CHECK(g_hmi_debug.auto_upload_enabled == 1U);
    tx_count_after_link = hmi_hal_stub_tx_count;

    HmiHalStub_AdvanceTick(5000U);
    Hmi_Process();
    CHECK(hmi_hal_stub_tx_count == tx_count_after_link);
}

static void TestQueueAndEmergencyPriority(void) {
    uint8_t frame[16];
    uint16_t length;
    uint16_t index;
    HmiEvent event;
    static const uint8_t config_response_upload_off[] = {0x5AU, 0xA5U, 0x08U, 0x83U, 0x00U, 0x80U,
                                                         0x02U, 0x00U, 0x14U, 0x10U, 0x28U};

    StartWithAutoUploadEnabled();
    for (index = 0U; index < 20U; index++) {
        length = BuildEvent(frame, HMI_VP_BUTTON_GATE, index, 0U);
        HmiHalStub_Inject(frame, length);
        Hmi_Process();
    }
    CHECK(g_hmi_debug.event_queue_overflow_count == 4U);
    CHECK(g_hmi_debug.event_queue_depth == 16U);
    for (index = 4U; index < 20U; index++) {
        CHECK(Hmi_PopEvent(&event) == 1U);
        CHECK(event.value == index);
    }
    CHECK(Hmi_PopEvent(&event) == 0U);

    length = BuildEvent(frame, HMI_VP_BUTTON_SCAN, 1U, 0U);
    HmiHalStub_Inject(frame, length);
    length = BuildEvent(frame, HMI_VP_BUTTON_GATE, 1U, 0U);
    HmiHalStub_Inject(frame, length);
    length = BuildEvent(frame, HMI_VP_BUTTON_RESET, 1U, 0U);
    HmiHalStub_Inject(frame, length);
    length = BuildEvent(frame, HMI_VP_BUTTON_EMERGENCY, 1U, 0U);
    HmiHalStub_Inject(frame, length);
    length = BuildEvent(frame, HMI_VP_BUTTON_CONVEYOR, 1U, 0U);
    HmiHalStub_Inject(frame, length);
    Hmi_Process();
    CHECK(g_hmi_debug.emergency_pending == 1U);
    CHECK(g_hmi_debug.event_queue_depth == 0U);
    CHECK(Hmi_PopEvent(&event) == 1U);
    CHECK(event.vp == HMI_VP_BUTTON_EMERGENCY);
    CHECK(event.value == 1U);
    CHECK(Hmi_PopEvent(&event) == 0U);

    length = BuildEvent(frame, HMI_VP_BUTTON_GATE, 0U, 0U);
    HmiHalStub_Inject(frame, length);
    Hmi_Process();
    CHECK(Hmi_PopEvent(&event) == 1U);
    Hmi_FlushEvents();
    CHECK(Hmi_PopEvent(&event) == 0U);

    HmiHalStub_Reset();
    Hmi_Init();
    HmiHalStub_Inject(config_response_upload_off, (uint16_t)sizeof(config_response_upload_off));
    length = BuildEvent(frame, HMI_VP_BUTTON_RESET, 1U, 0U);
    HmiHalStub_Inject(frame, length);
    Hmi_Process();
    CHECK(hmi_hal_stub_tx_count == 0U);
    CHECK(g_hmi_debug.reset_pending == 1U);
    CHECK(Hmi_PopEvent(&event) == 1U);
    CHECK(event.vp == HMI_VP_BUTTON_RESET);
}

static void TestFlushReceiveFence(void) {
    uint8_t frame[16];
    uint16_t length;
    HmiEvent event;

    /* A complete normal frame can already be in the ISR ring when a tunnel
     * state transition flushes its old actions.  It must not become fresh when
     * the main-loop parser runs afterward. */
    StartWithAutoUploadEnabled();
    length = BuildEvent(frame, HMI_VP_BUTTON_CONFIRM, 1U, 0U);
    HmiHalStub_Inject(frame, length);
    Hmi_FlushEvents();
    Hmi_Process();
    CHECK(Hmi_PopEvent(&event) == 0U);

    /* The same action received after the fence is fresh. */
    HmiHalStub_Inject(frame, length);
    Hmi_Process();
    CHECK(Hmi_PopEvent(&event) == 1U);
    CHECK(event.vp == HMI_VP_BUTTON_CONFIRM);
    CHECK(event.value == 1U);

    /* Header-before-flush and tail-after-flush is still one old ordinary
     * frame, so it cannot authorize an action in the new state. */
    StartWithAutoUploadEnabled();
    length = BuildEvent(frame, HMI_VP_BUTTON_RESCAN, 1U, 0U);
    HmiHalStub_Inject(frame, 5U);
    Hmi_Process();
    Hmi_FlushEvents();
    HmiHalStub_Inject(&frame[5], (uint16_t)(length - 5U));
    Hmi_Process();
    CHECK(Hmi_PopEvent(&event) == 0U);

    /* A complete EMERGENCY waiting in the ISR ring is never discarded by a
     * receive fence. */
    StartWithAutoUploadEnabled();
    length = BuildEvent(frame, HMI_VP_BUTTON_EMERGENCY, 1U, 0U);
    HmiHalStub_Inject(frame, length);
    Hmi_FlushEvents();
    Hmi_Process();
    CHECK(Hmi_PopEvent(&event) == 1U);
    CHECK(event.vp == HMI_VP_BUTTON_EMERGENCY);
    CHECK(event.value == 1U);

    /* RESET is also preserved when its frame itself straddles the fence. */
    StartWithAutoUploadEnabled();
    length = BuildEvent(frame, HMI_VP_BUTTON_RESET, 1U, 0U);
    HmiHalStub_Inject(frame, 6U);
    Hmi_Process();
    Hmi_FlushEvents();
    HmiHalStub_Inject(&frame[6], (uint16_t)(length - 6U));
    Hmi_Process();
    CHECK(Hmi_PopEvent(&event) == 1U);
    CHECK(event.vp == HMI_VP_BUTTON_RESET);
    CHECK(event.value == 1U);

    /* Flush only clears normal actions; it must not clear an already parsed
     * priority stop before the tunnel consumes it. */
    StartWithAutoUploadEnabled();
    length = BuildEvent(frame, HMI_VP_BUTTON_EMERGENCY, 1U, 0U);
    HmiHalStub_Inject(frame, length);
    Hmi_Process();
    CHECK(g_hmi_debug.emergency_pending == 1U);
    Hmi_FlushEvents();
    CHECK(g_hmi_debug.emergency_pending == 1U);
    CHECK(Hmi_PopEvent(&event) == 1U);
    CHECK(event.vp == HMI_VP_BUTTON_EMERGENCY);
}

static void TestCrcAndErrorRecovery(void) {
    uint8_t frame[20];
    uint8_t expected[12];
    uint16_t length;
    uint16_t crc;
    HmiEvent event;

    StartWithAutoUploadEnabled();
    length = BuildEvent(frame, HMI_VP_BUTTON_AUTO, 1U, 1U);
    HmiHalStub_Inject(frame, length);
    Hmi_Process();
    CHECK(Hmi_PopEvent(&event) == 1U);
    CHECK(event.vp == HMI_VP_BUTTON_AUTO);
    CHECK(g_hmi_debug.crc_mode == 2U);

    HmiHalStub_ClearTx();
    CHECK(Hmi_SendQuantity(0x1234U) == 1U);
    expected[0] = 0x5AU;
    expected[1] = 0xA5U;
    expected[2] = 0x07U;
    expected[3] = 0x82U;
    expected[4] = 0x20U;
    expected[5] = 0x00U;
    expected[6] = 0x12U;
    expected[7] = 0x34U;
    crc = ReferenceCrc16(&expected[3], 5U);
    expected[8] = (uint8_t)(crc & 0xFFU);
    expected[9] = (uint8_t)(crc >> 8U);
    ExpectTx(0U, expected, 10U);

    frame[length - 1U] ^= 0x01U;
    HmiHalStub_Inject(frame, length);
    Hmi_Process();
    CHECK(Hmi_PopEvent(&event) == 0U);

    HmiHalStub_InjectByte(0x5AU);
    Hmi_Process();
    HmiHalStub_InjectError(0x1234U);
    CHECK(g_hmi_debug.uart_error_count == 1U);
    CHECK(g_hmi_debug.last_uart_error == 0x1234U);
    CHECK(g_hmi_debug.rx_armed == 1U);
    Hmi_Process();

    length = BuildEvent(frame, HMI_VP_BUTTON_SEMI, 1U, 0U);
    HmiHalStub_Inject(frame, length);
    Hmi_Process();
    CHECK(Hmi_PopEvent(&event) == 1U);
    CHECK(event.vp == HMI_VP_BUTTON_SEMI);

    Hmi_IRQHandler();
    CHECK(hmi_hal_stub_irq_count == 1U);
}

int main(void) {
    TestTransmitFrames();
    TestConfigNegotiation();
    TestEventsAndFiltering();
    TestLateDisplayConfigResponse();
    TestTimeoutAndOverflowRecovery();
    TestQueueAndEmergencyPriority();
    TestFlushReceiveFence();
    TestCrcAndErrorRecovery();
    (void)printf("hmi transport tests passed\n");
    return EXIT_SUCCESS;
}
