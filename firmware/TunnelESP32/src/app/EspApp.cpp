#include <Arduino.h>
#include "Application.h"
#include "../platform/esp32/EspFota.h"
#include "../platform/esp32/EspDwinUpdate.h"
#include "../platform/esp32/EspConfig.h"
#include "dual_control.h"
#include "../platform/esp32/BoardProfile.h"
#include "../platform/esp32/EspPlatform.h"
#include "../platform/esp32/EspRuntime.h"
#include "../platform/esp32/EspModbus.h"
#include "../protocol/legacy/SharedUart.h"
extern "C" {
#include "../control/tunnel.h"
#include "../reader/zk7182.h"
#include "../control/motor_pwm.h"
#include "../hmi/hmi.h"
#include "../protocol/legacy/pc_control.h"
}
static void safetyTask(void *) {
    for (;;) {
        EspSafetyPoll();
        vTaskDelay(1);
    }
}
void tunnelSetup() {
    EspMotorsBegin();
    for (uint8_t pin : board::INPUT_PINS)
        pinMode(pin, INPUT);
    const bool ioReady = EspRuntimeBegin();
    SharedUart_Init();
    PcControl_Init();
    MotorPwm_Init();
    ZK7182_Init();
    Hmi_Init();
    Tunnel_Init();
    EspControlAlive();
    if (xTaskCreatePinnedToCore(safetyTask, "motor-safety", 3072, nullptr, 5, nullptr, 1) !=
            pdPASS ||
        !ioReady || !EspMotorReady()) {
        MotorPwm_SafetyStop(STOP_INITIALIZATION);
        EspRuntimeDisable();
    }
    if (EspRuntimeReady()) EspDwinUpdateBegin();
    EspUsbBegin();
    EspModbusBegin();
}
void tunnelLoop() {
    if (!EspRuntimeReady()) {
        EspFotaHealth(false);
        delay(100);
        return;
    }
    EspControlAlive();
    EspFotaHealth(EspConfigStorageReady() && EspMotorReady() && !MotorPwm_IsRunning(MOTOR_PWM_DRIVER_1) &&
        !MotorPwm_IsRunning(MOTOR_PWM_DRIVER_2) && !MotorPwm_IsRunning(MOTOR_PWM_DRIVER_4) &&
        !ZK7182_IsScanActive());
    EspDwinUpdatePoll();
    DualControl_Process();
    EspUsbPoll();
    ZK7182_Process();
    EspFlushPcTx();
    delay(1);
}
