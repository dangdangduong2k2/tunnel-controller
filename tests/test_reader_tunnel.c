/* Integration regression: real Tunnel + real reader, simulated GPIO/motors,
 * HMI and USB only. Reproduce USB/ACK failures at the stationary midpoint. */
#include "app_config.h"
#include "board.h"
#include "debug_state.h"
#include "tunnel.h"
#include "zk7182.h"
#include "tunnel_test_stubs.h"
#include "usb_host.h"
#include "usbh_cp210x.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifdef PC_CONTROL_TEST
#include "pc_control.h"
static void PcUsbTransmit(const uint8_t *bytes, uint32_t len);
static void PcUsbReply(uint8_t command);
#endif
USBH_HandleTypeDef hUsbHostFS;
static uint8_t connected, rf_on, pending, report_tags, drop_start, drop_stop;
static uint8_t *rx_buffer;
static uint16_t rx_length;
static uint32_t due_tick, tag_tick, reconnect_tick, starts, stops, first_start_tick;
static const char *scenario;

static uint16_t Crc(const uint8_t *data, uint16_t len) {
    uint16_t crc = 0xFFFFU, i;
    uint8_t bit;
    for (i = 0U; i < len; ++i) {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; ++bit)
            crc = (uint16_t)((crc >> 1U) ^ ((crc & 1U) ? 0x8408U : 0U));
    }
    return crc;
}
static void Feed(uint8_t *frame, uint16_t length) {
    uint16_t crc = Crc(frame, (uint16_t)(length - 2U));
    frame[length - 2U] = (uint8_t)crc;
    frame[length - 1U] = (uint8_t)(crc >> 8U);
    if (rx_buffer == NULL) {
        ZK7182_Process();
    }
    assert(rx_buffer != NULL && length <= 64U);
    memcpy(rx_buffer, frame, length);
    rx_buffer = NULL;
    rx_length = length;
    USBH_CP210X_ReceiveCallback(&hUsbHostFS);
}
static void Ack(uint8_t command) {
    uint8_t data[6] = {5U, 0U, 0U, 0U, 0U, 0U};
    data[2] = command;
    Feed(data, sizeof(data));
}
static void Tag(void) {
    uint8_t data[21] = {20U, 0U, 0xEEU, 0U, 5U, 12U, 0xE2U, 0U,  1U, 2U, 3U,
                        4U,  5U, 6U,    7U, 8U, 9U,  10U,   55U, 0U, 0U};
    Feed(data, sizeof(data));
}
void USB_HOST_RequestRecovery(void) {
    connected = 0U;
    pending = 0U;
    rx_buffer = NULL;
    reconnect_tick = tunnel_test_tick + 300U;
    ZK7182_TransportLost();
}
uint8_t USBH_CP210X_IsReady(USBH_HandleTypeDef *host) {
    (void)host;
    return connected;
}
USBH_StatusTypeDef USBH_CP210X_Transmit(USBH_HandleTypeDef *host, uint8_t *bytes, uint32_t len) {
    uint16_t crc;
    (void)host;
#ifdef PC_CONTROL_TEST
    if (bytes[2] != 0x50U && bytes[2] != 0x51U) {
        PcUsbTransmit(bytes, len);
        assert(connected != 0U && pending == 0U);
        pending = bytes[2];
        due_tick = tunnel_test_tick + 10U;
        return USBH_OK;
    }
#endif
    assert(bytes[2] == 0x50U || bytes[2] == 0x51U);
    assert(len == (bytes[2] == 0x50U ? 6U : 5U));
    assert(bytes[0] + 1U == len && bytes[1] == 0U);
    if (bytes[2] == 0x50U) {
        assert(bytes[3] == 0U);
    }
    crc = Crc(bytes, (uint16_t)(len - 2U));
    assert(bytes[len - 2U] == (uint8_t)crc && bytes[len - 1U] == (uint8_t)(crc >> 8U));
    if (connected == 0U || pending != 0U) {
        return USBH_BUSY;
    }
    pending = bytes[2];
    due_tick = tunnel_test_tick + 10U;
    if (pending == 0x50U) {
        if (starts == 0U) {
            first_start_tick = tunnel_test_tick;
        }
        starts++;
    } else {
        stops++;
    }
    return USBH_OK;
}
USBH_StatusTypeDef USBH_CP210X_Receive(USBH_HandleTypeDef *host, uint8_t *buffer, uint32_t len) {
    (void)host;
    assert(len == 64U);
    rx_buffer = buffer;
    return USBH_OK;
}
uint16_t USBH_CP210X_GetLastReceivedDataSize(USBH_HandleTypeDef *host) {
    (void)host;
    return rx_length;
}

