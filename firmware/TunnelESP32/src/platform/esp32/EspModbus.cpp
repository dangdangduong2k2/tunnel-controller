#include "../../common/Version.h"
#include <Arduino.h>
#include <ETH.h>
#include <SPI.h>
#include <atomic>
#include <errno.h>
#include <fcntl.h>
#include <lwip/sockets.h>
#include <driver/uart.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "BoardProfile.h"
#include "EspPlatform.h"
#include "EspModbus.h"
#include "EspMqtt.h"
#include "../../hmi/HmiNetwork.h"
#include "EspConfig.h"
#include "../../protocol/modbus/ModbusConfig.h"
#include "../../protocol/modbus/ModbusCore.h"
extern "C" {
#include "../../control/tunnel.h"
#include "../../control/motor_pwm.h"
#include "../../reader/zk7182.h"
}

static bool ethernetReady = false, serialReady = false, started = false;
static bool stopQueued = false;
static std::atomic<bool> serialFault(false), queueFault(false);
static uint32_t networkStarted = 0;
static bool fallbackApplied = false;
static HmiNetworkPublisher hmiNetwork;
static uint16_t readStatus(uint16_t a) {
    uint32_t value;
    switch (a) {
    case 0:
        return TUNNEL_REGISTER_MAP; // v3 adds full baud/format metadata at IR96..99.
    case 1:
        return TUNNEL_FIRMWARE_REGISTER;
    case 2:
        return Tunnel_GetState();
    case 3:
        return Tunnel_GetFault();
    case 4:
        return Tunnel_GetMode();
    case 5: {
        uint16_t mask = 0;
        for (unsigned i = 0; i < 7; i++)
            if (digitalRead(board::INPUT_PINS[i]))
                mask |= 1U << i;
        return mask;
    }
    case 6:
        return MotorPwm_IsRunning(MOTOR_PWM_DRIVER_1) |
               (MotorPwm_IsRunning(MOTOR_PWM_DRIVER_2) << 1) |
               (MotorPwm_IsRunning(MOTOR_PWM_DRIVER_4) << 2) | (ZK7182_IsScanActive() << 3) |
               (Tunnel_IsPcMaintenance() << 4) | (MotorPwm_IsReleased() << 5);
    case 7:
        return MotorPwm_GetStopReason();
    case 8:
        return Tunnel_GetStopFlags();
    case 9:
        return Tunnel_GetSpeedLevel();
    case 10:
        return ZK7182_GetUniqueEpcCount();
    case 11:
    case 12:
        value = ZK7182_GetTotalReadCount();
        return a == 11 ? value >> 16 : value;
    case 13:
    case 14:
        value = Tunnel_GetScanElapsedMs();
        return a == 13 ? value >> 16 : value;
    case 15:
        return ZK7182_IsReady() | (ZK7182_IsInventoryRunning() << 1) |
               (ZK7182_IsInventoryResponsePending() << 2) | (uint16_t(ZK7182_GetScanResult()) << 8);
    case 24:
        return ethernetReady | ((ethernetReady && ETH.linkUp()) << 1) |
               ((ethernetReady && ETH.hasIP()) << 2);
    case 25:
    case 26: {
        IPAddress ip = ETH.localIP();
        return a == 25 ? (uint16_t(ip[0]) << 8) | ip[1] : (uint16_t(ip[2]) << 8) | ip[3];
    }
    case 27:
        return EspConfigGet().unit;
    case 28:
        return uint16_t(EspConfigGet().baud); // legacy low word; use IR96/97
    case 96:
        return uint16_t(EspConfigGet().baud >> 16);
    case 97:
        return uint16_t(EspConfigGet().baud);
    case 98:
        return EspConfigGet().parity;
    case 99:
        return EspConfigGet().stopBits;
    case 29:
        return serialReady;
    case 30:
    case 31:
        value = millis();
        return a == 30 ? value >> 16 : value;
    default:
        return 0;
    }
}
static bool readerBegin(uint8_t command, const uint8_t *data, uint8_t length) {
    if (!Tunnel_IsPcMaintenance() || !ZK7182_IsReady())
        return false;
    return ZK7182_BeginPcCommand(command, data, length) != 0;
}
static bool readerReply(uint8_t *data, uint8_t *length, uint8_t *status) {
    return ZK7182_TakePcReply(data, length, status) != 0;
}
static void stop(bool emergency) {
    stopQueued = true;
    if (emergency)
        EspRequestEmergency();
    else
        EspRequestSafetyStop(STOP_PC_DISCONNECT);
}
static uint8_t appCommand(uint8_t command, uint16_t value) {
    uint8_t result = Tunnel_RemoteCommand(command, value);
    if (command == 0x22 && value == 1 && result == 0) {
        stopQueued = true;
        EspRequestSafetyStop(STOP_PC_TAKEOVER);
    }
    return result;
}
static const mb::Backend backend = {readStatus,  appCommand,         readerBegin,
                                    readerReply, Tunnel_PcHeartbeat, stop};
