#pragma once
#include "usbh_core.h"
#ifdef __cplusplus
extern "C" {
#endif
uint8_t USBH_CP210X_IsReady(USBH_HandleTypeDef *);
USBH_StatusTypeDef USBH_CP210X_Transmit(USBH_HandleTypeDef *, uint8_t *, uint32_t);
USBH_StatusTypeDef USBH_CP210X_Receive(USBH_HandleTypeDef *, uint8_t *, uint32_t);
uint16_t USBH_CP210X_GetLastReceivedDataSize(USBH_HandleTypeDef *);
void USBH_CP210X_TransmitCallback(USBH_HandleTypeDef *);
void USBH_CP210X_ReceiveCallback(USBH_HandleTypeDef *);
void USBH_CP210X_ErrorCallback(USBH_HandleTypeDef *);
#ifdef __cplusplus
}
#endif
