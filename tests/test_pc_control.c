/* Exercise real PC parser -> real Tunnel -> real ZK driver. Only physical
 * GPIO, motors and USB are simulated. No serial ports are opened. */
#define main ExistingIntegrationMain
#include "test_reader_tunnel.c"
#undef main
#include "speed_store.h"
#include "speed_store_fake.h"
static uint8_t reply[96], last_usb[10], antenna_mask, config_error, config_drop;
static uint16_t reply_size, sequence, rf_profile;
static uint32_t reply_count, config_count;
static uint8_t powers[4];

void PcControl_TestTransmit(const uint8_t *bytes, uint16_t len) {
    uint16_t crc = Crc(bytes + 2U, len - 4U);
    assert(len <= sizeof(reply) && bytes[0] == 0xA5U && bytes[1] == 0x5AU);
    assert(bytes[2] + 5U == len);
    assert(bytes[len - 2U] == (uint8_t)crc && bytes[len - 1U] == (uint8_t)(crc >> 8U));
    memcpy(reply, bytes, len);
    reply_size = len;
    reply_count++;
}
static void PcUsbTransmit(const uint8_t *bytes, uint32_t len) {
    uint16_t crc = Crc(bytes, (uint16_t)(len - 2U));
    assert(len <= sizeof(last_usb) && bytes[0] + 1U == len && bytes[1] == 0U);
    assert(bytes[len - 2U] == (uint8_t)crc && bytes[len - 1U] == (uint8_t)(crc >> 8U));
    assert(bytes[2] == 0x21U || bytes[2] == 0x3FU || bytes[2] == 0x7FU || bytes[2] == 0x91U ||
           bytes[2] == 0xECU || bytes[2] == 0x94U || bytes[2] == 0x2FU);
    memcpy(last_usb, bytes, len);
    config_count++;
}
static void PcUsbReply(uint8_t command) {
    uint8_t bytes[18] = {5U, 0U, 0U, 0U}, length = 6U;
    bytes[2] = command;
    bytes[3] = config_error;
    if (config_drop != 0U) {
        return;
    }
    if (config_error == 0U) {
        if (command == 0x21U) {
            length = 18U;
            bytes[4] = 2U;
            bytes[5] = 21U;
            bytes[6] = 0x8BU;
            bytes[12] = antenna_mask;
        }
        if (command == 0x3FU) {
            antenna_mask = last_usb[3] & 15U;
        }
        if (command == 0x94U) {
            length = 10U;
            memcpy(bytes + 4U, powers, 4U);
        }
        if (command == 0x2FU) {
            uint8_t i;
            for (i = 0U; i < 4U; ++i) {
                powers[i] = last_usb[3U + i] & 0x7FU;
            }
        }
        if (command == 0x7FU) {
            if (last_usb[0] == 7U) {
                if (last_usb[3] == 1U) {
                    rf_profile = ((uint16_t)last_usb[4] << 8U) | last_usb[5];
                }
                length = 8U;
                bytes[4] = (uint8_t)(rf_profile >> 8U);
                bytes[5] = (uint8_t)rf_profile;
            } else if (last_usb[3] == 0U) {
                length = 7U;
                bytes[4] = (uint8_t)rf_profile;
            } else {
                rf_profile = last_usb[3] & 63U;
            }
        }
        if (command == 0xECU) {
            length = 10U;
            bytes[4] = 0U;
            bytes[5] = 2U;
            bytes[6] = 1U;
            bytes[7] = 0U;
        }
        if (command == 0x91U) {
            length = 7U;
            bytes[4] = 18U - last_usb[7];
        }
    }
    bytes[0] = length - 1U;
    Feed(bytes, length);
}
static uint16_t MakeRequest(uint8_t *bytes, uint8_t cmd, const uint8_t *data, uint8_t len) {
    uint16_t crc;
    bytes[0] = 0xA5U;
    bytes[1] = 0x5AU;
    bytes[2] = len + 3U;
    bytes[3] = (uint8_t)sequence;
    bytes[4] = (uint8_t)(sequence >> 8U);
    bytes[5] = cmd;
    if (len != 0U) {
        memcpy(bytes + 6U, data, len);
    }
    crc = Crc(bytes + 2U, len + 4U);
    bytes[len + 6U] = (uint8_t)crc;
    bytes[len + 7U] = (uint8_t)(crc >> 8U);
    return len + 8U;
}
static void FeedPc(const uint8_t *bytes, uint16_t len) {
    uint16_t i;
    for (i = 0U; i < len; i++) {
        PcControl_ReceiveByte(bytes[i]);
    }
}
static uint8_t Request(uint8_t cmd, const uint8_t *data, uint8_t len) {
    uint8_t bytes[48];
    uint16_t size;
    uint32_t timeout = 2300U, before = reply_count;
    sequence++;
    size = MakeRequest(bytes, cmd, data, len);
    FeedPc(bytes, size);
    while (reply_count == before && timeout-- != 0U) {
        Run(1U);
    }
    assert(reply_count == before + 1U && reply[5] == (uint8_t)(cmd | 0x80U));
    assert(reply[3] == (uint8_t)sequence && reply[4] == (uint8_t)(sequence >> 8U));
    return reply[6];
}
static uint8_t One(uint8_t cmd, uint8_t arg) {
    return Request(cmd, &arg, 1U);
}
static void PcBoot(void) {
    PcControl_Init();
    Boot(0U);
    PcControl_Init();
    sequence = 0U;
    reply_count = 0U;
    config_count = 0U;
    antenna_mask = 7U;
    rf_profile = 13U;
    config_error = config_drop = 0U;
    memset(powers, 30, sizeof(powers));
}