static void ServiceUsb(void) {
    uint8_t command;
    if (connected == 0U) {
        if (reconnect_tick != 0U && (int32_t)(tunnel_test_tick - reconnect_tick) >= 0) {
            reconnect_tick = 0U;
            connected = 1U;
            ZK7182_TransportReady();
        }
        return;
    }
    if (pending != 0U && (int32_t)(tunnel_test_tick - due_tick) >= 0) {
        command = pending;
        pending = 0U;
        USBH_CP210X_TransmitCallback(&hUsbHostFS);
#ifdef PC_CONTROL_TEST
        if (command != 0x50U && command != 0x51U) {
            PcUsbReply(command);
        } else
#endif
            if (command == 0x50U) {
            rf_on = 1U;
            if (drop_start == 0U) {
                Ack(command);
            }
        } else {
            rf_on = 0U;
            if (drop_stop == 0U) {
                Ack(command);
            }
        }
    }
    if (rf_on != 0U && report_tags != 0U && tunnel_test_tick - tag_tick >= 50U) {
        tag_tick = tunnel_test_tick;
        Tag();
    }
}
static void Run(uint32_t ms) {
    while (ms-- != 0U) {
        TunnelTest_AdvanceTick(1U);
#ifdef PC_CONTROL_TEST
        PcControl_Process();
#endif
        Tunnel_Process();
        ServiceUsb();
        ZK7182_Process();
    }
}
static void Wait(TunnelState state, uint32_t ms) {
    while (Tunnel_GetState() != state && ms-- != 0U) {
        Run(1U);
    }
    if (Tunnel_GetState() != state) {
        fprintf(stderr, "%s: expected state %u, actual %u, fault %u, tick %lu, scan %u, cmd %02X\n",
                scenario, state, Tunnel_GetState(), Tunnel_GetFault(),
                (unsigned long)tunnel_test_tick, ZK7182_IsScanActive(),
                g_debug.reader_expected_command);
        assert(0);
    }
}
static void Sensor(uint16_t pin, uint8_t value) {
    TunnelTest_SetPairedSensor(pin, value, 1U);
    Run((pin == SENSOR_SS1_PIN ? SENSOR_SS1_DEBOUNCE_MS : SENSOR_DEBOUNCE_MS) + 10U);
}
static void Boot(uint8_t semi) {
    TunnelTest_ResetStubs();
    ZK7182_Init();
    Tunnel_Init();
    connected = 1U;
    rf_on = 0U;
    pending = 0U;
    report_tags = 1U;
    drop_start = 0U;
    drop_stop = 0U;
    rx_buffer = NULL;
    reconnect_tick = 0U;
    starts = 0U;
    stops = 0U;
    tag_tick = 0U;
    ZK7182_TransportReady();
    Wait(TUNNEL_WAIT_SS1_LOW, 1500U);
    if (semi != 0U) {
        TunnelTest_QueueEvent(0x1060U, 1U);
        Run(10U);
    }
}
static void CloseGate(void) {
    Sensor(SENSOR_SS1_PIN, 1U);
    Wait(TUNNEL_CONVEYOR_TO_SS1_HIGH, 100U);
    Sensor(SENSOR_SS1_PIN, 0U);
    Wait(TUNNEL_CLOSE_STEP_TO_SS2, 100U);
    assert(tunnel_test_motors[STEP_DRIVER].direction == STEP_CLOSE_DIRECTION);
    Sensor(SENSOR_SS2_PIN, 1U);
}
static void ToCenter(void) {
    CloseGate();
    Wait(TUNNEL_CONVEYOR_INVENTORY_TO_SS3, 100U);
    Run(400U);
    assert(rf_on != 0U);
    assert(tunnel_test_motors[CONVEYOR_DRIVER].direction == CONVEYOR_DIRECTION);
}
static void Finish(uint8_t semi, uint16_t count) {
    assert(tunnel_test_motors[CONVEYOR_DRIVER].running == 1U);
    assert(tunnel_test_motors[CONVEYOR_DRIVER].direction == CONVEYOR_DIRECTION);
    Run(300U);
    TunnelTest_SetPairedSensor(SENSOR_SS3_PIN, 1U, 1U);
    assert(tunnel_test_motors[CONVEYOR_DRIVER].running == 0U);
    Wait(semi != 0U ? TUNNEL_WAIT_CONFIRM : TUNNEL_OPEN_STEP, 6500U);
    assert(tunnel_test_last_quantity == count);
    if (semi != 0U) {
        assert(tunnel_test_motors[STEP_DRIVER].running == 0U);
        TunnelTest_QueueEvent(0x1070U, 1U);
        Wait(TUNNEL_OPEN_STEP, 100U);
    }
    assert(tunnel_test_motors[STEP_DRIVER].direction == STEP_OPEN_DIRECTION);
    assert(tunnel_test_motors[STEP_DRIVER].pulse_target == 5100U);
    TunnelTest_SetPairedSensor(SENSOR_SS2_PIN, 0U, 1U);
    TunnelTest_CompleteGatePair();
    Wait(TUNNEL_WAIT_REARM, 100U);
}
static void TestDisconnectAtCenter(void) {
    scenario = "USB failure during forward scan";
    Boot(0U);
    ToCenter();
    assert(ZK7182_GetUniqueEpcCount() == 1U);
    USBH_CP210X_ErrorCallback(&hUsbHostFS);
    Run(10U);
    Wait(TUNNEL_CONVEYOR_INVENTORY_TO_SS3, 10U);
    assert(ZK7182_IsScanActive() == 1U);
    Finish(0U, 1U);
}
static void TestMissingAck(uint8_t tags, uint8_t semi) {
    scenario = "ACK or tag data required before conveyor";
    Boot(semi);
    drop_start = 1U;
    report_tags = tags;
    if (tags != 0U) {
        ToCenter();
        Finish(semi, tags);
        assert(g_debug.inventory_start_data_confirm_count != 0U);
    } else {
        CloseGate();
        Wait(TUNNEL_WAIT_INVENTORY_START, 100U);
        assert(tunnel_test_motors[CONVEYOR_DRIVER].running == 0U);
        Wait(TUNNEL_FAULT, 10200U);
        assert(tunnel_test_motors[CONVEYOR_DRIVER].running == 0U);
        assert(tunnel_test_motors[STEP_DRIVER].running == 0U);
    }
}
static void TestPermanentLossDeadline(void) {
    scenario = "missing SS3 stops without opening";
    Boot(0U);
    ToCenter();
    USBH_CP210X_ErrorCallback(&hUsbHostFS);
    reconnect_tick = 0U;
    Wait(TUNNEL_FAULT, 30500U);
    assert(tunnel_test_motors[CONVEYOR_DRIVER].running == 0U);
    assert(tunnel_test_motors[STEP_DRIVER].running == 0U);
    assert(Tunnel_GetFault() == TUNNEL_FAULT_CONVEYOR_TIMEOUT);
}
static void TestNeverReady(void) {
    scenario = "reader unavailable before scan cannot hold closed gate forever";
    Boot(0U);
    connected = 0U;
    ZK7182_TransportLost();
    CloseGate();
    Wait(TUNNEL_WAIT_READER, 100U);
    Wait(TUNNEL_OPEN_STEP, 10200U);
    assert(starts == 0U && tunnel_test_last_quantity == 0U);
    assert(Tunnel_GetFault() == TUNNEL_FAULT_READER_NOT_READY);
}
static void TestMissingStopAck(void) {
    scenario = "STOP ACK lost cannot block gate indefinitely";
    Boot(0U);
    ToCenter();
    drop_stop = 1U;
    Finish(0U, 1U);
    assert(g_debug.inventory_timeout_count != 0U);
}
static void TestInterlockStillWins(void) {
    scenario = "SS2 lost while recovering reader";
    Boot(0U);
    ToCenter();
    USBH_CP210X_ErrorCallback(&hUsbHostFS);
    Sensor(SENSOR_SS2_PIN, 0U);
    Wait(TUNNEL_FAULT, 100U);
    assert(tunnel_test_motors[STEP_DRIVER].running == 0U);
    assert(tunnel_test_motors[CONVEYOR_DRIVER].running == 0U);
}
static void TestIn7LossDuringInventory(uint8_t semi) {
    scenario = "IN7 loss during AUTO/SEMI scan still finishes at IN3";
    Boot(semi);
    ToCenter();
    TunnelTest_SetSensor(SENSOR_SS7_PIN, 0U, 1U);
    Run(100U);
    assert(Tunnel_GetState() == TUNNEL_CONVEYOR_INVENTORY_TO_SS3);
    assert(Tunnel_GetFault() == TUNNEL_FAULT_NONE);
    assert(rf_on != 0U && ZK7182_IsInventoryRunning() != 0U);
    assert(tunnel_test_motors[CONVEYOR_DRIVER].running != 0U);
    assert(tunnel_test_motors[STEP_DRIVER].running == 0U);
    assert(tunnel_test_motors[STEP2_DRIVER].running == 0U);
    Finish(semi, 1U);
}
static void TestIn7LossBeforeReaderAck(void) {
    scenario = "confirmed IN7 survives dropout before reader ACK; ACK still required";
    Boot(0U);
    drop_start = 1U;
    report_tags = 0U;
    CloseGate();
    Wait(TUNNEL_WAIT_INVENTORY_START, 100U);
    TunnelTest_SetSensor(SENSOR_SS7_PIN, 0U, 1U);
    Run(100U);
    assert(Tunnel_GetState() == TUNNEL_WAIT_INVENTORY_START);
    assert(Tunnel_GetFault() == TUNNEL_FAULT_NONE);
    assert(tunnel_test_motors[CONVEYOR_DRIVER].running == 0U);
    Ack(0x50U);
    Wait(TUNNEL_CONVEYOR_INVENTORY_TO_SS3, 100U);
    assert(tunnel_test_motors[CONVEYOR_DRIVER].running != 0U);
    Finish(0U, 0U);

    Boot(0U);
    drop_start = 1U;
    report_tags = 0U;
    CloseGate();
    Wait(TUNNEL_WAIT_INVENTORY_START, 100U);
    TunnelTest_SetSensor(SENSOR_SS7_PIN, 0U, 1U);
    Wait(TUNNEL_FAULT, 10200U);
    assert(Tunnel_GetFault() == TUNNEL_FAULT_SCAN_ERROR);
    assert(tunnel_test_motors[CONVEYOR_DRIVER].running == 0U);
}
static void TestIn7LossDuringRescan(void) {
    scenario = "IN7 loss before SEMI result persists through Rescan and finishes at IN3";
    Boot(1U);
    ToCenter();
    TunnelTest_SetSensor(SENSOR_SS7_PIN, 0U, 1U);
    Run(100U);
    Sensor(SENSOR_SS3_PIN, 1U);
    Wait(TUNNEL_WAIT_CONFIRM, 6500U);
    TunnelTest_QueueEvent(0x1080U, 1U);
    Wait(TUNNEL_RESCAN_TO_SS1, 100U);
    Run(DIRECTION_REVERSAL_DELAY_MS + 10U);
    assert(tunnel_test_motors[CONVEYOR_DRIVER].running != 0U);
    Sensor(SENSOR_SS3_PIN, 0U);
    Sensor(SENSOR_SS1_PIN, 1U);
    Wait(TUNNEL_RESCAN_TO_SS3, 1000U);
    TunnelTest_SetSensor(SENSOR_SS7_PIN, 0U, 1U);
    Run(100U);
    assert(Tunnel_GetState() == TUNNEL_RESCAN_TO_SS3);
    assert(Tunnel_GetFault() == TUNNEL_FAULT_NONE);
    assert(rf_on != 0U && ZK7182_IsInventoryRunning() != 0U);
    Finish(1U, 1U);
}
static void TestIn7LossMissingExit(void) {
    scenario = "IN7 loss does not mask missing IN3 timeout";
    Boot(0U);
    ToCenter();
    TunnelTest_SetSensor(SENSOR_SS7_PIN, 0U, 1U);
    Wait(TUNNEL_FAULT, 30500U);
    assert(Tunnel_GetFault() == TUNNEL_FAULT_CONVEYOR_TIMEOUT);
    assert(tunnel_test_motors[CONVEYOR_DRIVER].running == 0U);
    assert(tunnel_test_motors[STEP_DRIVER].running == 0U);
    assert(tunnel_test_motors[STEP2_DRIVER].running == 0U);
}
int main(void) {
    Boot(0U);
    report_tags = 0U;
    ToCenter();
    Finish(0U, 0U);
    TestDisconnectAtCenter();
    TestMissingAck(1U, 0U);
    TestMissingAck(0U, 0U);
    TestMissingAck(0U, 1U);
    TestPermanentLossDeadline();
    TestNeverReady();
    TestMissingStopAck();
    TestInterlockStillWins();
    TestIn7LossDuringInventory(0U);
    TestIn7LossDuringInventory(1U);
    TestIn7LossBeforeReaderAck();
    TestIn7LossDuringRescan();
    TestIn7LossMissingExit();
    puts("PASS integration: real reader + Tunnel, confirmed IN7 retained before ACK and across "
         "AUTO/SEMI/Rescan to IN3, missing ACK/IN3 timeout unchanged, reader recovery, IN2 "
         "interlock; only 50/51 TX");
    return 0;
}
