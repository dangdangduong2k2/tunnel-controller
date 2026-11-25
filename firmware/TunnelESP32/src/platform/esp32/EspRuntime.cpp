#include "EspRuntime.h"
#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/queue.h"
#include "BoardProfile.h"
#include "EspPlatform.h"
#include "../../app/dual_control.h"
#include "../../protocol/legacy/SharedUart.h"
#include "EspModbus.h"
#include "EspFota.h"
extern "C" {
#include "../../control/tunnel.h"
#include "../../reader/zk7182.h"
#include "../../control/motor_pwm.h"
#include "../../common/debug_state.h"
#include "../../hmi/hmi.h"
#include "../../protocol/legacy/pc_control.h"
#include "../../control/speed_store.h"
#include "../../platform/compat/board.h"
}
GPIO_TypeDef virtualGpio = {};
volatile DebugStateTypeDef g_debug = {};
static Preferences storage;
static bool storageReady = false, ioReady = false;
static std::atomic<bool> lcdFault(false), stopRequested(false);
static bool capturing = false;
struct PcPacket {
    uint16_t size;
    uint8_t data[198];
};
static QueueHandle_t pcTx;

// DWIN remains on P1 UART0. Laptop J5 uses an ASCII envelope around PC v5,
// so outgoing PC data cannot accidentally write DWIN VPs or system settings.
extern "C" void EspLog(const char *) {}
extern "C" void EspRequestEmergency(void) {
    EspFotaCancel();
    MotorPwm_EmergencyStop();
    stopRequested = true;
}
extern "C" void EspRequestSafetyStop(uint8_t reason) {
    EspFotaCancel();
    MotorPwm_SafetyStop(reason);
    stopRequested = true;
}
extern "C" void EspRequestUpdateFence(void) {
    MotorPwm_SafetyStop(STOP_PC_DISCONNECT);
    stopRequested = true;
}
extern "C" uint8_t EspResetReason(void) {
    return (uint8_t)esp_reset_reason();
}
extern "C" uint8_t EspTakeEmergencyRequest(void) {
    return stopRequested.exchange(false);
}
extern "C" void EspPcTransmit(const uint8_t *bytes, uint16_t size) {
    PcPacket packet = {};
    packet.size = SharedUart_Encode(bytes, size, packet.data, sizeof(packet.data));
    if (!pcTx || !packet.size || xQueueSend(pcTx, &packet, 0) != pdTRUE)
        EspRequestSafetyStop(STOP_TX_QUEUE);
}
void EspFlushPcTx() {
    PcPacket packet;
    if (pcTx && xQueuePeek(pcTx, &packet, 0) == pdTRUE &&
        Serial0.availableForWrite() >= packet.size) {
        xQueueReceive(pcTx, &packet, 0);
        Serial0.write(packet.data, packet.size);
    }
}
extern "C" void EspPcPoll(void) {
    PcControl_Process();
    EspFlushPcTx();
}
extern "C" uint8_t EspHmiTransmit(const uint8_t *data, uint16_t size) {
    if (!ioReady || !size || Serial0.availableForWrite() < size)
        return 0;
    return Serial0.write(data, size) == size;
}
extern "C" uint8_t EspHmiTxIdle(void) {
    // Zero wait: only reports whether the FIFO and the queue behind it are empty
    // (the same probe EspModbus uses before an RS485 reply).
    return ioReady && uart_wait_tx_done(UART_NUM_0, 0) == ESP_OK;
}
extern "C" uint16_t EspHmiTxFree(void) {
    return ioReady ? (uint16_t)Serial0.availableForWrite() : 0;
}
extern "C" uint8_t EspPcTxPending(void) {
    return pcTx && uxQueueMessagesWaiting(pcTx) != 0;
}
extern "C" void EspHmiEmergency(void) {
    EspRequestEmergency();
}
extern "C" void EspHmiCapture(void) {
    if (!ioReady || capturing)
        return;
    capturing = true;
    if (lcdFault.exchange(false))
        Hmi_ReceiveError();
    unsigned count = 0;
    while (count < 512 && Serial0.available()) {
        int b = Serial0.read();
        if (b < 0)
            break;
        SharedUart_ReceiveByte((uint8_t)b);
        ++count;
    }
    if (Serial0.available())
        Hmi_ReceiveError();
    capturing = false;
}
extern "C" void EspFenceSerialInput(void) {
    // Discard only bytes already queued at the STOP boundary, bounded by the
    // UART driver's buffer. A newly pressed Reset is accepted next pass.
    int n = Serial0.available();
    for (int i = 0; i < n && i < 1024; ++i)
        Serial0.read();
    SharedUart_Fence();
}
extern "C" uint32_t HAL_GetTick(void) {
    return millis();
}
extern "C" GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *, uint16_t pin) {
    unsigned index;
    switch (pin) {
    case SENSOR_SS1_PIN:
        index = 0;
        break;
    case SENSOR_SS2_PIN:
        index = 1;
        break;
    case SENSOR_SS3_PIN:
        index = 2;
        break;
    case SENSOR_SS7_PIN:
        index = 6;
        break;
    default:
        return GPIO_PIN_SET;
    }
    return digitalRead(board::INPUT_PINS[index]) ? GPIO_PIN_RESET : GPIO_PIN_SET;
}
extern "C" void HAL_GPIO_Init(GPIO_TypeDef *, const GPIO_InitTypeDef *) {}
extern "C" void HAL_NVIC_SetPriority(int, uint32_t, uint32_t) {}
extern "C" void HAL_NVIC_EnableIRQ(int) {}
extern "C" uint8_t SpeedStore_Load(void) {
    uint8_t level = storageReady ? storage.getUChar("speed", 2) : 2;
    return level >= 1 && level <= 5 ? level : 2;
}
extern "C" uint8_t SpeedStore_Save(uint8_t level) {
    if (!storageReady || level < 1 || level > 5)
        return 0;
    return storage.putUChar("speed", level) == 1 && storage.getUChar("speed", 0) == level;
}
extern "C" void EspPollSensorEdges(void) {
    static uint8_t previous = 255;
    const uint8_t inputIndex[] = {0, 1, 2, 6}; // IN1, IN2, IN3, IN7; IN4 is unused.
    uint8_t raw = 0;
    for (unsigned i = 0; i < 4; i++)
        if (digitalRead(board::INPUT_PINS[inputIndex[i]]))
            raw |= 1U << i;
    const uint16_t virtualPins[] = {SENSOR_SS1_PIN, SENSOR_SS2_PIN, SENSOR_SS3_PIN, SENSOR_SS7_PIN};
    for (unsigned i = 0; i < 4; i++)
        if ((previous ^ raw) & (1U << i))
            HAL_GPIO_EXTI_Callback(virtualPins[i]);
    previous = raw;
}

bool EspRuntimeBegin() {
    esp_log_level_set("*", ESP_LOG_NONE);
    Serial0.setRxBufferSize(1024);
    Serial0.setTxBufferSize(256);
    Serial0.begin(115200, SERIAL_8N1, board::LCD_RX, board::LCD_TX);
    // A pull-up defines idle for an unplugged input; it cannot resolve
    // conflicting LCD/CH340 push-pull outputs on the board's shared RX net.
    gpio_set_pull_mode((gpio_num_t)board::LCD_RX, GPIO_PULLUP_ONLY);
    pcTx = xQueueCreate(8, sizeof(PcPacket));
    ioReady = (bool)Serial0 && pcTx;
    Serial0.onReceiveError([](hardwareSerial_error_t) {
        lcdFault = true;
        EspRequestSafetyStop(STOP_UART);
    });
    storageReady = storage.begin("tunnel-v1", false);
    return ioReady;
}
void EspRuntimeDisable() {
    ioReady = false;
}
bool EspRuntimeReady() {
    return ioReady;
}
