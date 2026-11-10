#pragma once
#include <stdint.h>
typedef enum { USBH_OK = 0, USBH_BUSY, USBH_FAIL } USBH_StatusTypeDef;
typedef struct {
    uint32_t unused;
} USBH_HandleTypeDef;