static mb::Core core(backend);
struct RtuFrame {
    uint16_t length;
    uint32_t received, readyAt;
    uint8_t bytes[256];
};
static QueueHandle_t rtuQueue = nullptr;
static SemaphoreHandle_t rtuRxLock = nullptr;
struct TcpPeer {
    int fd = -1;
    uint16_t used = 0, out = 0, sent = 0;
    uint32_t rxAt = 0, txAt = 0;
    uint8_t rx[260] = {}, tx[260] = {};
};
static TcpPeer peers[3];
static unsigned nextPeer = 0;
static int listener = -1;
static uint32_t listenRetry = 0;
static void closePeer(unsigned i) {
    if (peers[i].fd >= 0) {
        lwip_close(peers[i].fd);
        core.disconnect(uint8_t(i + 2));
    }
    peers[i] = TcpPeer{};
}
static bool nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}
static void networkPoll() {
    if (!ethernetReady || !ETH.hasIP() || !ETH.linkUp()) {
        for (unsigned i = 0; i < 3; i++)
            closePeer(i);
        if (listener >= 0) {
            lwip_close(listener);
            listener = -1;
        }
        return;
    }
    if (listener < 0 && int32_t(millis() - listenRetry) >= 0) {
        listenRetry = millis() + 1000;
        int fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
            return;
        int reuse = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(EspConfigGet().tcpPort);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (!nonblocking(fd) || lwip_bind(fd, (sockaddr *)&addr, sizeof(addr)) ||
            lwip_listen(fd, 3)) {
            lwip_close(fd);
            return;
        }
        listener = fd;
    }
    if (listener < 0)
        return;
    int accepted = lwip_accept(listener, nullptr, nullptr);
    if (accepted >= 0) {
        bool assigned = false;
        if (nonblocking(accepted))
            for (unsigned i = 0; i < 3; i++)
                if (peers[i].fd < 0) {
                    peers[i] = TcpPeer{};
                    peers[i].fd = accepted;
                    core.resetPeer(uint8_t(i + 2));
                    assigned = true;
                    break;
                }
        if (!assigned)
            lwip_close(accepted);
    }
    // Bounded, nonblocking sends. A slow reader cannot stall the motor loop.
    for (unsigned i = 0; i < 3; i++) {
        TcpPeer &p = peers[i];
        if (p.fd < 0 || !p.out)
            continue;
        if (uint32_t(millis() - p.txAt) >= 250) {
            closePeer(i);
            continue;
        }
        int n = lwip_send(p.fd, p.tx + p.sent, p.out - p.sent, MSG_DONTWAIT);
        if (n > 0) {
            p.sent += uint16_t(n);
            if (p.sent == p.out)
                p.sent = p.out = 0;
        } else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
            closePeer(i);
    }
}
static bool tcpRequest(unsigned i) {
    TcpPeer &p = peers[i];
    if (p.fd < 0 || p.out)
        return false;
    if (p.used && uint32_t(millis() - p.rxAt) >= 250) {
        closePeer(i);
        return false;
    }
    int n = lwip_recv(p.fd, p.rx + p.used, sizeof(p.rx) - p.used, MSG_DONTWAIT);
    if (n > 0) {
        if (!p.used)
            p.rxAt = millis();
        p.used += uint16_t(n);
    } else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
        closePeer(i);
        return false;
    }
    if (p.used < 6)
        return false;
    uint16_t length = (uint16_t(p.rx[4]) << 8) | p.rx[5];
    if (p.rx[2] || p.rx[3] || length < 2 || length > 254) {
        closePeer(i);
        return false;
    }
    uint16_t total = length + 6;
    if (p.used < total)
        return false;
    p.out = uint16_t(core.tcp(uint8_t(i + 2), p.rx, total, p.tx, EspConfigGet().unit));
    p.sent = 0;
    p.txAt = millis();
    p.used -= total;
    memmove(p.rx, p.rx + total, p.used);
    p.rxAt = millis();
    return true;
}
extern "C" void EspModbusBegin(void) {
    EspConfigBegin();
    const cfg::Settings &settings = EspConfigGet();
    rtuQueue = xQueueCreate(4, sizeof(RtuFrame));
    rtuRxLock = xSemaphoreCreateMutex();
    digitalWrite(MODBUS_DE, LOW);
    pinMode(MODBUS_DE, OUTPUT);
    Serial1.setRxBufferSize(512);
    Serial1.setTxBufferSize(512);
    const uint32_t formats[3][2] = {
        {SERIAL_8N1, SERIAL_8N2}, {SERIAL_8E1, SERIAL_8E2}, {SERIAL_8O1, SERIAL_8O2}};
    Serial1.begin(settings.baud, formats[settings.parity][settings.stopBits - 1], MODBUS_RX,
                  MODBUS_TX);
    serialReady = rtuQueue && rtuRxLock && Serial1 &&
                  Serial1.setPins(MODBUS_RX, MODBUS_TX, -1, MODBUS_DE) &&
                  Serial1.setMode(UART_MODE_RS485_HALF_DUPLEX) && Serial1.setRxTimeout(2);
    // UART task captures a complete silence-delimited frame, never runs a command.
    Serial1.onReceiveError([](hardwareSerial_error_t) { serialFault = true; });
    Serial1.onReceive(
        []() {
            if (!rtuRxLock)
                return;
            xSemaphoreTake(rtuRxLock, portMAX_DELAY);
            RtuFrame frame = {};
            frame.received = millis();
            // Timeout at 2 character times; wait another 1.5 before responding.
            // At >19200 use the Modbus recommended fixed 1.75 ms silent interval.
            const cfg::Settings &s = EspConfigGet();
            const uint32_t bits = 1 + 8 + (s.parity ? 1 : 0) + s.stopBits;
            // RX timeout hardware counts in current character units.
            const uint32_t guard = s.baud > 19200 ? 1750 : (1500000UL * bits + s.baud - 1) / s.baud;
            frame.readyAt = micros() + guard;
            bool bad = serialFault.exchange(false);
            int count = Serial1.available();
            if (count > 256)
                bad = true;
            for (int i = 0; i < count; i++) {
                int b = Serial1.read();
                if (b >= 0 && frame.length < 256)
                    frame.bytes[frame.length++] = uint8_t(b);
            }
            if (!bad && frame.length && rtuQueue && xQueueSend(rtuQueue, &frame, 0) != pdTRUE)
                queueFault = true;
            xSemaphoreGive(rtuRxLock);
        },
        true);
    SPI.begin(MODBUS_SCK, MODBUS_MISO, MODBUS_MOSI, MODBUS_CS);
    ethernetReady = ETH.begin(ETH_PHY_W5500, 1, MODBUS_CS, MODBUS_IRQ, MODBUS_RST, SPI);
    if (ethernetReady) {
        ETH.setHostname("tunnel-3protocol");
        if (!settings.dhcp)
            ethernetReady = ETH.config(IPAddress(settings.ip), IPAddress(settings.gateway),
                                       IPAddress(settings.mask), IPAddress(settings.dns));
    }
    EspMqttBegin(core);
    networkStarted = millis();
    EspConfigWebBegin();
    started = true;
}
extern "C" void EspModbusPoll(void) {
    if (!started)
        return;
    core.tick(millis());
    cfg::Safety safety = {core.active(), false,
                          ZK7182_IsScanActive() || ZK7182_IsInventoryRunning(),
                          core.readerBusy() || ZK7182_IsInventoryResponsePending()};
    for (unsigned i = 0; i < 4; i++)
        if (MotorPwm_IsRunning((MotorPwmId)i))
            safety.moving = true;
    EspConfigPoll(safety);
    if (EspConfigBusy())
        return;
    if (stopQueued)
        return;
    if (ethernetReady && EspConfigGet().dhcp && !fallbackApplied && !ETH.hasIP() &&
        uint32_t(millis() - networkStarted) >= 30000 && cfg::canApply(safety)) {
        fallbackApplied = ETH.config(IPAddress(192, 168, 4, 1), IPAddress(0, 0, 0, 0),
                                     IPAddress(255, 255, 255, 0));
        EspConfigSetFallback(fallbackApplied);
    }
    EspMqttPoll();
    if (stopQueued)
        return;
    HmiNetworkStatus hmiStatus = {};
    hmiStatus.link = ETH.linkUp();
    hmiStatus.hasIp = ETH.hasIP();
    hmiStatus.fallback = fallbackApplied;
    hmiStatus.mqttConnected = EspMqttConnected();
    const IPAddress ip = ETH.localIP(), mask = ETH.subnetMask(), gateway = ETH.gatewayIP();
    for (unsigned i = 0; i < 4; i++) {
        hmiStatus.ip[i] = ip[i];
        hmiStatus.mask[i] = mask[i];
        hmiStatus.gateway[i] = gateway[i];
    }
    hmiNetwork.poll(millis(), EspConfigGet(), hmiStatus);
    if (queueFault.exchange(false) && core.owns(mb::RTU))
        core.disconnect(mb::RTU);
    networkPoll();
    if (stopQueued)
        return;
    // Fair round robin between RTU and each TCP client; one command per pass.
    for (unsigned k = 0; k < 4; k++) {
        unsigned slot = nextPeer;
        nextPeer = (nextPeer + 1) % 4;
        if (slot == 0) {
            RtuFrame frame;
            if (serialReady && uart_wait_tx_done(UART_NUM_1, 0) == ESP_OK &&
                xQueuePeek(rtuQueue, &frame, 0) == pdTRUE &&
                int32_t(micros() - frame.readyAt) >= 0) {
                xQueueReceive(rtuQueue, &frame, 0);
                if (uint32_t(millis() - frame.received) > 100)
                    continue;
                uint8_t reply[256];
                size_t length = core.rtu(EspConfigGet().unit, frame.bytes, frame.length, reply);
                if (length && Serial1.availableForWrite() >= int(length))
                    Serial1.write(reply, length);
                else if (length && core.owns(mb::RTU))
                    core.disconnect(mb::RTU);
                break;
            }
        } else if (tcpRequest(slot - 1))
            break;
    }
}
extern "C" void EspModbusFence(void) {
    if (!started)
        return;
    stopQueued = false;
    EspMqttFence();
    // Serialize the UART capture with the stop fence: a callback that began
    // before STOP must not enqueue its stale Reset after the queue is cleared.
    if (rtuRxLock)
        xSemaphoreTake(rtuRxLock, portMAX_DELAY);
    if (rtuQueue)
        xQueueReset(rtuQueue);
    // Bound draining to the UART ring size. Discard already received commands.
    for (int n = Serial1.available(); n > 0; --n)
        Serial1.read();
    if (rtuRxLock)
        xSemaphoreGive(rtuRxLock);
    for (unsigned i = 0; i < 3; i++) {
        TcpPeer &p = peers[i];
        p.used = 0;
        if (p.fd < 0)
            continue;
        uint8_t discard[260];
        // Close if more than a bounded amount remains; never execute pre-stop Reset.
        for (unsigned k = 0; k < 4; k++) {
            int n = lwip_recv(p.fd, discard, sizeof(discard), MSG_DONTWAIT);
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                break;
            if (n <= 0 || k == 3) {
                closePeer(i);
                break;
            }
        }
    }
    EspConfigFenced();
}
extern "C" uint8_t EspModbusActive(void) {
    return core.active();
}
extern "C" uint8_t EspModbusRtuReady(void) {
    return serialReady;
}
extern "C" uint8_t EspModbusLegacyAllow(uint8_t command) {
    core.tick(millis());
    if (EspConfigBusy() && command != 0x17)
        return 0;
    if (stopQueued && command != 0x17)
        return 0;
    return core.legacyAllow(command);
}
extern "C" uint8_t EspModbusLegacyOwns(void) {
    return core.owns(mb::LEGACY);
}
extern "C" void EspModbusLegacyRelease(void) {
    core.disconnect(mb::LEGACY);
}
