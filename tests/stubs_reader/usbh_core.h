#ifndef USBH_CORE_H
#define USBH_CORE_H
#include <stdint.h>
typedef enum { USBH_OK = 0U, USBH_BUSY, USBH_FAIL } USBH_StatusTypeDef;
typedef struct {
    uint32_t unused;
} USBH_HandleTypeDef;
typedef struct {
    uint32_t unused;
} USBH_ClassTypeDef;
#endif
