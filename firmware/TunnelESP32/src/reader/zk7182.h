#ifndef ZK7182_H
#define ZK7182_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "../platform/compat/usbh_core.h"

typedef enum {
    ZK7182_SCAN_RESULT_IDLE = 0U,
    ZK7182_SCAN_RESULT_IN_PROGRESS,
    ZK7182_SCAN_RESULT_OK,
    ZK7182_SCAN_RESULT_TRANSPORT_LOST,
    ZK7182_SCAN_RESULT_NO_RESPONSE,
    ZK7182_SCAN_RESULT_READER_ERROR,
    ZK7182_SCAN_RESULT_PROTOCOL_ERROR,
    ZK7182_SCAN_RESULT_CAPACITY_OVERFLOW,
    ZK7182_SCAN_RESULT_INCOMPLETE_ANTENNAS
} ZK7182_ScanResult;

void ZK7182_Init(void);
void ZK7182_Process(void);
void ZK7182_TransportReady(void);
void ZK7182_TransportLost(void);

uint8_t ZK7182_IsReady(void);
uint8_t ZK7182_StartScan(uint32_t duration_ms);
uint8_t ZK7182_IsScanActive(void);
uint8_t ZK7182_IsInventoryRunning(void);
uint8_t ZK7182_IsScanComplete(void);
uint8_t ZK7182_IsInventoryResponsePending(void);
ZK7182_ScanResult ZK7182_GetScanResult(void);
uint16_t ZK7182_GetUniqueEpcCount(void);
uint32_t ZK7182_GetTotalReadCount(void);
/* ISR-safe pause queues STOP in Process. Tags arriving before STOP ACK
 * remain part of this scan until cancellation or its 30 s deadline. */
void ZK7182_PauseScan(void);
void ZK7182_ResumeScan(void);
void ZK7182_CancelScanAtUniqueCount(uint16_t unique_count);
void ZK7182_ClearScanComplete(void);
void ZK7182_CancelScan(void);
/* Only explicit PC maintenance requests may use this bounded command bridge.
 * TakePcReply data is command/status/reader payload; max 64 bytes. */
uint8_t ZK7182_BeginPcCommand(uint8_t command, const uint8_t *data, uint8_t length);
uint8_t ZK7182_TakePcReply(uint8_t *data, uint8_t *length, uint8_t *status);

#ifdef __cplusplus
}
#endif

#endif /* ZK7182_H */