static void TestFramingAndInterlocks(void) {
    uint8_t bytes[48], arg = 1U;
    uint16_t len;
    uint32_t calls, before;
    PcBoot();
    assert(Request(1U, NULL, 0U) == 0U);
    assert(reply_size == 49U && reply[7] == 8U && reply[19] == 1U && reply[21] == 41U);
    assert(One(0x12U, 1U) == 3U);    /* Open gate cannot scan. */
    assert(One(0x21U, 0x21U) == 2U); /* Bridge requires maintenance. */
    sequence++;
    len = MakeRequest(bytes, 0x11U, &arg, 1U);
    bytes[len - 1U] ^= 1U;
    before = reply_count;
    FeedPc(bytes, len);
    Run(5U);
    assert(reply_count == before && tunnel_test_motors[CONVEYOR_DRIVER].running == 0U);
    /* Noise, partial packet timeout, then a valid fragmented frame. */
    PcControl_ReceiveByte(0x42U);
    PcControl_ReceiveByte(0xA5U);
    PcControl_ReceiveByte(0x5AU);
    Run(300U);
    sequence++;
    len = MakeRequest(bytes, 0x11U, &arg, 1U);
    FeedPc(bytes, 4U);
    Run(10U);
    FeedPc(bytes + 4U, len - 4U);
    Run(5U);
    assert(reply[6] == 0U);
    assert(tunnel_test_motors[CONVEYOR_DRIVER].running != 0U);
    calls = tunnel_test_motors[CONVEYOR_DRIVER].start_continuous_calls;
    FeedPc(bytes, len);
    Run(5U);
    assert(tunnel_test_motors[CONVEYOR_DRIVER].start_continuous_calls == calls);
    Run(3001U);
    assert(tunnel_test_motors[CONVEYOR_DRIVER].running == 0U &&
           Tunnel_GetState() == TUNNEL_STOPPED);
    assert(Request(0x17U, NULL, 0U) == 0U);
    assert(tunnel_test_motors[STEP_DRIVER].running == 0U); /* STOP never homes. */
    assert(Request(0x6AU, NULL, 0U) == 7U);
    /* Overflowed input is discarded and a later valid status works. */
    for (calls = 0U; calls < 300U; calls++) {
        PcControl_ReceiveByte(0x55U);
    }
    Run(2U);
    assert(Request(1U, NULL, 0U) == 0U);
}
static void TestDuration(void) {
    static const uint16_t periods[5] = {10500U, 7500U, 5000U, 2500U, 1250U};
    uint8_t level, old[2] = {3U, 0U};
    PcBoot();
    assert(Request(0x14U, old, 2U) == 7U);
    assert(One(0x19U, 0U) == 1U && One(0x19U, 6U) == 1U);
    for (level = 1U; level <= 5U; ++level) {
        assert(One(0x19U, level) == 0U);
        assert(Tunnel_GetSpeedLevel() == level);
        assert(One(0x11U, 1U) == 0U);
        assert(tunnel_test_motors[CONVEYOR_DRIVER].period_ns == periods[level - 1U]);
        assert(One(0x19U, 1U) == 2U); /* No period jump while moving. */
        assert(One(0x11U, 0U) == 0U);
        Run(10U);
    }
    assert(config_count == 0U);
}
static void TestPersistentSpeed(void) {
    uint32_t writes;
    PcBoot();
    assert(One(0x19U, 4U) == 0U && SpeedStore_Load() == 4U);
    writes = speed_test_writes;
    assert(One(0x19U, 4U) == 0U && speed_test_writes == writes);
    /* Recreate the real Tunnel / UART / reader state, retaining only flash. */
    SpeedStoreTest_PowerCycle();
    PcControl_Init();
    ZK7182_Init();
    Tunnel_Init();
    ZK7182_TransportReady();
    Wait(TUNNEL_WAIT_SS1_LOW, 1500U);
    assert(Request(1U, NULL, 0U) == 0U && reply[19] == 4U && reply[21] == 9U);
    assert(One(0x11U, 1U) == 0U);
    assert(tunnel_test_motors[CONVEYOR_DRIVER].period_ns == 2500U);
    assert(One(0x11U, 0U) == 0U);
    Run(10U);
    speed_test_fail_after = 2;
    assert(One(0x19U, 5U) == 8U);
    assert(Tunnel_GetSpeedLevel() == 4U && SpeedStore_Load() == 4U);
    SpeedStoreTest_PowerCycle();
    PcControl_Init();
    ZK7182_Init();
    Tunnel_Init();
    ZK7182_TransportReady();
    Wait(TUNNEL_WAIT_SS1_LOW, 1500U);
    assert(Tunnel_GetSpeedLevel() == 4U);
    assert(One(0x19U, 5U) == 0U && SpeedStore_Load() == 5U);
    assert(config_count == 0U);
    puts("PASS PC speed persistence: Set/ACK, reboot restores actual PWM, unchanged save skips "
         "writes, torn write returns 8 and preserves saved speed; no ZK settings");
}
static void TestZkBridge(void) {
    uint8_t ant[] = {0x3FU, 0x85U}, profile[] = {0x7FU, 0xCDU};
    uint8_t rl[] = {0x91U, 0x00U, 0x0EU, 0x11U, 0x90U, 2U}; /* 922000 kHz, port 3 */
    uint8_t query[] = {0x7FU, 0U};
    uint32_t count;
    PcBoot();
    assert(One(0x20U, 1U) == 0U && Tunnel_IsPcMaintenance() != 0U);
    Sensor(SENSOR_SS1_PIN, 1U);
    assert(Tunnel_GetState() == TUNNEL_WAIT_SS1_LOW);
    TunnelTest_QueueEvent(0x1020U, 1U);
    Run(10U);
    assert(starts == 0U);
    assert(One(0x11U, 1U) == 2U && One(0x12U, 1U) == 2U);
    assert(One(0x21U, 0x21U) == 0U && reply[7] == 0x21U && reply[17] == 7U);
    assert(Request(0x21U, ant, sizeof(ant)) == 0U && antenna_mask == 5U && last_usb[3] == 0x85U);
    ant[1] = 9U;
    assert(Request(0x21U, ant, sizeof(ant)) == 0U &&
           antenna_mask == 9U); /* persistent explicit Set */
    assert(Request(0x21U, profile, sizeof(profile)) == 0U && rf_profile == 13U &&
           last_usb[3] == 0xCDU);
    {
        uint8_t bytes[48];
        uint16_t len = MakeRequest(bytes, 0x21U, profile, sizeof(profile));
        count = config_count;
        FeedPc(bytes, len);
        Run(20U);
        assert(config_count == count);
    }
    assert(Request(0x21U, query, sizeof(query)) == 0U && reply[9] == 13U);
    assert(Request(0x21U, rl, sizeof(rl)) == 0U && reply[9] == 16U && last_usb[7] == 2U);
    count = config_count;
    ant[1] = 0U;
    assert(Request(0x21U, ant, sizeof(ant)) == 1U);
    rl[5] = 4U;
    assert(Request(0x21U, rl, sizeof(rl)) == 1U);
    rl[5] = 0U;
    rl[4] = 0x91U;
    assert(Request(0x21U, rl, sizeof(rl)) == 1U);
    profile[1] = 0xFFU;
    assert(Request(0x21U, profile, sizeof(profile)) == 1U);
    assert(One(0x21U, 0x50U) == 1U && One(0x21U, 0x76U) == 1U && config_count == count);
    config_error = 0xF8U;
    assert(One(0x21U, 0x21U) == 0U && reply[8] == 0xF8U);
    config_error = 0U;
    config_drop = 1U;
    assert(One(0x21U, 0x21U) == 6U);
    config_drop = 0U;
    Run(3001U);
    assert(Tunnel_IsPcMaintenance() == 0U && Tunnel_GetState() == TUNNEL_STOPPED);
}
static void TestShortAutomaticAndTransportLoss(void) {
    uint8_t bytes[48], cmd = 0x21U;
    uint16_t len;
    uint32_t before;
    PcBoot();
    ToCenter();
    Finish(0U, 1U);
    assert(config_count == 0U);
    PcBoot();
    assert(One(0x20U, 1U) == 0U);
    config_drop = 1U;
    sequence++;
    len = MakeRequest(bytes, 0x21U, &cmd, 1U);
    FeedPc(bytes, len);
    Run(5U);
    assert(pending == 0x21U); /* Transaction still waiting. */
    before = tunnel_test_tick;
    assert(Request(0x17U, NULL, 0U) == 0U);
    assert(tunnel_test_tick - before < 20U);
    assert(Tunnel_GetState() == TUNNEL_STOPPED && Tunnel_IsPcMaintenance() == 0U);
    assert(One(0x11U, 1U) == 2U); /* Emergency latch */
    Run(2200U);
    config_drop = 0U;
    assert(Tunnel_GetState() == TUNNEL_STOPPED);
    PcBoot();
    assert(One(0x20U, 1U) == 0U);
    sequence++;
    len = MakeRequest(bytes, 0x21U, &cmd, 1U);
    FeedPc(bytes, len);
    Run(2U);
    USBH_CP210X_ErrorCallback(&hUsbHostFS);
    Run(2U);
    assert(reply[5] == 0xA1U && reply[6] == 4U);
    TunnelTest_QueueEvent(0x1040U, 1U);
    Run(5U);
    assert(Tunnel_IsPcMaintenance() == 0U && Tunnel_GetState() == TUNNEL_STOPPED);
}
static void TestReader288Compatibility(void) {
    uint8_t version[] = {0xECU, 0U}, get[] = {0x7FU, 0U, 0U, 0U};
    uint8_t set[] = {0x7FU, 1U, 0x10U, 0x1BU};           /* profile 4123, big endian */
    uint8_t rl[] = {0x91U, 0U, 0x0CU, 0xD1U, 0xBDU, 3U}; /* 840125 kHz, port 4 */
    uint32_t count;
    PcBoot();
    assert(One(0x20U, 1U) == 0U);
    assert(Request(0x21U, version, sizeof(version)) == 0U && reply[7] == 0xECU && reply[10] == 2U);
    assert(rf_profile == 13U && antenna_mask == 7U);
    assert(Request(0x21U, get, sizeof(get)) == 0U && reply[9] == 0U && reply[10] == 13U);
    assert(Request(0x21U, set, sizeof(set)) == 0U && rf_profile == 4123U);
    assert(last_usb[0] == 7U && last_usb[3] == 1U && last_usb[4] == 0x10U && last_usb[5] == 0x1BU);
    assert(Request(0x21U, get, sizeof(get)) == 0U && reply[9] == 0x10U && reply[10] == 0x1BU);
    assert(Request(0x21U, rl, sizeof(rl)) == 0U && reply[9] == 15U);
    count = config_count;
    set[2] = 0xFFU;
    set[3] = 0xFFU;
    assert(Request(0x21U, set, sizeof(set)) == 1U);
    get[3] = 1U;
    assert(Request(0x21U, get, sizeof(get)) == 1U);
    version[1] = 1U;
    assert(Request(0x21U, version, sizeof(version)) == 1U);
    assert(config_count == count && antenna_mask == 7U);
    puts("PASS reader288 compatibility: EC version query, 16-bit profile Set/Get, 840.125 MHz ANT4 "
         "return loss, invalid writes rejected");
}

