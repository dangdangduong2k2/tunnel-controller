#ifndef PC_CONTROL_H
#define PC_CONTROL_H
#include <stdint.h>
void PcControl_Init(void);
void PcControl_Process(void);
uint8_t PcControl_HasStatusRequest(void);
uint8_t PcControl_CanReceiveBatch(void);
void PcControl_DiscardInput(void);
/* Complete, CRC-checked packets from the shared-UART envelope. */
uint8_t PcControl_ReceivePacket(const uint8_t *data, uint16_t length);
void PcControl_ProcessEmergency(void);
void PcControl_IRQHandler(void);
/* ISR only enqueues bytes. No commands run inside the UART interrupt. */
void PcControl_ReceiveByte(uint8_t value);
#endif
