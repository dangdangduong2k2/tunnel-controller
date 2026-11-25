#ifndef TUNNEL_H
#define TUNNEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    TUNNEL_WAIT_SS1_LOW = 0U,
    TUNNEL_CONVEYOR_TO_SS1_HIGH,
    TUNNEL_CLOSE_STEP_TO_SS2,
    TUNNEL_WAIT_READER,
    TUNNEL_CONVEYOR_INVENTORY_TO_SS3,
    TUNNEL_DISPLAY_RESULT,
    TUNNEL_OPEN_STEP,
    TUNNEL_WAIT_REARM,
    TUNNEL_FAULT,
    TUNNEL_WAIT_CONFIRM,
    TUNNEL_RESCAN_TO_SS1,
    TUNNEL_RESCAN_WAIT_READER,
    TUNNEL_RESCAN_TO_SS3,
    TUNNEL_RETURN_GATE_TO_SS2,
    TUNNEL_STOPPED,
    TUNNEL_MANUAL,
    TUNNEL_MODE_OPEN_GATE,
    TUNNEL_STARTUP_WAIT,
    /* IDs 18-22 retired in v1.3; retained for debugger compatibility only. */
    TUNNEL_CENTER_REVERSE_WAIT,
    TUNNEL_CENTER_REVERSE,
    TUNNEL_CENTER_INVENTORY,
    TUNNEL_CENTER_FORWARD_TO_SS3,
    TUNNEL_SS3_TIMEOUT_DRAIN,
    TUNNEL_WAIT_INVENTORY_START
} TunnelState;

typedef enum { TUNNEL_MODE_AUTO = 0U, TUNNEL_MODE_SEMI_AUTO } TunnelMode;

typedef enum {
    TUNNEL_FAULT_NONE = 0U,
    TUNNEL_FAULT_MOTOR_START,
    TUNNEL_FAULT_STEP_CLOSE_TIMEOUT,
    TUNNEL_FAULT_STEP_OPEN_COUNT,
    TUNNEL_FAULT_STEP_OPEN_TIMEOUT,
    TUNNEL_FAULT_CONVEYOR_TIMEOUT,
    TUNNEL_FAULT_READER_NOT_READY,
    TUNNEL_FAULT_SCAN_ERROR,
    TUNNEL_FAULT_EPC_CAPACITY,
    TUNNEL_FAULT_HMI,
    TUNNEL_FAULT_SENSOR_ORDER,
    TUNNEL_FAULT_INVALID_STATE,
    TUNNEL_FAULT_RESCAN_TIMEOUT,
    TUNNEL_FAULT_HOME_LIMIT
} TunnelFault;

void Tunnel_Init(void);
/* Separate-button panel migration: legacy value 1 becomes STOP-only, never START. */
void Tunnel_SetHmiScanExplicit(uint8_t enabled);
void Tunnel_Process(void);
/* Short ISR-safe motor cutoff; called after HAL_IncTick, no UART/USB work. */
void Tunnel_Tick1ms(void);
TunnelState Tunnel_GetState(void);
TunnelFault Tunnel_GetFault(void);
TunnelMode Tunnel_GetMode(void);
uint8_t Tunnel_RemoteCommand(uint8_t command, uint16_t value);
void Tunnel_PcHeartbeat(void);
void Tunnel_StopForSafety(uint8_t reason);
uint8_t Tunnel_GetStopFlags(void);
uint32_t Tunnel_GetPcAge(void);
uint8_t Tunnel_IsPcMaintenance(void);
uint8_t Tunnel_GetSpeedLevel(void);
uint16_t Tunnel_GetConveyorArr(void);
uint32_t Tunnel_GetScanDurationMs(void);
uint32_t Tunnel_GetActiveScanDurationMs(void);
uint32_t Tunnel_GetScanElapsedMs(void);
/* Result observer, default none. Called on the control task exactly once per
 * finalized result: SEMI AUTO when the result is confirmed (DWIN or remote)
 * and the gate opening has started, AUTO when the gate has opened after a
 * cycle without a recorded fault. Rescan, manual scan, faults and stops never
 * call it. cycle_token is the entry tick of the state being finalized, so a
 * repeated report of the same result carries the same token. The hook must
 * return quickly: no flash, UART or network work. */
typedef void (*TunnelFinalizeHook)(uint8_t semi, uint16_t quantity, uint32_t cycle_token);
void Tunnel_SetFinalizeHook(TunnelFinalizeHook hook);

#ifdef __cplusplus
}
#endif

#endif /* TUNNEL_H */
