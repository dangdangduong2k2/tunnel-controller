#pragma once
#include "usbh_core.h"
#ifdef __cplusplus
extern "C" {
#endif
extern USBH_HandleTypeDef hUsbHostFS;
void USB_HOST_RequestRecovery(void);
#ifdef __cplusplus
}
#endif