static void TestAntennaPower(void) {
    uint8_t set[] = {0x2FU, 0x9EU, 0x9BU, 0x9CU, 0x9DU}, get = 0x94U;
    uint8_t bytes[48];
    uint16_t len;
    uint32_t count;
    PcBoot();
    assert(Request(0x21U, set, sizeof(set)) == 2U && config_count == 0U);
    assert(One(0x20U, 1U) == 0U);
    assert(Request(0x21U, &get, 1U) == 0U && reply[7] == 0x94U && reply[9] == 30U &&
           reply[12] == 30U);
    assert(last_usb[0] == 4U);
    assert(Request(0x21U, set, sizeof(set)) == 0U && last_usb[0] == 8U);
    assert(memcmp(last_usb + 3U, set + 1U, 4U) == 0);
    count = config_count;
    len = MakeRequest(bytes, 0x21U, set, sizeof(set));
    FeedPc(bytes, len);
    Run(20U);
    assert(config_count == count); /* Duplicate Set not re-executed. */
    assert(Request(0x21U, &get, 1U) == 0U && reply[9] == 30U && reply[10] == 27U &&
           reply[11] == 28U && reply[12] == 29U);
    assert(antenna_mask == 7U && rf_profile == 13U && starts == 0U);
    set[1] = 0U;
    set[2] = 10U;
    set[3] = 20U;
    set[4] = 30U;
    assert(Request(0x21U, set, sizeof(set)) == 0U && powers[0] == 0U && powers[3] == 30U);
    assert(memcmp(last_usb + 3U, set + 1U, 4U) == 0); /* Persistent bytes preserved. */
    count = config_count;
    set[2] = 31U;
    assert(Request(0x21U, set, sizeof(set)) == 1U);
    set[2] = 0xFFU;
    assert(Request(0x21U, set, sizeof(set)) == 1U);
    set[2] = 10U;
    assert(Request(0x21U, set, 2U) == 1U); /* Global one-byte Set intentionally disallowed. */
    assert(Request(0x21U, set, 4U) == 1U);
    set[0] = 0x94U;
    assert(Request(0x21U, set, 2U) == 1U);
    assert(config_count == count);
    set[0] = 0x2FU;
    config_error = 0xF8U;
    assert(Request(0x21U, set, sizeof(set)) == 0U && reply[8] == 0xF8U && powers[0] == 0U);
    config_error = 0U;
    config_drop = 1U;
    count = config_count;
    assert(Request(0x21U, set, sizeof(set)) == 6U && config_count == count + 1U);
    config_drop = 0U;
    assert(One(0x20U, 0U) == 0U && Request(0x21U, &get, 1U) == 2U);
    puts("PASS antenna power: 94 Get, four-port 2F RAM/persistent, exact bytes, readback, invalid "
         "values/lengths, lease, duplicate Set, timeout and no inventory/config side effects");
}

