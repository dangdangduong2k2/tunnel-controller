#include "hmi_hal_stub.h"
#include "debug_state.h"
#include "main.h"

#include <assert.h>
#include <string.h>

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart3;
volatile DebugStateTypeDef g_debug;

HmiHalStubTxFrame hmi_hal_stub_tx[HMI_HAL_STUB_MAX_TX_FRAMES];
uint32_t hmi_hal_stub_tx_count;
uint32_t hmi_hal_stub_rx_arm_count;
uint32_t hmi_hal_stub_irq_count;
uint8_t hmi_hal_stub_rx_active;
HAL_StatusTypeDef hmi_hal_stub_tx_status;
HAL_StatusTypeDef hmi_hal_stub_rx_status;

static uint8_t *s_rx_destination;
static uint16_t s_rx_length;
static uint32_t s_tick;
static uint32_t s_primask;

void HmiHalStub_Reset(void) {
    (void)memset(&huart1, 0, sizeof(huart1));
    (void)memset(&huart3, 0, sizeof(huart3));
    (void)memset((void *)&g_debug, 0, sizeof(g_debug));
    (void)memset(hmi_hal_stub_tx, 0, sizeof(hmi_hal_stub_tx));
    huart1.Instance = (void *)(uintptr_t)1U;
    huart3.Instance = (void *)(uintptr_t)3U;
    hmi_hal_stub_tx_count = 0U;
    hmi_hal_stub_rx_arm_count = 0U;
    hmi_hal_stub_irq_count = 0U;
    hmi_hal_stub_rx_active = 0U;
    hmi_hal_stub_tx_status = HAL_OK;
    hmi_hal_stub_rx_status = HAL_OK;
    s_rx_destination = NULL;
    s_rx_length = 0U;
    s_tick = 0U;
    s_primask = 0U;
}

void HmiHalStub_ClearTx(void) {
    (void)memset(hmi_hal_stub_tx, 0, sizeof(hmi_hal_stub_tx));
    hmi_hal_stub_tx_count = 0U;
}

void HmiHalStub_SetTick(uint32_t tick) {
    s_tick = tick;
}

void HmiHalStub_AdvanceTick(uint32_t milliseconds) {
    s_tick += milliseconds;
}

void HmiHalStub_InjectByte(uint8_t byte) {
    assert(hmi_hal_stub_rx_active != 0U);
    assert(s_rx_destination != NULL);
    assert(s_rx_length == 1U);
    *s_rx_destination = byte;
    hmi_hal_stub_rx_active = 0U;
    HAL_UART_RxCpltCallback(&huart1);
}

void HmiHalStub_Inject(const uint8_t *data, uint16_t length) {
    uint16_t index;

    for (index = 0U; index < length; index++) {
        HmiHalStub_InjectByte(data[index]);
    }
}

void HmiHalStub_InjectError(uint32_t error) {
    huart1.ErrorCode = error;
    hmi_hal_stub_rx_active = 0U;
    HAL_UART_ErrorCallback(&huart1);
}

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, uint8_t *data, uint16_t length,
                                    uint32_t timeout) {
    HmiHalStubTxFrame *capture;

    (void)timeout;
    assert(huart == &huart1);
    if (hmi_hal_stub_tx_status != HAL_OK) {
        return hmi_hal_stub_tx_status;
    }

    assert(hmi_hal_stub_tx_count < HMI_HAL_STUB_MAX_TX_FRAMES);
    assert(length <= HMI_HAL_STUB_MAX_TX_BYTES);
    capture = &hmi_hal_stub_tx[hmi_hal_stub_tx_count];
    capture->length = length;
    (void)memcpy(capture->data, data, length);
    hmi_hal_stub_tx_count++;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *huart, uint8_t *data, uint16_t length) {
    assert(huart == &huart1);
    hmi_hal_stub_rx_arm_count++;
    if (hmi_hal_stub_rx_status != HAL_OK) {
        return hmi_hal_stub_rx_status;
    }
    if (hmi_hal_stub_rx_active != 0U) {
        return HAL_BUSY;
    }

    s_rx_destination = data;
    s_rx_length = length;
    hmi_hal_stub_rx_active = 1U;
    return HAL_OK;
}

void HAL_UART_IRQHandler(UART_HandleTypeDef *huart) {
    assert(huart == &huart1);
    hmi_hal_stub_irq_count++;
}

uint32_t HAL_UART_GetError(const UART_HandleTypeDef *huart) {
    return huart->ErrorCode;
}

uint32_t HAL_GetTick(void) {
    return s_tick;
}

uint32_t __get_PRIMASK(void) {
    return s_primask;
}

void __disable_irq(void) {
    s_primask = 1U;
}

void __enable_irq(void) {
    s_primask = 0U;
}

void Error_Handler(void) {
    assert(0);
}
