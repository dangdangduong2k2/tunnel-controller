#ifndef HMI_HAL_STUB_H
#define HMI_HAL_STUB_H

#include "stm32f4xx_hal.h"

#define HMI_HAL_STUB_MAX_TX_FRAMES 64U
#define HMI_HAL_STUB_MAX_TX_BYTES 32U

typedef struct {
    uint16_t length;
    uint8_t data[HMI_HAL_STUB_MAX_TX_BYTES];
} HmiHalStubTxFrame;

extern HmiHalStubTxFrame hmi_hal_stub_tx[HMI_HAL_STUB_MAX_TX_FRAMES];
extern uint32_t hmi_hal_stub_tx_count;
extern uint32_t hmi_hal_stub_rx_arm_count;
extern uint32_t hmi_hal_stub_irq_count;
extern uint8_t hmi_hal_stub_rx_active;
extern HAL_StatusTypeDef hmi_hal_stub_tx_status;
extern HAL_StatusTypeDef hmi_hal_stub_rx_status;

void HmiHalStub_Reset(void);
void HmiHalStub_ClearTx(void);
void HmiHalStub_SetTick(uint32_t tick);
void HmiHalStub_AdvanceTick(uint32_t milliseconds);
void HmiHalStub_InjectByte(uint8_t byte);
void HmiHalStub_Inject(const uint8_t *data, uint16_t length);
void HmiHalStub_InjectError(uint32_t error);

#endif /* HMI_HAL_STUB_H */
