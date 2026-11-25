#ifndef HMI_H
#define HMI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define HMI_PAGE_HOME 0U
#define HMI_PAGE_CONTROL 1U
#define HMI_PAGE_RESULT 2U

#define HMI_VP_BUTTON_CONVEYOR 0x1001U
#define HMI_VP_BUTTON_GATE 0x1010U
#define HMI_VP_BUTTON_SCAN 0x1020U
/* Legacy one-button panels use 1 to toggle. HMI v2 separate buttons use
 * explicit values so repeating START/STOP never reverses the requested action. */
#define HMI_SCAN_TOGGLE 1U
#define HMI_SCAN_START 2U
#define HMI_SCAN_STOP 3U
#define HMI_VP_BUTTON_AUTO 0x1030U
#define HMI_VP_BUTTON_EMERGENCY 0x1040U
#define HMI_VP_BUTTON_RESET 0x1050U
#define HMI_VP_BUTTON_SEMI 0x1060U
#define HMI_VP_BUTTON_CONFIRM 0x1070U
#define HMI_VP_BUTTON_RESCAN 0x1080U
/* HMI v2: every settings/history/editor touch. Never reaches the machine queue. */
#define HMI_VP_UI_EVENT 0x1090U

#define HMI_VP_QUANTITY 0x2000U

#define HMI_VP_INDICATOR_CONVEYOR 0x3001U
#define HMI_VP_INDICATOR_GATE 0x3002U
#define HMI_VP_INDICATOR_SCAN 0x3003U
#define HMI_VP_INDICATOR_AUTO 0x3004U
#define HMI_VP_INDICATOR_SEMI 0x3005U

typedef struct {
    uint16_t vp;
    uint16_t value;
} HmiEvent;

typedef struct {
    uint32_t tx_frame_count;
    uint32_t tx_error_count;
    uint32_t rx_byte_count;
    uint32_t rx_frame_count;
    uint32_t rx_ack_count;
    uint32_t rx_event_count;
    uint32_t rx_ignored_frame_count;
    uint32_t rx_malformed_frame_count;
    uint32_t rx_timeout_count;
    uint32_t rx_ring_overflow_count;
    uint32_t event_queue_overflow_count;
    uint32_t uart_error_count;
    uint32_t rx_arm_error_count;
    uint32_t config_read_count;
    uint32_t config_response_count;
    uint32_t config_write_count;
    uint32_t last_uart_error;
    uint16_t last_vp;
    uint16_t last_value;
    uint8_t rx_armed;
    uint8_t event_queue_depth;
    uint8_t emergency_pending;
    uint8_t config_known;
    uint8_t auto_upload_enabled;
    /* 0=not detected, 1=CRC disabled, 2=CRC enabled. */
    uint8_t crc_mode;
    uint8_t reset_pending;
    uint8_t config_write_pending;
    uint8_t ui_queue_depth;
    uint32_t ui_event_count;
    uint32_t ui_queue_overflow_count;
} HmiDiagnostics;

extern volatile HmiDiagnostics g_hmi_debug;

void Hmi_Init(void);
void Hmi_SetControlLocked(uint8_t locked);
uint8_t Hmi_IsControlLocked(void);
void Hmi_Process(void);
/* ESP32 stream adapter: called only by the control task. */
void Hmi_ReceiveByte(uint8_t byte);
void Hmi_ReceiveError(void);
/* Called only after the coordinator has already latched Emergency. */
void Hmi_DiscardActionsAfterStop(void);
uint8_t Hmi_PopEvent(HmiEvent *event);
/* Clears normal queued/older receive actions. Pending or in-flight valid
 * RESET/EMERGENCY requests are deliberately preserved. */
void Hmi_FlushEvents(void);
/* HMI v2 UI codes received on HMI_VP_UI_EVENT, oldest first. Separate from the
 * machine queue: no ownership lock, no state-transition fence and no repeat
 * filtering (typing "11" delivers two identical codes). Emptied by Init, a
 * UART error and every STOP fence. Values are never copied to diagnostics. */
uint8_t Hmi_PopUiEvent(uint16_t *value);
/* Last page written with Hmi_SendPage and how many page writes succeeded. */
uint16_t Hmi_GetLastPage(uint32_t *write_count);

uint8_t Hmi_SendWord(uint16_t vp, uint16_t value);
/* Fixed-size ASCII text field, 2..128 even bytes, including FF FF terminator. */
uint8_t Hmi_SendText(uint16_t vp, const char *text, uint16_t field_bytes);
uint8_t Hmi_SendPage(uint16_t page);
uint8_t Hmi_SendQuantity(uint16_t quantity);
uint8_t Hmi_SendIndicators(uint16_t conveyor, uint16_t gate, uint16_t scan, uint16_t auto_mode,
                           uint16_t semi_mode);

/* Called by USART1_IRQHandler.  HAL completion/error callbacks are owned by
 * this module and ignore UART handles other than huart1. */
void Hmi_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* HMI_H */