static void TestPcSupervision(void) {
    PcBoot();
    assert(One(0x22U, 1U) == 0U);
    assert(Tunnel_GetStopFlags() & 8U);
    /* Simulate an already homed, armed AUTO cycle with a registered monitor. */
    MotorPwm_Init();
    Tunnel_Init();
    /* Establish sensor AUTO first, then register: takeover must stop it holding. */
    Run(1300U);
    TunnelTest_SetPairedSensor(SENSOR_SS1_PIN, 1U, 1U);
    Run(1100U);
    assert(MotorPwm_IsRunning(CONVEYOR_DRIVER));
    assert(One(0x22U, 1U) == 0U && !MotorPwm_IsRunning(CONVEYOR_DRIVER));
    Run(3001U);
    assert(Tunnel_GetState() == TUNNEL_STOPPED && !MotorPwm_IsReleased());
    assert(!MotorPwm_IsRunning(CONVEYOR_DRIVER) && MotorPwm_GetStopReason() == STOP_PC_TIMEOUT);
    assert(Request(1U, NULL, 0U) == 0U && reply[31] == STOP_PC_TIMEOUT && (reply[32] & 1U));
    assert(One(0x11U, 1U) == 2U); /* Recovery of COM alone cannot restart. */
    PcBoot();
    assert(One(0x22U, 1U) == 0U);
    /* Arrival of a delayed status must not hide an expired old heartbeat. */
    TunnelTest_AdvanceTick(3001U);
    assert(Request(1U, NULL, 0U) == 0U && reply[31] == STOP_PC_TIMEOUT);
    PcBoot();
    assert(One(0x22U, 1U) == 0U);
    assert(One(0x22U, 0U) == 0U);
    assert(Tunnel_GetState() == TUNNEL_STOPPED && !MotorPwm_IsReleased());
    assert(!(Tunnel_GetStopFlags() & 8U) && MotorPwm_GetStopReason() == STOP_PC_DISCONNECT);
    puts("PASS PC supervision: sensor AUTO timeout holds, late heartbeat cannot revive, deliberate "
         "disconnect holds");
}
#ifdef TUNNEL_DUAL_TEST
int PcRegressionMain(void)
#else
int main(void)
#endif
{
    TestPcSupervision();
    TestAntennaPower();
    TestPersistentSpeed();
    TestReader288Compatibility();
    TestFramingAndInterlocks();
    TestDuration();
    TestZkBridge();
    TestShortAutomaticAndTransportLoss();
    puts("PASS PC control: real UART framing/Tunnel/ZK bridge, CRC, fragmentation, overflow, "
         "replay, interlocks, five speeds, Emergency priority, lease expiry, antenna/profile/RL, "
         "explicit-write allowlist, timeout/error forwarding");
    return 0;
}
