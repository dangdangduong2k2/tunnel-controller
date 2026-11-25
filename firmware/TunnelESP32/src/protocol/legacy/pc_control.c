#include "pc_control.h"
#include "../../control/tunnel.h"
#include "../../reader/zk7182.h"
#include "../../config/app_config.h"
#include "../../platform/compat/board.h"
#include "../../common/debug_state.h"
#include "../../control/motor_pwm.h"
#include "../../hmi/hmi.h"
#include "../../platform/compat/stm32f4xx_hal.h"
#include <string.h>
#if defined(ARDUINO) || defined(TUNNEL_MODBUS_TEST)
#include "../../platform/esp32/EspModbus.h"
#endif
#if defined(ARDUINO) || defined(TUNNEL_DUAL_TEST)
#include "../../platform/esp32/EspPlatform.h"
#endif
#if !defined(PC_CONTROL_TEST) && !defined(ARDUINO)
#include "main.h"
extern UART_HandleTypeDef huart3;
#elif defined(PC_CONTROL_TEST)
void PcControl_TestTransmit(const uint8_t *bytes, uint16_t length);
#else
#include "../../platform/esp32/EspPlatform.h"
#endif

#define PC_RING_SIZE 256U
#define PC_FRAME_SIZE 96U
static uint8_t rx_ring[PC_RING_SIZE];
static volatile uint16_t rx_write, rx_read;
static volatile uint8_t rx_discard, tx_busy;
static uint8_t request[PC_FRAME_SIZE], cache_request[PC_FRAME_SIZE];
static uint8_t tx[PC_FRAME_SIZE], cache_reply[PC_FRAME_SIZE];
static uint16_t used, cache_request_size, cache_reply_size;
#if !defined(PC_CONTROL_TEST) && !defined(ARDUINO)
static volatile uint16_t tx_index, tx_length;
#endif
static uint32_t parse_tick;
static uint8_t waiting_zk;
static uint8_t status_seen;
static uint8_t urgent_stop, urgent_request[8];
static uint8_t zk_request[PC_FRAME_SIZE];

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
static void U16(uint8_t *data, uint16_t v) {
    data[0] = (uint8_t)v;
    data[1] = (uint8_t)(v >> 8U);
}
static void U32(uint8_t *data, uint32_t v) {
    U16(data, (uint16_t)v);
    U16(data + 2U, (uint16_t)(v >> 16U));
}
static void Send(const uint8_t *bytes, uint16_t length) {
    (void)memcpy(tx, bytes, length);
#ifdef PC_CONTROL_TEST
    PcControl_TestTransmit(tx, length);
#elif defined(ARDUINO)
    EspPcTransmit(tx, length);
#else
    tx_index = 0U;
    tx_length = length;
    tx_busy = 1U;
    __HAL_UART_ENABLE_IT(&huart3, UART_IT_TXE);
#endif
}
static void Reply(uint8_t status, const uint8_t *data, uint8_t length) {
    uint16_t crc;
    cache_reply[0] = 0xA5U;
    cache_reply[1] = 0x5AU;
    cache_reply[2] = (uint8_t)(length + 4U);
    cache_reply[3] = request[3];
    cache_reply[4] = request[4];
    cache_reply[5] = request[5] | 0x80U;
    cache_reply[6] = status;
    if (length != 0U) {
        (void)memcpy(cache_reply + 7U, data, length);
    }
    cache_reply_size = (uint16_t)length + 9U;
    crc = Crc(cache_reply + 2U, (uint16_t)length + 5U);
    U16(cache_reply + cache_reply_size - 2U, crc);
    cache_request_size = (uint16_t)request[2] + 5U;
    (void)memcpy(cache_request, request, cache_request_size);
    Send(cache_reply, cache_reply_size);
}
static void Status(void) {
    uint8_t data[40] = {0};
    uint16_t flags = 0U;
    if (HAL_GPIO_ReadPin(SENSOR_SS2_PORT, SENSOR_SS2_PIN) == SENSOR_ACTIVE_LEVEL) {
        flags |= 1U;
    }
    if (MotorPwm_IsRunning(STEP_DRIVER) != 0U) {
        flags |= 2U | 512U;
    }
    if (MotorPwm_IsRunning(STEP2_DRIVER) != 0U) {
        flags |= 2U | 1024U;
    }
    if (HAL_GPIO_ReadPin(SENSOR_SS7_PORT, SENSOR_SS7_PIN) == SENSOR_ACTIVE_LEVEL) {
        flags |= 256U;
    }
    if (MotorPwm_IsRunning(CONVEYOR_DRIVER) != 0U) {
        flags |= 4U;
    }
    if (ZK7182_IsScanActive() != 0U) {
        flags |= 8U;
    }
    if (ZK7182_IsReady() != 0U) {
        flags |= 16U;
    }
    if (Tunnel_IsPcMaintenance() != 0U) {
        flags |= 32U;
    }
    if (HAL_GPIO_ReadPin(SENSOR_SS1_PORT, SENSOR_SS1_PIN) == SENSOR_ACTIVE_LEVEL) {
        flags |= 64U;
    }
    if (HAL_GPIO_ReadPin(SENSOR_SS3_PORT, SENSOR_SS3_PIN) == SENSOR_ACTIVE_LEVEL) {
        flags |= 128U;
    }
    data[0] = 8U;
    data[1] = (uint8_t)Tunnel_GetState();
    data[2] = (uint8_t)Tunnel_GetFault();
    data[3] = (uint8_t)Tunnel_GetMode();
    U16(data + 4U, flags);
    U16(data + 6U, ZK7182_GetUniqueEpcCount());
    U32(data + 8U, ZK7182_GetTotalReadCount());
    U16(data + 12U, Tunnel_GetSpeedLevel());
    U16(data + 14U, Tunnel_GetConveyorArr());
    U32(data + 16U, Tunnel_GetScanElapsedMs());
    data[20] = g_debug.cp210x_ready;
    data[21] = g_debug.last_status;
    data[22] = g_debug.reader_expected_command;
    data[23] = (CONVEYOR_PERIOD_NS == 14000UL) ? 0U : (CONVEYOR_PERIOD_NS == 10500UL) ? 1U : 2U;
    data[24] = MotorPwm_GetStopReason();
    data[25] = Tunnel_GetStopFlags();
#ifdef ARDUINO
    data[26] = EspResetReason();
#endif
    U32(data + 28U, HAL_GetTick());
    U32(data + 32U, g_hmi_debug.uart_error_count);
    U32(data + 36U, Tunnel_GetPcAge());
    Reply(0U, data, sizeof(data));
}
static void Execute(void) {
    uint8_t cmd = request[5], length = request[2] - 3U;
    uint16_t argument = 0U;
#if defined(ARDUINO) || defined(TUNNEL_MODBUS_TEST)
    if (!EspModbusLegacyAllow(cmd)) {
        Reply(2U, NULL, 0U);
        return;
    }
    if (EspModbusLegacyOwns())
        Tunnel_PcHeartbeat();
#else
    Tunnel_PcHeartbeat();
#endif
    if ((cmd != 0x17U) && (cache_request_size == (uint16_t)request[2] + 5U) &&
        (memcmp(cache_request, request, cache_request_size) == 0)) {
        Send(cache_reply, cache_reply_size);
        return; /* At-most-once physical action. */
    }
    if (cmd == 0x01U) {
        if (length == 0U) {
            status_seen = 1U;
            Status();
        } else {
            Reply(1U, NULL, 0U);
        }
        return;
    }
    if ((waiting_zk != 0U) && (cmd != 0x17U)) {
        Reply(2U, NULL, 0U);
        return;
    }
    if (cmd == 0x21U) {
        if (Tunnel_IsPcMaintenance() == 0U) {
            Reply(2U, NULL, 0U);
            return;
        }
        if ((length < 1U) || (length > 6U)) {
            Reply(1U, NULL, 0U);
            return;
        }
        if (ZK7182_IsReady() == 0U) {
            Reply(4U, NULL, 0U);
            return;
        }
        if (ZK7182_BeginPcCommand(request[6], request + 7U, length - 1U) == 0U) {
            Reply(1U, NULL, 0U);
            return;
        }
        (void)memcpy(zk_request, request, (uint16_t)request[2] + 5U);
        waiting_zk = 1U;
        return;
    }
    if ((cmd >= 0x15U) && (cmd <= 0x18U)) {
        if (length != 0U) {
            Reply(1U, NULL, 0U);
            return;
        }
    } else if (cmd == 0x14U) {
        if (length != 2U) {
            Reply(1U, NULL, 0U);
            return;
        }
        argument = (uint16_t)request[6] | ((uint16_t)request[7] << 8U);
    } else if (((cmd >= 0x10U) && (cmd <= 0x13U)) ||
               ((cmd == 0x20U) || (cmd == 0x19U) || (cmd == 0x22U))) {
        if (length != 1U) {
            Reply(1U, NULL, 0U);
            return;
        }
        argument = request[6];
    } else {
        Reply(7U, NULL, 0U);
        return;
    }
#if defined(ARDUINO) || defined(TUNNEL_DUAL_TEST)
    if (cmd == 0x17U) {
        EspRequestEmergency();
    }
#endif
    {
        uint8_t result = Tunnel_RemoteCommand(cmd, argument);
#if defined(ARDUINO) || defined(TUNNEL_MODBUS_TEST)
        if (cmd == 0x22U && argument == 0U && result == 0U)
            EspModbusLegacyRelease();
#endif
        Reply(result, NULL, 0U);
    }
}
uint8_t PcControl_IsReaderBusy(void) { return waiting_zk; }
void PcControl_Init(void) {
    rx_write = 0U;
    rx_read = 0U;
    rx_discard = 0U;
    tx_busy = 0U;
    used = 0U;
    waiting_zk = 0U;
    cache_reply_size = 0U;
    cache_request_size = 0U;
    status_seen = 0U;
    urgent_stop = 0U;
    parse_tick = HAL_GetTick();
#if !defined(PC_CONTROL_TEST) && !defined(ARDUINO)
    __HAL_UART_CLEAR_OREFLAG(&huart3);
    HAL_NVIC_SetPriority(USART3_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
    __HAL_UART_ENABLE_IT(&huart3, UART_IT_RXNE);
    __HAL_UART_ENABLE_IT(&huart3, UART_IT_ERR);
#endif
}
void PcControl_ReceiveByte(uint8_t value) {
    uint16_t next = (uint16_t)((rx_write + 1U) % PC_RING_SIZE);
    if (next == rx_read) {
        rx_discard = 1U;
#if defined(ARDUINO) || defined(TUNNEL_DUAL_TEST)
        EspRequestSafetyStop(STOP_RX_OVERFLOW);
#endif
        return;
    }
    rx_ring[rx_write] = value;
    rx_write = next;
}
uint8_t PcControl_HasStatusRequest(void) {
    return status_seen;
}
uint8_t PcControl_ReceivePacket(const uint8_t *data, uint16_t length) {
    uint16_t i, crc;
    if (length < 8U || length > 40U || data[0] != 0xA5U || data[1] != 0x5AU || data[2] < 3U ||
        data[2] > 35U || length != (uint16_t)data[2] + 5U)
        return 0U;
    crc = Crc(data + 2U, length - 4U);
    if (data[length - 2U] != (uint8_t)crc || data[length - 1U] != (uint8_t)(crc >> 8U))
        return 0U;
    if (length == 8U && data[5] == 0x17U) {
        memcpy(urgent_request, data, 8U);
        urgent_stop = 1U;
#if defined(ARDUINO) || defined(TUNNEL_DUAL_TEST)
        EspRequestEmergency();
#endif
    } else {
        for (i = 0U; i < length; i++)
            PcControl_ReceiveByte(data[i]);
    }
    return 1U;
}
void PcControl_ProcessEmergency(void) {
    if (!urgent_stop)
        return;
    urgent_stop = 0U;
    used = 0U;
    memcpy(request, urgent_request, 8U);
    Execute();
}
uint8_t PcControl_CanReceiveBatch(void) {
    return rx_read == rx_write;
}
void PcControl_DiscardInput(void) {
    rx_read = rx_write;
    rx_discard = 0U;
    used = 0U;
    urgent_stop = 0U;
    // Preserve the outstanding reader reply and STOP acknowledgement cache.
}
void PcControl_Process(void) {
    uint16_t count = 0U, length, crc;
    uint8_t data[64], saved_request[PC_FRAME_SIZE], received_length, status;
    uint32_t interrupt_state;
    if (tx_busy != 0U) {
        return;
    }
    if (waiting_zk != 0U) {
        if (ZK7182_TakePcReply(data, &received_length, &status) != 0U) {
            waiting_zk = 0U;
            (void)memcpy(saved_request, request, sizeof(request));
            (void)memcpy(request, zk_request, (uint16_t)zk_request[2] + 5U);
            Reply(status, data, received_length);
            (void)memcpy(request, saved_request, sizeof(request));
            return;
        }
    }
    if (rx_discard != 0U) {
        interrupt_state = __get_PRIMASK();
        __disable_irq();
        rx_read = rx_write;
        rx_discard = 0U;
        used = 0U;
        if (interrupt_state == 0U) {
            __enable_irq();
        }
    }
    if ((used != 0U) && ((HAL_GetTick() - parse_tick) >= 250UL)) {
        used = 0U;
    }
    while ((rx_read != rx_write) && (count++ < 64U)) {
        request[used++] = rx_ring[rx_read];
        rx_read = (uint16_t)((rx_read + 1U) % PC_RING_SIZE);
        parse_tick = HAL_GetTick();
        while (used != 0U) {
            if ((request[0] != 0xA5U) || ((used >= 2U) && (request[1] != 0x5AU)) ||
                ((used >= 3U) && ((request[2] < 3U) || (request[2] > 35U)))) {
                used--;
                (void)memmove(request, request + 1U, used);
                continue;
            }
            if (used < 3U) {
                break;
            }
            length = (uint16_t)request[2] + 5U;
            if (used < length) {
                break;
            }
            crc = Crc(request + 2U, length - 4U);
            if ((request[length - 2U] != (uint8_t)crc) ||
                (request[length - 1U] != (uint8_t)(crc >> 8U))) {
                used--;
                (void)memmove(request, request + 1U, used);
                continue;
            }
            Execute();
            used = 0U;
            return; /* Only one command per main-loop pass. */
        }
    }
}
void PcControl_IRQHandler(void) {
#if !defined(PC_CONTROL_TEST) && !defined(ARDUINO)
    uint32_t sr = huart3.Instance->SR;
    uint8_t value;
    if ((sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE)) != 0U) {
        value = (uint8_t)huart3.Instance->DR;
        if ((sr & (USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE)) != 0U) {
            rx_discard = 1U;
        } else {
            PcControl_ReceiveByte(value);
        }
    }
    if (((sr & USART_SR_TXE) != 0U) && ((huart3.Instance->CR1 & USART_CR1_TXEIE) != 0U)) {
        huart3.Instance->DR = tx[tx_index++];
        if (tx_index >= tx_length) {
            __HAL_UART_DISABLE_IT(&huart3, UART_IT_TXE);
            __HAL_UART_ENABLE_IT(&huart3, UART_IT_TC);
        }
    }
    /* Read SR again: writing DR above clears TC. Do not finish on stale TC. */
    if (((huart3.Instance->SR & USART_SR_TC) != 0U) &&
        ((huart3.Instance->CR1 & USART_CR1_TCIE) != 0U)) {
        __HAL_UART_DISABLE_IT(&huart3, UART_IT_TC);
        tx_busy = 0U;
    }
#endif
}
