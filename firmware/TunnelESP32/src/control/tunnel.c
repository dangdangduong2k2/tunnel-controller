#include "../common/ProtocolLimits.h"
#include "tunnel.h"
#include "../config/app_config.h"
#include "../platform/compat/board.h"
#include "../common/debug_state.h"
#include "../hmi/hmi.h"
#include "motor_pwm.h"
#include "speed_store.h"
#include "../reader/zk7182.h"
#include "../platform/compat/stm32f4xx_hal.h"
#ifdef ARDUINO
#include "../platform/esp32/EspPlatform.h"
#endif

#define TUNNEL_PROCESS_PERIOD_MS 5UL

typedef enum {
    TUNNEL_SENSOR_SS1 = 0U,
    TUNNEL_SENSOR_SS2,
    TUNNEL_SENSOR_SS3,
    TUNNEL_SENSOR_SS7,
    TUNNEL_SENSOR_COUNT
} TunnelSensorIndex;

typedef struct {
    uint8_t raw_active;
    uint8_t stable_active;
    uint32_t raw_changed_tick;
} TunnelSensorFilter;

static volatile TunnelState tunnel_state;
static TunnelFault tunnel_fault;
static TunnelSensorFilter sensor_filter[TUNNEL_SENSOR_COUNT];
static uint32_t tunnel_last_tick;
static volatile uint32_t state_entry_tick;
static uint32_t hmi_retry_tick;
static uint32_t rearm_clear_tick;
static uint16_t display_quantity;
static volatile uint16_t ss3_cutoff_quantity;
static uint8_t hmi_attempted;
static uint8_t rearm_clear_timing;
static volatile uint8_t motion_paused;
static volatile uint8_t ss3_cutoff_valid;
static uint8_t inventory_scan_failed;
static TunnelMode tunnel_mode;
static uint8_t cycle_semi;
static uint8_t rescan_in_progress;
static uint8_t reverse_started;
static uint8_t gate_started[2];
static volatile uint8_t gate_paused[2];
static const MotorPwmId gate_motors[2] = {STEP_DRIVER, STEP2_DRIVER};
static const TunnelSensorIndex gate_sensors[2] = {TUNNEL_SENSOR_SS2, TUNNEL_SENSOR_SS7};
static uint32_t home_motion_wait_tick;
static uint8_t gate_homed;
/* Confirmation belongs to this closing operation, never to the raw input.
 * IN7 can drop out after door 2 has stopped at a debounced closed limit. */
static volatile uint8_t gate2_closed_confirmed;
static uint8_t manual_scan_requested;
static uint8_t manual_scan_only;
static uint8_t rf_emergency_latched;
static volatile uint8_t manual_scan_started;
static uint8_t manual_scan_stopping;
static uint32_t manual_scan_stop_tick;
static uint32_t manual_scan_wait_tick;
static uint32_t manual_conveyor_tick;
static uint8_t hmi_conveyor_down;
static uint32_t hmi_status_tick;
static uint32_t hmi_result_refresh_tick;
static uint16_t hmi_last_quantity;
static uint16_t hmi_last_page;
static uint16_t hmi_last_indicators;
static uint8_t hmi_quantity_valid;
static uint8_t hmi_page_valid;
static uint8_t hmi_indicators_valid;
static uint8_t hmi_link_seen;
static volatile uint32_t first_travel_ms;
static volatile uint32_t first_segment_tick;
static volatile uint8_t first_segment_running;
static volatile uint8_t gate_interlock_latched;
static volatile uint8_t ss3_timeout_latched;
static volatile uint32_t ss3_timeout_tick;
static volatile uint32_t inventory_session_tick;
static uint32_t configured_scan_duration_ms;
static volatile uint32_t active_scan_duration_ms;
static uint8_t pc_maintenance;
static uint8_t emergency_latched;
static uint8_t conveyor_speed_level;
static const uint16_t conveyor_arr[5] = {41U, 29U, 19U, 9U, 4U};
uint8_t Tunnel_GetSpeedLevel(void) {
    return conveyor_speed_level;
}
uint16_t Tunnel_GetConveyorArr(void) {
    return conveyor_arr[conveyor_speed_level - 1U];
}
static volatile uint8_t pc_motion_owned;
static volatile uint8_t pc_lease_expired;
static uint8_t pc_supervised;
static volatile uint32_t pc_heartbeat_tick;
typedef enum {
    TUNNEL_HOME_NONE = 0U,
    TUNNEL_HOME_EMERGENCY,
    TUNNEL_HOME_RESET,
    TUNNEL_HOME_MANUAL
} TunnelHomeReason;
static TunnelHomeReason home_reason;

static const MotorProfile step_close_profile = {STEP_START_PERIOD_US, STEP_RUN_PERIOD_US,
                                                STEP_CLOSE_ACCEL_PULSES, STEP_CLOSE_CRUISE_PULSES,
                                                STEP_CLOSE_DECEL_PULSES};
static const MotorProfile step_open_profile = {STEP_START_PERIOD_US, STEP_RUN_PERIOD_US,
                                               STEP_OPEN_ACCEL_PULSES, STEP_OPEN_CRUISE_PULSES,
                                               STEP_OPEN_DECEL_PULSES};

static void Tunnel_StopInventoryAndDisplay(void);
static void Tunnel_StartOpeningStepState(TunnelState state);
static void Tunnel_BeginHome(TunnelHomeReason reason);
static void Tunnel_StartRescan(void);
static void Tunnel_HandleHmiEvents(void);
static void Tunnel_UpdateHmi(uint32_t now);

static uint8_t Tunnel_IsInventoryState(void) {
    return ((tunnel_state == TUNNEL_CONVEYOR_INVENTORY_TO_SS3) ||
            (tunnel_state == TUNNEL_RESCAN_TO_SS3))
               ? 1U
               : 0U;
}

static uint8_t Tunnel_IsFirstPass(void) {
    return ((tunnel_state == TUNNEL_CONVEYOR_INVENTORY_TO_SS3) ||
            (tunnel_state == TUNNEL_RESCAN_TO_SS3))
               ? 1U
               : 0U;
}

static uint8_t Tunnel_ManualAllowed(void) {
    return ((tunnel_state == TUNNEL_WAIT_SS1_LOW) || (tunnel_state == TUNNEL_WAIT_REARM) ||
            (tunnel_state == TUNNEL_MANUAL))
               ? 1U
               : 0U;
}

static uint8_t Tunnel_ReadSensor(TunnelSensorIndex sensor) {
    GPIO_TypeDef *port;
    uint16_t pin;

    switch (sensor) {
    case TUNNEL_SENSOR_SS1:
        port = SENSOR_SS1_PORT;
        pin = SENSOR_SS1_PIN;
        break;

    case TUNNEL_SENSOR_SS2:
        port = SENSOR_SS2_PORT;
        pin = SENSOR_SS2_PIN;
        break;

    case TUNNEL_SENSOR_SS3:
        port = SENSOR_SS3_PORT;
        pin = SENSOR_SS3_PIN;
        break;

    case TUNNEL_SENSOR_SS7:
        port = SENSOR_SS7_PORT;
        pin = SENSOR_SS7_PIN;
        break;

    default:
        return 0U;
    }

    return (HAL_GPIO_ReadPin(port, pin) == SENSOR_ACTIVE_LEVEL) ? 1U : 0U;
}

static uint8_t Tunnel_GatesRunning(void) {
    return (MotorPwm_IsRunning(STEP_DRIVER) || MotorPwm_IsRunning(STEP2_DRIVER)) ? 1U : 0U;
}
static void Tunnel_StopGates(void) {
    MotorPwm_Stop(STEP_DRIVER);
    MotorPwm_Stop(STEP2_DRIVER);
}
static uint8_t Tunnel_AnyGateRaw(void) {
    return (Tunnel_ReadSensor(TUNNEL_SENSOR_SS2) || Tunnel_ReadSensor(TUNNEL_SENSOR_SS7)) ? 1U : 0U;
}
static uint8_t Tunnel_GateClosedForInventory(void) {
    uint8_t i;
    for (i = 0U; i < 2U; ++i) {
        TunnelSensorIndex sensor = gate_sensors[i];
        if (MotorPwm_IsRunning(gate_motors[i])) {
            return 0U;
        }
        if ((i == 1U) && gate2_closed_confirmed) {
            continue;
        }
        if (!sensor_filter[sensor].stable_active || !sensor_filter[sensor].raw_active ||
            !Tunnel_ReadSensor(sensor)) {
            return 0U;
        }
    }
    return 1U;
}
/* Independent raw cutoffs. One door reaching its limit never stops the other. */
static void Tunnel_GateRawCutoffs(void) {
    uint8_t i;
    for (i = 0U; i < 2U; ++i) {
        if (Tunnel_ReadSensor(gate_sensors[i]) || ((i == 1U) && gate2_closed_confirmed)) {
            MotorPwm_Stop(gate_motors[i]);
            gate_paused[i] = 1U;
        }
    }
}

static uint8_t Tunnel_StartClosedGateScan(void) {
    uint32_t interrupt_state;
    uint8_t started;

    started = 0U;
    interrupt_state = __get_PRIMASK();
    __disable_irq();
    if (Tunnel_GateClosedForInventory() != 0U) {
        started = ZK7182_StartScan(configured_scan_duration_ms);
        if (started != 0U) {
            active_scan_duration_ms = configured_scan_duration_ms;
            inventory_session_tick = HAL_GetTick();
        }
    }
    if (interrupt_state == 0U) {
        __enable_irq();
    }
    return started;
}

static void Tunnel_SetState(TunnelState state) {
    uint32_t interrupt_state;

    interrupt_state = __get_PRIMASK();
    __disable_irq();
    tunnel_state = state;
    state_entry_tick = HAL_GetTick();
    motion_paused = 0U;
    ss3_cutoff_valid = 0U;
    hmi_attempted = 0U;
    rearm_clear_timing = 0U;
    g_debug.tunnel_state = (uint8_t)state;
    if (interrupt_state == 0U) {
        __enable_irq();
    }
}

static uint8_t Tunnel_StateTimedOut(uint32_t now, uint32_t timeout_ms) {
    return ((now - state_entry_tick) >= timeout_ms) ? 1U : 0U;
}

static uint32_t Tunnel_SensorDebounceMs(TunnelSensorIndex sensor) {
    return (sensor == TUNNEL_SENSOR_SS1) ? SENSOR_SS1_DEBOUNCE_MS : SENSOR_DEBOUNCE_MS;
}

static void Tunnel_UpdateSensor(TunnelSensorIndex sensor, uint32_t now) {
    uint8_t active;

    active = Tunnel_ReadSensor(sensor);
    if (active != sensor_filter[sensor].raw_active) {
        sensor_filter[sensor].raw_active = active;
        sensor_filter[sensor].raw_changed_tick = now;
    } else if ((active != sensor_filter[sensor].stable_active) &&
               ((now - sensor_filter[sensor].raw_changed_tick) >=
                Tunnel_SensorDebounceMs(sensor))) {
        sensor_filter[sensor].stable_active = active;
    }
}

static uint8_t Tunnel_RawStable(TunnelSensorIndex sensor, uint8_t required_active, uint32_t now) {
    return ((sensor_filter[sensor].raw_active == required_active) &&
            ((now - sensor_filter[sensor].raw_changed_tick) >= Tunnel_SensorDebounceMs(sensor)))
               ? 1U
               : 0U;
}

static void Tunnel_UpdateDebug(void) {
    g_debug.tunnel_state = (uint8_t)tunnel_state;
    g_debug.tunnel_fault = (uint8_t)tunnel_fault;
    g_debug.tunnel_ss1_active = sensor_filter[TUNNEL_SENSOR_SS1].stable_active;
    g_debug.tunnel_ss2_active = sensor_filter[TUNNEL_SENSOR_SS2].stable_active;
    g_debug.tunnel_ss3_active = sensor_filter[TUNNEL_SENSOR_SS3].stable_active;
    g_debug.tunnel_step_running = Tunnel_GatesRunning();
    g_debug.tunnel_conveyor_running = MotorPwm_IsRunning(CONVEYOR_DRIVER);
    g_debug.tunnel_step_pulse_count = MotorPwm_GetPulseCount(STEP_DRIVER);
    g_debug.tunnel_step_period_us = MotorPwm_GetPeriodUs(STEP_DRIVER);
    if ((tunnel_state == TUNNEL_CLOSE_STEP_TO_SS2) || (tunnel_state == TUNNEL_RETURN_GATE_TO_SS2)) {
        g_debug.tunnel_step_close_pulse_count = g_debug.tunnel_step_pulse_count;
    } else if ((tunnel_state == TUNNEL_OPEN_STEP) || (tunnel_state == TUNNEL_MODE_OPEN_GATE)) {
        g_debug.tunnel_step_open_pulse_count = g_debug.tunnel_step_pulse_count;
    }
    g_debug.tunnel_display_quantity = display_quantity;
    g_debug.tunnel_mode = (uint8_t)tunnel_mode;
    g_debug.tunnel_cycle_semi = cycle_semi;
    g_debug.tunnel_gate_homed = gate_homed;
    g_debug.tunnel_home_reason = (uint8_t)home_reason;
    g_debug.tunnel_manual_scan_requested = manual_scan_requested;
    g_debug.tunnel_first_travel_ms = first_travel_ms;
    g_debug.tunnel_center_reverse_target_ms = 0U;
}

static void Tunnel_ResetTravel(void) {
    first_segment_running = 0U;
    first_travel_ms = 0U;
    gate_interlock_latched = 0U;
    ss3_timeout_latched = 0U;
    ss3_timeout_tick = 0U;
    g_debug.tunnel_first_travel_ms = 0U;
    g_debug.tunnel_center_reverse_target_ms = 0U;
    g_debug.tunnel_center_reverse_actual_ms = 0U;
    g_debug.tunnel_center_hold_ms = 0U;
}

/* Caller serializes launch with EXTI. Track motor-on time, not wall time:
 * reader startup, SS3 debounce and rejected-bounce pauses do not add travel. */
static uint8_t Tunnel_StartTransferMotion(void) {
    uint8_t started;

    started =
        MotorPwm_StartContinuousArr(CONVEYOR_DRIVER, CONVEYOR_DIRECTION, Tunnel_GetConveyorArr());
    if ((started != 0U) && (Tunnel_IsFirstPass() != 0U)) {
        first_segment_tick = HAL_GetTick();
        first_segment_running = 1U;
    }
    return started;
}

static void Tunnel_StopTransferMotion(void) {
    uint32_t interrupt_state;
    uint32_t elapsed;
    uint32_t total;

    interrupt_state = __get_PRIMASK();
    __disable_irq();
    MotorPwm_Stop(CONVEYOR_DRIVER);
    if (first_segment_running != 0U) {
        elapsed = HAL_GetTick() - first_segment_tick;
        total = first_travel_ms;
        first_travel_ms = total + elapsed;
        first_segment_running = 0U;
    }
    if (interrupt_state == 0U) {
        __enable_irq();
    }
}

static void Tunnel_StopAtSs3(void) {
    Tunnel_StopTransferMotion();
    /* Every SS3 edge stops motion and requests RF STOP. */
    ZK7182_PauseScan();
    motion_paused = 1U;
}

static void Tunnel_EnterFault(TunnelFault fault) {
    gate2_closed_confirmed = 0U;
    Tunnel_StopGates();
    Tunnel_StopTransferMotion();
    ZK7182_CancelScan();
    manual_scan_requested = 0U;
    manual_scan_started = 0U;
    manual_scan_stopping = 0U;
    Hmi_FlushEvents();
    tunnel_fault = fault;
    Tunnel_SetState(TUNNEL_FAULT);
    g_debug.tunnel_fault = (uint8_t)fault;
}

static void Tunnel_StartFirstConveyor(void) {
    uint32_t interrupt_state;
    uint8_t sensor_order_ok;
    uint8_t started;

    sensor_order_ok = 1U;
    started = 1U;
    Tunnel_ResetTravel();
    interrupt_state = __get_PRIMASK();
    __disable_irq();
    if ((Tunnel_AnyGateRaw() != 0U) || (Tunnel_ReadSensor(TUNNEL_SENSOR_SS3) != 0U)) {
        sensor_order_ok = 0U;
    } else {
        gate2_closed_confirmed = 0U;
        Tunnel_SetState(TUNNEL_CONVEYOR_TO_SS1_HIGH);
        cycle_semi = (tunnel_mode == TUNNEL_MODE_SEMI_AUTO) ? 1U : 0U;
        rescan_in_progress = 0U;
        if (Tunnel_ReadSensor(TUNNEL_SENSOR_SS1) != 0U) {
            started = MotorPwm_StartContinuousArr(CONVEYOR_DRIVER, CONVEYOR_DIRECTION,
                                                  Tunnel_GetConveyorArr());
        } else {
            /* SS1 already released during the transition.  Do not emit another
               conveyor pulse; the debounced state will start the closing step. */
            motion_paused = 1U;
        }
    }
    if (interrupt_state == 0U) {
        __enable_irq();
    }

    if (sensor_order_ok == 0U) {
        Tunnel_EnterFault(TUNNEL_FAULT_SENSOR_ORDER);
    } else if (started == 0U) {
        Tunnel_EnterFault(TUNNEL_FAULT_MOTOR_START);
    }
}

static void Tunnel_StartClosingStep(void) {
    uint8_t i;
    gate2_closed_confirmed = 0U;
    MotorPwm_Stop(CONVEYOR_DRIVER);
    if (Tunnel_AnyGateRaw()) {
        Tunnel_EnterFault(TUNNEL_FAULT_SENSOR_ORDER);
        return;
    }
    Tunnel_SetState(TUNNEL_CLOSE_STEP_TO_SS2);
    gate_homed = 0U;
    g_debug.tunnel_step_close_pulse_count = 0U;
    g_debug.tunnel_step_open_pulse_count = 0U;
    for (i = 0U; i < 2U; ++i) {
        TunnelSensorIndex sensor = gate_sensors[i];
        sensor_filter[sensor].raw_active = 0U;
        sensor_filter[sensor].stable_active = 0U;
        sensor_filter[sensor].raw_changed_tick = HAL_GetTick();
        gate_started[i] = 1U;
        gate_paused[i] = 0U;
        if (!MotorPwm_StartProfile(gate_motors[i], STEP_CLOSE_DIRECTION, &step_close_profile, 0U)) {
            Tunnel_EnterFault(TUNNEL_FAULT_MOTOR_START);
            return;
        }
    }
    Tunnel_GateRawCutoffs();
}

static void Tunnel_StartInventoryTransfer(void) {
    uint32_t interrupt_state;
    uint8_t ss3_already_active;

    ss3_already_active = 0U;
    interrupt_state = __get_PRIMASK();
    __disable_irq();
    if (Tunnel_ReadSensor(TUNNEL_SENSOR_SS3) != 0U) {
        ss3_already_active = 1U;
    } else {
        /* Arm a fresh LOW for the first pass measurement. */
        sensor_filter[TUNNEL_SENSOR_SS3].raw_active = 0U;
        sensor_filter[TUNNEL_SENSOR_SS3].stable_active = 0U;
        sensor_filter[TUNNEL_SENSOR_SS3].raw_changed_tick = HAL_GetTick();
    }
    if (interrupt_state == 0U) {
        __enable_irq();
    }
    if (ss3_already_active != 0U) {
        /* Do not move into an already active exit sensor. */
        Tunnel_EnterFault(TUNNEL_FAULT_SENSOR_ORDER);
        return;
    }

    Tunnel_ResetTravel();
    if (Tunnel_StartClosedGateScan() == 0U) {
        tunnel_fault = (Tunnel_GateClosedForInventory() == 0U) ? TUNNEL_FAULT_SENSOR_ORDER
                                                               : TUNNEL_FAULT_READER_NOT_READY;
        g_debug.tunnel_fault = (uint8_t)tunnel_fault;
        return;
    }
    inventory_scan_failed = 0U;
    tunnel_fault = TUNNEL_FAULT_NONE;

    Tunnel_SetState(TUNNEL_WAIT_INVENTORY_START);
}

/* RF must be acknowledged (or proven by tag data) before moving the parcel. */
static void Tunnel_ProcessInventoryStart(uint32_t now) {
    uint32_t interrupt_state;
    uint8_t started = 1U;
    if ((Tunnel_GateClosedForInventory() == 0U) || (Tunnel_ReadSensor(TUNNEL_SENSOR_SS3) != 0U)) {
        Tunnel_EnterFault(TUNNEL_FAULT_SENSOR_ORDER);
        return;
    }
    if ((ZK7182_IsScanComplete() != 0U) ||
        (Tunnel_StateTimedOut(now, TUNNEL_READER_READY_TIMEOUT_MS) != 0U)) {
        Tunnel_EnterFault(TUNNEL_FAULT_SCAN_ERROR);
        return;
    }
    if (ZK7182_IsInventoryRunning() == 0U) {
        return;
    }
    interrupt_state = __get_PRIMASK();
    __disable_irq();
    Tunnel_SetState((rescan_in_progress != 0U) ? TUNNEL_RESCAN_TO_SS3
                                               : TUNNEL_CONVEYOR_INVENTORY_TO_SS3);
    if ((Tunnel_ReadSensor(TUNNEL_SENSOR_SS3) == 0U) && (Tunnel_GateClosedForInventory() != 0U)) {
        started = Tunnel_StartTransferMotion();
    } else {
        Tunnel_StopAtSs3();
    }
    if (interrupt_state == 0U) {
        __enable_irq();
    }
    if (started == 0U) {
        Tunnel_EnterFault(TUNNEL_FAULT_MOTOR_START);
    }
}

static void Tunnel_StopInventoryAndDisplay(void) {
    Tunnel_StopTransferMotion();
    if (ss3_cutoff_valid != 0U) {
        display_quantity = ss3_cutoff_quantity;
    } else {
        display_quantity = ZK7182_GetUniqueEpcCount();
    }
    ZK7182_CancelScanAtUniqueCount(display_quantity);
    g_debug.tunnel_display_quantity = display_quantity;
    hmi_retry_tick = HAL_GetTick();
    Tunnel_SetState(TUNNEL_DISPLAY_RESULT);
}

/* No scan could start: end the wait with zero for THIS attempt, never a
 * stale quantity from the previous parcel. Existing AUTO/SEMI result handling
 * owns the opening sequence. Mechanical interlocks still take precedence. */
static void Tunnel_FinishUnavailableReader(void) {
    if (Tunnel_GateClosedForInventory() == 0U) {
        Tunnel_EnterFault(TUNNEL_FAULT_SENSOR_ORDER);
        return;
    }
    tunnel_fault = TUNNEL_FAULT_READER_NOT_READY;
    g_debug.tunnel_fault = (uint8_t)tunnel_fault;
    ss3_cutoff_quantity = 0U;
    ss3_cutoff_valid = 1U;
    Tunnel_StopInventoryAndDisplay();
}

static void Tunnel_StartOpeningStepState(TunnelState state) {
    uint32_t interrupt_state;
    uint8_t started;

    interrupt_state = __get_PRIMASK();
    __disable_irq();
    Tunnel_SetState(state);
    gate_homed = 0U;
    gate2_closed_confirmed = 0U;
    g_debug.tunnel_step_open_pulse_count = 0U;
    started = MotorPwm_StartProfile(STEP_DRIVER, STEP_OPEN_DIRECTION, &step_open_profile, 0U);
    if (started != 0U) {
        started = MotorPwm_StartProfile(STEP2_DRIVER, STEP_OPEN_DIRECTION, &step_open_profile, 0U);
    }
    if (interrupt_state == 0U) {
        __enable_irq();
    }

    if (started == 0U) {
        Tunnel_EnterFault(TUNNEL_FAULT_MOTOR_START);
    }
}

static void Tunnel_ProcessConveyorToSs1(uint32_t now) {
    uint32_t interrupt_state;
    uint8_t resume_attempted;
    uint8_t started;

    resume_attempted = 0U;
    started = 1U;
    if (Tunnel_StateTimedOut(now, TUNNEL_CONVEYOR_TIMEOUT_MS) != 0U) {
        Tunnel_EnterFault(TUNNEL_FAULT_CONVEYOR_TIMEOUT);
        return;
    }

    if (sensor_filter[TUNNEL_SENSOR_SS1].stable_active == 0U) {
        MotorPwm_Stop(CONVEYOR_DRIVER);
        Tunnel_StartClosingStep();
        return;
    }

    if ((motion_paused != 0U) && (Tunnel_RawStable(TUNNEL_SENSOR_SS1, 1U, now) != 0U)) {
        interrupt_state = __get_PRIMASK();
        __disable_irq();
        if ((tunnel_state == TUNNEL_CONVEYOR_TO_SS1_HIGH) && (motion_paused != 0U) &&
            (Tunnel_ReadSensor(TUNNEL_SENSOR_SS1) != 0U)) {
            resume_attempted = 1U;
            started = MotorPwm_StartContinuousArr(CONVEYOR_DRIVER, CONVEYOR_DIRECTION,
                                                  Tunnel_GetConveyorArr());
            if ((started != 0U) && (Tunnel_ReadSensor(TUNNEL_SENSOR_SS1) != 0U)) {
                motion_paused = 0U;
            } else if (started != 0U) {
                MotorPwm_Stop(CONVEYOR_DRIVER);
                motion_paused = 1U;
            }
        }
        if (interrupt_state == 0U) {
            __enable_irq();
        }
        if ((resume_attempted != 0U) && (started == 0U)) {
            Tunnel_EnterFault(TUNNEL_FAULT_MOTOR_START);
            return;
        }
    } else if ((motion_paused == 0U) && (MotorPwm_IsRunning(CONVEYOR_DRIVER) == 0U)) {
        Tunnel_EnterFault(TUNNEL_FAULT_MOTOR_START);
    }
}

/* IN2 must remain physically closed. Once IN7 confirms door 2 for this
 * closing operation, a dropout must never restart its closing profile. */
static uint8_t Tunnel_ProcessGateClose(uint32_t now, uint8_t homing) {
    uint8_t i;
    if (Tunnel_StateTimedOut(now, STEP_CLOSE_TIMEOUT_MS)) {
        Tunnel_EnterFault(homing ? TUNNEL_FAULT_HOME_LIMIT : TUNNEL_FAULT_STEP_CLOSE_TIMEOUT);
        return 0U;
    }
    Tunnel_GateRawCutoffs();
    if (sensor_filter[TUNNEL_SENSOR_SS7].stable_active &&
        Tunnel_RawStable(TUNNEL_SENSOR_SS7, 1U, now) && Tunnel_ReadSensor(TUNNEL_SENSOR_SS7) &&
        !MotorPwm_IsRunning(STEP2_DRIVER)) {
        gate2_closed_confirmed = 1U;
    }
    if (Tunnel_GateClosedForInventory()) {
        return 1U;
    }
    if (homing && (now - home_motion_wait_tick) < DIRECTION_REVERSAL_DELAY_MS) {
        return 0U;
    }
    for (i = 0U; i < 2U; ++i) {
        MotorPwmId motor = gate_motors[i];
        TunnelSensorIndex sensor = gate_sensors[i];
        uint32_t completed = gate_started[i] ? MotorPwm_GetPulseCount(motor) : 0U;
        if (Tunnel_ReadSensor(sensor) || ((i == 1U) && gate2_closed_confirmed)) {
            continue;
        }
        if (gate_started[i] && completed >= STEP_CLOSE_PULSE_COUNT) {
            if (homing) {
                Tunnel_EnterFault(TUNNEL_FAULT_HOME_LIMIT);
                return 0U;
            }
            continue; /* Bounded pulse budget; normal close waits only until its deadline. */
        }
        if (gate_started[i] && !gate_paused[i]) {
            if (!MotorPwm_IsRunning(motor)) {
                Tunnel_EnterFault(TUNNEL_FAULT_MOTOR_START);
                return 0U;
            }
            continue;
        }
        if (gate_paused[i] && !Tunnel_RawStable(sensor, 0U, now)) {
            continue;
        }
        if (!MotorPwm_StartProfile(motor, STEP_CLOSE_DIRECTION, &step_close_profile, completed)) {
            Tunnel_EnterFault(TUNNEL_FAULT_MOTOR_START);
            return 0U;
        }
        gate_started[i] = 1U;
        gate_paused[i] = 0U;
        if (Tunnel_ReadSensor(sensor)) {
            MotorPwm_Stop(motor);
            gate_paused[i] = 1U;
        }
    }
    return 0U;
}

static void Tunnel_ProcessClosingStep(uint32_t now) {
    if (Tunnel_ProcessGateClose(now, 0U)) {
        g_debug.tunnel_step_close_pulse_count = MotorPwm_GetPulseCount(STEP_DRIVER);
        gate_homed = 1U;
        Tunnel_SetState(TUNNEL_WAIT_READER);
    }
}

static void Tunnel_ProcessInventoryTransfer(uint32_t now) {
    uint32_t interrupt_state;
    ZK7182_ScanResult result;
    uint8_t resume_attempted;
    uint8_t started;

    resume_attempted = 0U;
    started = 1U;
    if ((sensor_filter[TUNNEL_SENSOR_SS3].raw_active != 0U) && (motion_paused == 0U)) {
        /* Fallback if an EXTI edge was missed: stop no later than this poll. */
        Tunnel_StopAtSs3();
    }
    /* Preserve valid EPCs if the reader fails during this pass. */
    if ((inventory_scan_failed == 0U) && (ZK7182_IsScanComplete() != 0U)) {
        result = ZK7182_GetScanResult();
        tunnel_fault = (result == ZK7182_SCAN_RESULT_CAPACITY_OVERFLOW) ? TUNNEL_FAULT_EPC_CAPACITY
                                                                        : TUNNEL_FAULT_SCAN_ERROR;
        g_debug.tunnel_fault = (uint8_t)tunnel_fault;
        inventory_scan_failed = 1U;
        ZK7182_CancelScan();
    } else if ((inventory_scan_failed == 0U) && (ZK7182_IsScanActive() == 0U)) {
        tunnel_fault = TUNNEL_FAULT_SCAN_ERROR;
        g_debug.tunnel_fault = (uint8_t)tunnel_fault;
        inventory_scan_failed = 1U;
        ZK7182_CancelScan();
    }

    if (sensor_filter[TUNNEL_SENSOR_SS3].stable_active != 0U) {
        if ((ZK7182_IsInventoryResponsePending() != 0U) &&
            ((now - sensor_filter[TUNNEL_SENSOR_SS3].raw_changed_tick) <
             TUNNEL_INVENTORY_DRAIN_TIMEOUT_MS)) {
            return;
        }
        if (ZK7182_IsInventoryResponsePending() != 0U) {
            /* Never hold the machine forever if the reader never terminates the
               in-flight transaction.  Keep every EPC validated before this limit. */
            g_debug.inventory_stop_drain_timeout_count++;
        }
        ss3_cutoff_quantity = ZK7182_GetUniqueEpcCount();
        ss3_cutoff_valid = 1U;
        Tunnel_StopInventoryAndDisplay();
        return;
    }

    if ((motion_paused != 0U) && (Tunnel_RawStable(TUNNEL_SENSOR_SS3, 0U, now) != 0U)) {
        interrupt_state = __get_PRIMASK();
        __disable_irq();
        if ((Tunnel_IsInventoryState() != 0U) && (motion_paused != 0U) &&
            (ss3_timeout_latched == 0U)) {
            /* A stable HIGH rejects the previous LOW as bounce.  Re-arm before
               reading the physical pin so a new LOW in this critical window is
               latched as a new stop episode. */
            ss3_cutoff_valid = 0U;
            if (Tunnel_ReadSensor(TUNNEL_SENSOR_SS3) == 0U) {
                resume_attempted = 1U;
                ZK7182_ResumeScan();
                started = Tunnel_StartTransferMotion();
                if ((started != 0U) && (Tunnel_ReadSensor(TUNNEL_SENSOR_SS3) == 0U)) {
                    motion_paused = 0U;
                } else if (started != 0U) {
                    Tunnel_StopAtSs3();
                }
            } else {
                /* SS3 became LOW after the HIGH debounce but before restart. */
                Tunnel_StopAtSs3();
            }
        }
        if (interrupt_state == 0U) {
            __enable_irq();
        }
        if ((resume_attempted != 0U) && (started == 0U)) {
            /* A conveyor restart failure must not prevent a later SS3 LOW from
               publishing the current quantity and opening the step mechanism. */
            tunnel_fault = TUNNEL_FAULT_MOTOR_START;
            g_debug.tunnel_fault = (uint8_t)tunnel_fault;
        }
    }

    if ((motion_paused == 0U) && (MotorPwm_IsRunning(CONVEYOR_DRIVER) == 0U) &&
        (tunnel_fault == TUNNEL_FAULT_NONE)) {
        /* Retain diagnostics. An invalid first pass cannot authorize reverse;
           on the final pass a physical SS3 still allows publishing the result. */
        tunnel_fault = TUNNEL_FAULT_MOTOR_START;
        g_debug.tunnel_fault = (uint8_t)tunnel_fault;
    }
}

static void Tunnel_BeginHome(TunnelHomeReason reason) {
    uint32_t interrupt_state;

    /* Only explicit Reset can re-arm after Emergency. */
    if (emergency_latched != 0U) {
        if (reason != TUNNEL_HOME_RESET) {
            return;
        }
        MotorPwm_Init();
        emergency_latched = 0U;
        rf_emergency_latched = 0U;
    }
    interrupt_state = __get_PRIMASK();
    __disable_irq();
    MotorPwm_Stop(CONVEYOR_DRIVER);
    Tunnel_StopGates();
    ZK7182_PauseScan();
    if (interrupt_state == 0U) {
        __enable_irq();
    }
    ZK7182_CancelScan();
    Tunnel_ResetTravel();
    Hmi_FlushEvents();
    home_motion_wait_tick = HAL_GetTick();
    if (tunnel_state == TUNNEL_RETURN_GATE_TO_SS2) {
        /* Repeated recovery commands stop immediately, but must not grant a new
           6000-pulse budget or restart the 30-second missing-limit deadline. */
        gate_paused[0] = gate_paused[1] = 1U;
        if ((reason == TUNNEL_HOME_EMERGENCY) || (home_reason != TUNNEL_HOME_EMERGENCY)) {
            home_reason = reason;
        }
        return;
    }
    manual_scan_requested = 0U;
    manual_scan_started = 0U;
    manual_scan_stopping = 0U;
    inventory_scan_failed = 0U;
    rescan_in_progress = 0U;
    gate_started[0] = gate_started[1] = 0U;
    gate_paused[0] = gate_paused[1] = 0U;
    gate_homed = 0U;
    gate2_closed_confirmed = 0U;
    home_reason = reason;
    tunnel_fault = TUNNEL_FAULT_NONE;
    {
        uint8_t i;
        for (i = 0U; i < 2U; ++i) {
            TunnelSensorIndex sensor = gate_sensors[i];
            sensor_filter[sensor].raw_active = Tunnel_ReadSensor(sensor);
            sensor_filter[sensor].stable_active = 0U;
            sensor_filter[sensor].raw_changed_tick = HAL_GetTick();
        }
    }
    Tunnel_SetState(TUNNEL_RETURN_GATE_TO_SS2);
}

static void Tunnel_ProcessHome(uint32_t now) {
    if (Tunnel_ProcessGateClose(now, 1U)) {
        gate_homed = 1U;
        g_debug.tunnel_step_close_pulse_count =
            gate_started[0] ? MotorPwm_GetPulseCount(STEP_DRIVER) : 0U;
        Hmi_FlushEvents();
        Tunnel_SetState((home_reason == TUNNEL_HOME_MANUAL) ? TUNNEL_MANUAL : TUNNEL_STOPPED);
    }
}

static void Tunnel_StartRescan(void) {
    MotorPwm_Stop(CONVEYOR_DRIVER);
    Tunnel_StopGates();
    Tunnel_ResetTravel();
    Hmi_FlushEvents();
    display_quantity = 0U;
    inventory_scan_failed = 0U;
    tunnel_fault = TUNNEL_FAULT_NONE;
    rescan_in_progress = 1U;
    reverse_started = 0U;
    g_debug.tunnel_rescan_count++;
    sensor_filter[TUNNEL_SENSOR_SS1].raw_active = Tunnel_ReadSensor(TUNNEL_SENSOR_SS1);
    sensor_filter[TUNNEL_SENSOR_SS1].stable_active = 0U;
    sensor_filter[TUNNEL_SENSOR_SS1].raw_changed_tick = HAL_GetTick();
    Tunnel_SetState(TUNNEL_RESCAN_TO_SS1);
}

static void Tunnel_ProcessRescanReverse(uint32_t now) {
    uint32_t interrupt_state;
    uint8_t started;

    if (Tunnel_StateTimedOut(now, TUNNEL_CONVEYOR_TIMEOUT_MS) != 0U) {
        Tunnel_EnterFault(TUNNEL_FAULT_RESCAN_TIMEOUT);
        return;
    }
    if (Tunnel_ReadSensor(TUNNEL_SENSOR_SS1) != 0U) {
        MotorPwm_Stop(CONVEYOR_DRIVER);
        motion_paused = 1U;
        if (Tunnel_RawStable(TUNNEL_SENSOR_SS1, 1U, now) != 0U) {
            Tunnel_SetState(TUNNEL_RESCAN_WAIT_READER);
        }
        return;
    }
    if ((now - state_entry_tick) < DIRECTION_REVERSAL_DELAY_MS) {
        return;
    }
    if ((reverse_started != 0U) && (motion_paused == 0U)) {
        if (MotorPwm_IsRunning(CONVEYOR_DRIVER) == 0U) {
            Tunnel_EnterFault(TUNNEL_FAULT_MOTOR_START);
        }
        return;
    }
    if ((motion_paused != 0U) && (Tunnel_RawStable(TUNNEL_SENSOR_SS1, 0U, now) == 0U)) {
        return;
    }

    started = 1U;
    interrupt_state = __get_PRIMASK();
    __disable_irq();
    if (Tunnel_ReadSensor(TUNNEL_SENSOR_SS1) == 0U) {
        started = MotorPwm_StartContinuousArr(CONVEYOR_DRIVER, CONVEYOR_REVERSE_DIRECTION,
                                              Tunnel_GetConveyorArr());
        reverse_started = 1U;
        motion_paused = 0U;
        if (Tunnel_ReadSensor(TUNNEL_SENSOR_SS1) != 0U) {
            MotorPwm_Stop(CONVEYOR_DRIVER);
            motion_paused = 1U;
        }
    } else {
        motion_paused = 1U;
    }
    if (interrupt_state == 0U) {
        __enable_irq();
    }
    if (started == 0U) {
        Tunnel_EnterFault(TUNNEL_FAULT_MOTOR_START);
    }
}

static void Tunnel_ProcessRescanWait(uint32_t now) {
    if (Tunnel_StateTimedOut(now, TUNNEL_READER_READY_TIMEOUT_MS) != 0U) {
        if (Tunnel_ReadSensor(TUNNEL_SENSOR_SS3) != 0U) {
            Tunnel_EnterFault(TUNNEL_FAULT_SENSOR_ORDER);
        } else {
            Tunnel_FinishUnavailableReader();
        }
        return;
    }
    /* The previous SS3 LOW is irrelevant on the reverse trip. Before the new
       forward trip require it to have cleared, then arm a fresh LOW edge. */
    if (((now - state_entry_tick) >= DIRECTION_REVERSAL_DELAY_MS) &&
        (Tunnel_RawStable(TUNNEL_SENSOR_SS3, 0U, now) != 0U) &&
        (Tunnel_ReadSensor(TUNNEL_SENSOR_SS3) == 0U) && (ZK7182_IsReady() != 0U)) {
        Tunnel_StartInventoryTransfer();
    }
}

/* Explicit stationary RF scan; never homes or starts a motor. Automatic parcel
 * inventory still uses Tunnel_StartClosedGateScan and its door interlocks. */
static uint8_t Tunnel_RequestManualScan(uint16_t run) {
    uint8_t i;
#ifdef ARDUINO
    uint32_t rf_token = EspRfStartToken();
#endif
    if (run > 1U)
        return 1U;
    if (!run) {
        if (manual_scan_only) {
#ifdef ARDUINO
            EspRfRevoke();
#endif
            manual_scan_requested = 0U;
            ZK7182_PauseScan();
        } else if (ZK7182_IsScanActive()) {
            return 2U; /* OP4 must not take over an automatic parcel scan. */
        }
        return 0U;
    }
    if (pc_maintenance || rf_emergency_latched || tunnel_fault != TUNNEL_FAULT_NONE)
        return 2U;
#ifdef ARDUINO
    if (EspEmergencyHeld())
        return 3U;
#endif
    if (!Tunnel_ManualAllowed() && tunnel_state != TUNNEL_STOPPED &&
        tunnel_state != TUNNEL_STARTUP_WAIT)
        return 2U;
    for (i = 0U; i < MOTOR_PWM_DRIVER_COUNT; ++i)
        if (MotorPwm_IsRunning((MotorPwmId)i))
            return 2U;
    if (manual_scan_only)
        return (manual_scan_requested && !manual_scan_stopping) ? 0U : 2U;
    if (!ZK7182_IsReady())
        return 4U;
#ifdef ARDUINO
    if (!EspRfAuthorizeStandalone(rf_token))
        return 2U; /* A concurrent STOP must win over this request. */
#endif
    manual_scan_only = 1U;
    manual_scan_requested = 1U;
    manual_scan_wait_tick = HAL_GetTick();
    Tunnel_SetState(TUNNEL_MANUAL);
    return 0U;
}

static void Tunnel_ProcessManual(uint32_t now) {
    if ((MotorPwm_IsRunning(CONVEYOR_DRIVER) != 0U) &&
        ((Tunnel_ReadSensor(TUNNEL_SENSOR_SS3) != 0U) ||
         ((now - manual_conveyor_tick) >= TUNNEL_CONVEYOR_TIMEOUT_MS))) {
        MotorPwm_Stop(CONVEYOR_DRIVER);
    }
    if ((manual_scan_requested != 0U) && (manual_scan_started == 0U)) {
        if ((ZK7182_IsReady() != 0U) &&
            (ZK7182_StartScan(configured_scan_duration_ms) != 0U)) {
            active_scan_duration_ms = configured_scan_duration_ms;
            inventory_session_tick = HAL_GetTick();
            manual_scan_started = 1U;
            display_quantity = 0U;
        } else if ((now - manual_scan_wait_tick) >= TUNNEL_READER_READY_TIMEOUT_MS) {
            manual_scan_requested = 0U;
            tunnel_fault = TUNNEL_FAULT_READER_NOT_READY;
        }
    }
    if (manual_scan_started != 0U) {
        display_quantity = ZK7182_GetUniqueEpcCount();
        if ((now - inventory_session_tick) >= active_scan_duration_ms) {
            manual_scan_requested = 0U;
        }
        if ((ZK7182_IsScanComplete() != 0U) || (ZK7182_IsScanActive() == 0U)) {
            manual_scan_requested = 0U;
        }
        if ((manual_scan_requested == 0U) && (manual_scan_stopping == 0U)) {
            ZK7182_PauseScan();
            manual_scan_stopping = 1U;
            manual_scan_stop_tick = now;
        }
        if ((manual_scan_stopping != 0U) &&
            ((ZK7182_IsInventoryResponsePending() == 0U) ||
             ((now - inventory_session_tick) >= active_scan_duration_ms) ||
             ((now - manual_scan_stop_tick) >= TUNNEL_INVENTORY_DRAIN_TIMEOUT_MS))) {
            display_quantity = ZK7182_GetUniqueEpcCount();
            ZK7182_CancelScanAtUniqueCount(display_quantity);
            manual_scan_started = 0U;
            manual_scan_stopping = 0U;
            /* A fresh ON queued during drain starts a new session, never mixes EPCs. */
            manual_scan_wait_tick = now;
        }
    }
    if ((manual_scan_requested == 0U) && (manual_scan_started == 0U) &&
        (MotorPwm_IsRunning(CONVEYOR_DRIVER) == 0U) && (Tunnel_GatesRunning() == 0U)) {
        /* Finishing an isolated read must not arm AUTO on a sensor edge. */
        Tunnel_SetState(manual_scan_only ? TUNNEL_STOPPED : TUNNEL_WAIT_REARM);
#ifdef ARDUINO
        if (manual_scan_only)
            EspRfRevoke();
#endif
        manual_scan_only = 0U;
    }
}

static void Tunnel_HandleHmiEvents(void) {
    HmiEvent event;
    uint32_t interrupt_state;
    uint8_t started;

    while (Hmi_PopEvent(&event) != 0U) {
        if ((event.vp == HMI_VP_BUTTON_CONVEYOR) && (event.value <= 1U)) {
            /* The existing DWIN bit button is mode 0x03: touch=1, release=0.
             * Toggle once per fresh touch. Track even rejected touches so holding
             * a button across a busy/Reset transition cannot start the conveyor. */
            uint8_t fresh_press = (uint8_t)((event.value != 0U) && (hmi_conveyor_down == 0U));
            hmi_conveyor_down = (uint8_t)event.value;
            if (fresh_press == 0U) {
                continue;
            }
        }
        if (manual_scan_only && event.vp != HMI_VP_BUTTON_SCAN &&
            event.vp != HMI_VP_BUTTON_EMERGENCY) {
            g_debug.tunnel_hmi_ignored_count++;
            continue;
        }
        if ((event.vp == HMI_VP_BUTTON_RESET) || (event.vp == HMI_VP_BUTTON_EMERGENCY)) {
            pc_maintenance = 0U;
            pc_motion_owned = 0U;
        } else if (pc_maintenance != 0U) {
            g_debug.tunnel_hmi_ignored_count++;
            continue;
        }
        if ((event.vp == HMI_VP_BUTTON_EMERGENCY) && (event.value == 1U)) {
            (void)Tunnel_RemoteCommand(0x17U, 0U);
            return;
        }
        if ((event.vp == HMI_VP_BUTTON_RESET) && (event.value == 1U)) {
            Tunnel_BeginHome(TUNNEL_HOME_RESET);
            return;
        }
        if ((event.vp == HMI_VP_BUTTON_SCAN) && (event.value == 1U)) {
            if (Tunnel_RequestManualScan(manual_scan_requested ? 0U : 1U))
                g_debug.tunnel_hmi_ignored_count++;
            continue;
        }
        if (emergency_latched != 0U) {
            continue;
        }
        if (((event.vp == HMI_VP_BUTTON_AUTO) && (event.value <= 1U)) ||
            ((event.vp == HMI_VP_BUTTON_SEMI) && (event.value == 1U))) {
            if ((tunnel_state == TUNNEL_STOPPED) && (gate_homed != 0U)) {
                tunnel_mode =
                    (event.vp == HMI_VP_BUTTON_AUTO) ? TUNNEL_MODE_AUTO : TUNNEL_MODE_SEMI_AUTO;
                home_reason = TUNNEL_HOME_NONE;
                Hmi_FlushEvents();
                Tunnel_StartOpeningStepState(TUNNEL_MODE_OPEN_GATE);
                return;
            }
            if ((tunnel_state == TUNNEL_WAIT_SS1_LOW) || (tunnel_state == TUNNEL_WAIT_REARM)) {
                tunnel_mode =
                    (event.vp == HMI_VP_BUTTON_AUTO) ? TUNNEL_MODE_AUTO : TUNNEL_MODE_SEMI_AUTO;
            } else {
                g_debug.tunnel_hmi_ignored_count++;
            }
            continue;
        }
        if ((event.vp == HMI_VP_BUTTON_CONFIRM) && (event.value == 1U)) {
            if (tunnel_state == TUNNEL_WAIT_CONFIRM) {
                Hmi_FlushEvents();
                Tunnel_StartOpeningStepState(TUNNEL_OPEN_STEP);
                return;
            }
            g_debug.tunnel_hmi_ignored_count++;
            continue;
        }
        if ((event.vp == HMI_VP_BUTTON_RESCAN) && (event.value == 1U)) {
            if (tunnel_state == TUNNEL_WAIT_CONFIRM) {
                Tunnel_StartRescan();
                return;
            }
            g_debug.tunnel_hmi_ignored_count++;
            continue;
        }
        /* Reset deliberately ends in STOPPED with both gates homed. That is a
         * valid manual starting point too; a latched stop was rejected above. */
        if ((Tunnel_ManualAllowed() == 0U) &&
            !((tunnel_state == TUNNEL_STOPPED) && (gate_homed != 0U))) {
            g_debug.tunnel_hmi_ignored_count++;
            continue;
        }
        if ((event.vp == HMI_VP_BUTTON_CONVEYOR) && (event.value == 1U)) {
            if (MotorPwm_IsRunning(CONVEYOR_DRIVER) != 0U) {
                MotorPwm_Stop(CONVEYOR_DRIVER);
                continue;
            }
            if (Tunnel_ReadSensor(TUNNEL_SENSOR_SS3) != 0U) {
                g_debug.tunnel_hmi_ignored_count++;
                continue;
            }
            if (tunnel_state != TUNNEL_MANUAL) {
                Tunnel_SetState(TUNNEL_MANUAL);
            }
            started = 1U;
            interrupt_state = __get_PRIMASK();
            __disable_irq();
            if ((Tunnel_ReadSensor(TUNNEL_SENSOR_SS3) == 0U) &&
                (MotorPwm_IsRunning(CONVEYOR_DRIVER) == 0U)) {
                started = MotorPwm_StartContinuousArr(CONVEYOR_DRIVER, CONVEYOR_DIRECTION,
                                                      Tunnel_GetConveyorArr());
                manual_conveyor_tick = HAL_GetTick();
                if (Tunnel_ReadSensor(TUNNEL_SENSOR_SS3) != 0U) {
                    MotorPwm_Stop(CONVEYOR_DRIVER);
                }
            }
            if (interrupt_state == 0U) {
                __enable_irq();
            }
            if (started == 0U) {
                Tunnel_EnterFault(TUNNEL_FAULT_MOTOR_START);
                return;
            }
            continue;
        }
        if ((event.vp == HMI_VP_BUTTON_GATE) && (event.value == 1U)) {
            Tunnel_BeginHome(TUNNEL_HOME_MANUAL);
            return;
        }
        if ((event.vp == HMI_VP_BUTTON_GATE) && (event.value == 0U)) {
            if ((gate_homed != 0U) || (Tunnel_GateClosedForInventory() != 0U)) {
                MotorPwm_Stop(CONVEYOR_DRIVER);
                ZK7182_CancelScan();
                manual_scan_requested = 0U;
                manual_scan_started = 0U;
                manual_scan_stopping = 0U;
                Hmi_FlushEvents();
                Tunnel_StartOpeningStepState(TUNNEL_MODE_OPEN_GATE);
                return;
            }
        }
        g_debug.tunnel_hmi_ignored_count++;
    }
}

static void Tunnel_UpdateHmi(uint32_t now) {
    uint16_t page;
    uint16_t quantity;
    uint16_t indicators;
    uint16_t conveyor;
    uint16_t gate;
    uint16_t scan;
    uint16_t auto_mode;
    uint16_t semi_mode;

    if ((tunnel_state == TUNNEL_STARTUP_WAIT) || ((now - hmi_status_tick) < HMI_STATUS_PERIOD_MS)) {
        return;
    }
    hmi_status_tick = now;
    if ((g_hmi_debug.config_known != 0U) && (hmi_link_seen == 0U)) {
        /* HAL TX success before the LCD finished booting does not prove display
           delivery. Re-publish once a real reply establishes the live link. */
        hmi_link_seen = 1U;
        hmi_quantity_valid = 0U;
        hmi_page_valid = 0U;
        hmi_indicators_valid = 0U;
    }
    page = (tunnel_state == TUNNEL_WAIT_CONFIRM) ? HMI_PAGE_RESULT : HMI_PAGE_CONTROL;
    if ((tunnel_state == TUNNEL_WAIT_CONFIRM) &&
        ((now - hmi_result_refresh_tick) >= HMI_RESULT_REFRESH_MS)) {
        /* UART TX success is not an LCD delivery acknowledgement. Recover a lost
           result-page write while waiting for the operator; never auto-confirm.
           Only this stationary popup is refreshed, not the manual hold buttons. */
        hmi_result_refresh_tick = now;
        hmi_quantity_valid = 0U;
        hmi_page_valid = 0U;
    }
    quantity = (ZK7182_IsScanActive() != 0U) ? ZK7182_GetUniqueEpcCount() : display_quantity;
    conveyor = MotorPwm_IsRunning(CONVEYOR_DRIVER);
    gate = Tunnel_GatesRunning();
    scan = ((ZK7182_IsScanActive() != 0U) && (g_debug.tunnel_scan_paused == 0U)) ? 1U : 0U;
    auto_mode = (!manual_scan_only && (tunnel_mode == TUNNEL_MODE_AUTO) && (tunnel_state != TUNNEL_STOPPED) &&
                 (tunnel_state != TUNNEL_RETURN_GATE_TO_SS2))
                    ? 1U
                    : 0U;
    semi_mode = (!manual_scan_only && (tunnel_mode == TUNNEL_MODE_SEMI_AUTO) && (tunnel_state != TUNNEL_STOPPED) &&
                 (tunnel_state != TUNNEL_RETURN_GATE_TO_SS2))
                    ? 1U
                    : 0U;
    indicators = conveyor | (gate << 1U) | (scan << 2U) | (auto_mode << 3U) | (semi_mode << 4U);

    if ((hmi_quantity_valid == 0U) || (quantity != hmi_last_quantity)) {
        if (Hmi_SendQuantity(quantity) != 0U) {
            hmi_last_quantity = quantity;
            hmi_quantity_valid = 1U;
        }
    }
    if ((hmi_page_valid == 0U) || (page != hmi_last_page)) {
        if (Hmi_SendPage(page) != 0U) {
            hmi_last_page = page;
            hmi_page_valid = 1U;
        }
    }
    if ((hmi_indicators_valid == 0U) || (indicators != hmi_last_indicators)) {
        if (Hmi_SendIndicators(conveyor, gate, scan, auto_mode, semi_mode) != 0U) {
            hmi_last_indicators = indicators;
            hmi_indicators_valid = 1U;
        }
    }
}

void Tunnel_Init(void) {
    GPIO_InitTypeDef gpio;
    uint32_t now;
    uint32_t sensor;

    __HAL_RCC_GPIOC_CLK_ENABLE();
    gpio.Pin = SENSOR_SS1_PIN | SENSOR_SS2_PIN | SENSOR_SS3_PIN | SENSOR_SS7_PIN;
    gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);

    __HAL_GPIO_EXTI_CLEAR_IT(SENSOR_SS1_PIN | SENSOR_SS2_PIN | SENSOR_SS3_PIN | SENSOR_SS7_PIN);
    HAL_NVIC_SetPriority(EXTI1_IRQn, 3U, 0U);
    HAL_NVIC_EnableIRQ(EXTI1_IRQn);
    HAL_NVIC_SetPriority(EXTI2_IRQn, 3U, 0U);
    HAL_NVIC_EnableIRQ(EXTI2_IRQn);
    HAL_NVIC_SetPriority(EXTI3_IRQn, 3U, 0U);
    HAL_NVIC_EnableIRQ(EXTI3_IRQn);

    Tunnel_StopGates();
    MotorPwm_Stop(CONVEYOR_DRIVER);
    now = HAL_GetTick();
    for (sensor = 0U; sensor < (uint32_t)TUNNEL_SENSOR_COUNT; sensor++) {
        sensor_filter[sensor].raw_active = Tunnel_ReadSensor((TunnelSensorIndex)sensor);
        sensor_filter[sensor].stable_active = 0U;
        sensor_filter[sensor].raw_changed_tick = now;
    }

    tunnel_last_tick = now;
    state_entry_tick = now;
    hmi_retry_tick = now;
    rearm_clear_tick = now;
    display_quantity = 0U;
    ss3_cutoff_quantity = 0U;
    hmi_attempted = 0U;
    rearm_clear_timing = 0U;
    motion_paused = 0U;
    ss3_cutoff_valid = 0U;
    inventory_scan_failed = 0U;
    tunnel_fault = TUNNEL_FAULT_NONE;
    tunnel_mode = TUNNEL_MODE_AUTO;
    cycle_semi = 0U;
    rescan_in_progress = 0U;
    reverse_started = 0U;
    gate_started[0] = gate_started[1] = 0U;
    gate_paused[0] = gate_paused[1] = 0U;
    home_motion_wait_tick = now;
    gate_homed = 0U;
    gate2_closed_confirmed = 0U;
    home_reason = TUNNEL_HOME_NONE;
    manual_scan_requested = 0U;
    manual_scan_started = 0U;
    manual_scan_stopping = 0U;
    manual_scan_stop_tick = now;
    manual_scan_wait_tick = now;
    manual_conveyor_tick = now;
    hmi_conveyor_down = 0U;
    hmi_status_tick = now;
    hmi_result_refresh_tick = now;
    hmi_quantity_valid = 0U;
    hmi_page_valid = 0U;
    hmi_indicators_valid = 0U;
    hmi_link_seen = 0U;
    g_debug.tunnel_rescan_count = 0U;
    g_debug.tunnel_hmi_ignored_count = 0U;
    Tunnel_ResetTravel();
    /* Let LCD/USB initialize without blocking their processing. Then require
       a clean all-HIGH baseline before an SS1 edge can start motion. */
    emergency_latched = 0U;
    rf_emergency_latched = 0U;
    manual_scan_only = 0U;
    conveyor_speed_level = SpeedStore_Load();
    configured_scan_duration_ms = TUNNEL_INVENTORY_DURATION_MS;
    active_scan_duration_ms = TUNNEL_INVENTORY_DURATION_MS;
    pc_maintenance = 0U;
    pc_motion_owned = 0U;
    pc_lease_expired = 0U;
    pc_heartbeat_tick = now;
    pc_supervised = 0U;
    Hmi_SetControlLocked(0U);
    Tunnel_SetState(TUNNEL_STARTUP_WAIT);
    Tunnel_UpdateDebug();
}

void Tunnel_Tick1ms(void) {
    uint32_t now;
    uint32_t interrupt_state;

    if ((pc_motion_owned || pc_supervised || pc_maintenance) &&
        ((HAL_GetTick() - pc_heartbeat_tick) >= TUNNEL_CONTROL_LEASE_MS)) {
        Tunnel_StopGates();
        MotorPwm_Stop(CONVEYOR_DRIVER);
        ZK7182_PauseScan();
        pc_lease_expired = 1U;
    }
    /* Cut off independently of USB/HMI main-loop latency. This latch survives
     * a concurrent first-SS3 state transition and is cleared only for a new
     * parcel or explicit recovery; a bounced SS3 cannot restart the belt. */
    if ((tunnel_state == TUNNEL_MANUAL) && (manual_scan_started != 0U) &&
        ((HAL_GetTick() - inventory_session_tick) >= active_scan_duration_ms)) {
        MotorPwm_Stop(CONVEYOR_DRIVER);
        ZK7182_PauseScan();
    }
    if ((Tunnel_IsInventoryState() != 0U)) {
        interrupt_state = __get_PRIMASK();
        __disable_irq();
        now = HAL_GetTick();
        if (((Tunnel_IsInventoryState() != 0U)) && (ss3_timeout_latched == 0U) &&
            (((now - inventory_session_tick) >= active_scan_duration_ms) ||
             ((Tunnel_IsInventoryState() != 0U) &&
              (Tunnel_StateTimedOut(now, TUNNEL_SS3_TIMEOUT_MS) != 0U)))) {
            Tunnel_StopTransferMotion();
            ZK7182_PauseScan();
            ss3_timeout_tick = now;
            ss3_timeout_latched = 1U;
        }
        if (interrupt_state == 0U) {
            __enable_irq();
        }
    }
}

void Tunnel_Process(void) {
    uint32_t now;

    Tunnel_Tick1ms();
    if (pc_lease_expired != 0U) {
        Tunnel_StopForSafety(STOP_PC_TIMEOUT);
    }
    if ((pc_motion_owned != 0U) && (pc_lease_expired == 0U) && (Tunnel_ManualAllowed() != 0U) &&
        (Tunnel_GatesRunning() == 0U) && (MotorPwm_IsRunning(CONVEYOR_DRIVER) == 0U) &&
        (ZK7182_IsScanActive() == 0U) && (manual_scan_requested == 0U) &&
        (manual_scan_started == 0U)) {
        pc_motion_owned = 0U;
    }
    Tunnel_HandleHmiEvents();
    /* IN7 confirmation is retained from closing until opening/recovery.
       IN2 remains the live interlock; diagnostic inputs are always physical. */
    if ((Tunnel_IsInventoryState() != 0U) &&
        ((gate_interlock_latched != 0U) || (Tunnel_ReadSensor(TUNNEL_SENSOR_SS2) == 0U))) {
        Tunnel_EnterFault(TUNNEL_FAULT_SENSOR_ORDER);
        return;
    }
    /* An HMI event can enter a new state across a SysTick boundary. Snapshot
       time afterward so unsigned timeout subtraction cannot see a future entry. */
    now = HAL_GetTick();
    Tunnel_Tick1ms(); /* Also service cutoff on a main-loop pass. */

    if ((ss3_timeout_latched != 0U) && ((Tunnel_IsInventoryState() != 0U))) {
        Tunnel_StopTransferMotion();
        if ((gate_interlock_latched != 0U) || !sensor_filter[TUNNEL_SENSOR_SS2].stable_active ||
            !sensor_filter[TUNNEL_SENSOR_SS2].raw_active || !Tunnel_ReadSensor(TUNNEL_SENSOR_SS2) ||
            Tunnel_GatesRunning()) {
            Tunnel_EnterFault(TUNNEL_FAULT_SENSOR_ORDER);
            return;
        }
        Tunnel_EnterFault(TUNNEL_FAULT_CONVEYOR_TIMEOUT);
        return;
    }

    /* Poll the physical SS3 pin on every main-loop pass as a backup for EXTI.
       This check intentionally precedes the 5 ms state-machine throttle so no
       new reader command can be launched during that otherwise blind window. */
    if ((Tunnel_IsInventoryState() != 0U) && (motion_paused == 0U) &&
        (Tunnel_ReadSensor(TUNNEL_SENSOR_SS3) != 0U)) {
        Tunnel_StopAtSs3();
    }
    if ((tunnel_state == TUNNEL_RESCAN_TO_SS1) && (Tunnel_ReadSensor(TUNNEL_SENSOR_SS1) != 0U)) {
        MotorPwm_Stop(CONVEYOR_DRIVER);
        motion_paused = 1U;
    }
    if ((tunnel_state == TUNNEL_RETURN_GATE_TO_SS2) || (tunnel_state == TUNNEL_CLOSE_STEP_TO_SS2)) {
        Tunnel_GateRawCutoffs();
    }

    if ((now - tunnel_last_tick) < TUNNEL_PROCESS_PERIOD_MS) {
        return;
    }
    tunnel_last_tick = now;
    Tunnel_UpdateSensor(TUNNEL_SENSOR_SS1, now);
    Tunnel_UpdateSensor(TUNNEL_SENSOR_SS2, now);
    Tunnel_UpdateSensor(TUNNEL_SENSOR_SS3, now);
    Tunnel_UpdateSensor(TUNNEL_SENSOR_SS7, now);

    switch (tunnel_state) {
    case TUNNEL_WAIT_SS1_LOW:
        if (pc_maintenance != 0U) {
            break;
        }
        if (sensor_filter[TUNNEL_SENSOR_SS1].stable_active != 0U) {
            display_quantity = 0U;
            inventory_scan_failed = 0U;
            tunnel_fault = TUNNEL_FAULT_NONE;
            Tunnel_StartFirstConveyor();
        }
        break;

    case TUNNEL_CONVEYOR_TO_SS1_HIGH:
        Tunnel_ProcessConveyorToSs1(now);
        break;

    case TUNNEL_CLOSE_STEP_TO_SS2:
        Tunnel_ProcessClosingStep(now);
        break;

    case TUNNEL_WAIT_READER:
        if (sensor_filter[TUNNEL_SENSOR_SS3].stable_active != 0U) {
            /* Exit must be clear before starting a new pass. */
            Tunnel_EnterFault(TUNNEL_FAULT_SENSOR_ORDER);
        } else if (ZK7182_IsReady() != 0U) {
            Tunnel_StartInventoryTransfer();
        } else if (Tunnel_StateTimedOut(now, TUNNEL_READER_READY_TIMEOUT_MS) != 0U) {
            Tunnel_FinishUnavailableReader();
        }
        break;

    case TUNNEL_WAIT_INVENTORY_START:
        Tunnel_ProcessInventoryStart(now);
        break;

    case TUNNEL_CONVEYOR_INVENTORY_TO_SS3:
    case TUNNEL_RESCAN_TO_SS3:
        Tunnel_ProcessInventoryTransfer(now);
        break;

    case TUNNEL_DISPLAY_RESULT:
        if ((hmi_attempted == 0U) || ((now - hmi_retry_tick) >= HMI_RETRY_PERIOD_MS)) {
            hmi_attempted = 1U;
            hmi_retry_tick = now;
            if (Hmi_SendQuantity(display_quantity) != 0U) {
                hmi_last_quantity = display_quantity;
                hmi_quantity_valid = 1U;
                if (cycle_semi != 0U) {
                    if (Hmi_SendPage(HMI_PAGE_RESULT) != 0U) {
                        hmi_last_page = HMI_PAGE_RESULT;
                        hmi_page_valid = 1U;
                        hmi_result_refresh_tick = HAL_GetTick();
                        Hmi_FlushEvents();
                        Tunnel_SetState(TUNNEL_WAIT_CONFIRM);
                    }
                } else {
                    Tunnel_StartOpeningStepState(TUNNEL_OPEN_STEP);
                }
            }
        }
        if ((tunnel_state == TUNNEL_DISPLAY_RESULT) &&
            (Tunnel_StateTimedOut(now, TUNNEL_HMI_TIMEOUT_MS) != 0U)) {
            Tunnel_EnterFault(TUNNEL_FAULT_HMI);
        }
        break;

    case TUNNEL_OPEN_STEP:
    case TUNNEL_MODE_OPEN_GATE:
        if (Tunnel_StateTimedOut(now, STEP_OPEN_TIMEOUT_MS)) {
            Tunnel_EnterFault(TUNNEL_FAULT_STEP_OPEN_TIMEOUT);
        } else if ((!MotorPwm_IsRunning(STEP_DRIVER) &&
                    MotorPwm_GetPulseCount(STEP_DRIVER) != STEP_OPEN_PULSE_COUNT) ||
                   (!MotorPwm_IsRunning(STEP2_DRIVER) &&
                    MotorPwm_GetPulseCount(STEP2_DRIVER) != STEP_OPEN_PULSE_COUNT)) {
            Tunnel_EnterFault(TUNNEL_FAULT_STEP_OPEN_COUNT);
        } else if (!Tunnel_GatesRunning()) {
            g_debug.tunnel_step_open_pulse_count = MotorPwm_GetPulseCount(STEP_DRIVER);
            Hmi_FlushEvents();
            Tunnel_SetState(TUNNEL_WAIT_REARM);
        }
        break;

    case TUNNEL_WAIT_REARM:
        if ((sensor_filter[TUNNEL_SENSOR_SS1].stable_active == 0U) &&
            (sensor_filter[TUNNEL_SENSOR_SS2].stable_active == 0U) &&
            (sensor_filter[TUNNEL_SENSOR_SS3].stable_active == 0U) &&
            (sensor_filter[TUNNEL_SENSOR_SS7].stable_active == 0U) &&
            (Tunnel_RawStable(TUNNEL_SENSOR_SS1, 0U, now) != 0U) &&
            (Tunnel_RawStable(TUNNEL_SENSOR_SS2, 0U, now) != 0U) &&
            (Tunnel_RawStable(TUNNEL_SENSOR_SS3, 0U, now) != 0U) &&
            (Tunnel_RawStable(TUNNEL_SENSOR_SS7, 0U, now) != 0U)) {
            if (rearm_clear_timing == 0U) {
                rearm_clear_timing = 1U;
                rearm_clear_tick = now;
            } else if ((now - rearm_clear_tick) >= TUNNEL_REARM_DEBOUNCE_MS) {
                Tunnel_SetState(TUNNEL_WAIT_SS1_LOW);
            }
        } else {
            rearm_clear_timing = 0U;
        }
        break;

    case TUNNEL_STARTUP_WAIT:
        if (Tunnel_StateTimedOut(now, TUNNEL_STARTUP_DELAY_MS) != 0U) {
            Tunnel_SetState(TUNNEL_WAIT_REARM);
        }
        break;

    case TUNNEL_RESCAN_TO_SS1:
        Tunnel_ProcessRescanReverse(now);
        break;

    case TUNNEL_RESCAN_WAIT_READER:
        Tunnel_ProcessRescanWait(now);
        break;

    case TUNNEL_RETURN_GATE_TO_SS2:
        Tunnel_ProcessHome(now);
        break;

    case TUNNEL_MANUAL:
        Tunnel_ProcessManual(now);
        break;

    case TUNNEL_WAIT_CONFIRM:
    case TUNNEL_STOPPED:
        break;

    case TUNNEL_FAULT:
        break;

    default:
        Tunnel_EnterFault(TUNNEL_FAULT_INVALID_STATE);
        break;
    }

    Tunnel_UpdateDebug();
    Tunnel_UpdateHmi(now);
}

TunnelState Tunnel_GetState(void) {
    return tunnel_state;
}

TunnelFault Tunnel_GetFault(void) {
    return tunnel_fault;
}

TunnelMode Tunnel_GetMode(void) {
    return tunnel_mode;
}

void HAL_GPIO_EXTI_Callback(uint16_t gpio_pin) {
    if ((Tunnel_IsInventoryState() != 0U) && (gpio_pin == SENSOR_SS2_PIN) &&
        (Tunnel_ReadSensor(TUNNEL_SENSOR_SS2) == 0U)) {
        Tunnel_StopTransferMotion();
        ZK7182_PauseScan();
        gate_interlock_latched = 1U;
        return;
    }
    if ((gpio_pin == SENSOR_SS1_PIN) && (tunnel_state == TUNNEL_RESCAN_TO_SS1) &&
        (Tunnel_ReadSensor(TUNNEL_SENSOR_SS1) != 0U)) {
        MotorPwm_Stop(CONVEYOR_DRIVER);
        motion_paused = 1U;
        return;
    }
    if ((gpio_pin == SENSOR_SS1_PIN) && (tunnel_state == TUNNEL_CONVEYOR_TO_SS1_HIGH) &&
        (Tunnel_ReadSensor(TUNNEL_SENSOR_SS1) == 0U)) {
        /* SS1 HIGH: stop the first conveyor phase at the raw edge. */
        MotorPwm_Stop(CONVEYOR_DRIVER);
        motion_paused = 1U;
        return;
    }

    if (((gpio_pin == SENSOR_SS2_PIN) || (gpio_pin == SENSOR_SS7_PIN)) &&
        ((tunnel_state == TUNNEL_CLOSE_STEP_TO_SS2) ||
         (tunnel_state == TUNNEL_RETURN_GATE_TO_SS2))) {
        Tunnel_GateRawCutoffs();
        return;
    }

    if ((gpio_pin == SENSOR_SS3_PIN) && (Tunnel_IsInventoryState() != 0U) &&
        (Tunnel_ReadSensor(TUNNEL_SENSOR_SS3) != 0U)) {
        /* First SS3 ends this scan pass: no automatic reverse. */
        Tunnel_StopAtSs3();
    }
    if ((gpio_pin == SENSOR_SS3_PIN) && (tunnel_state == TUNNEL_MANUAL) &&
        (Tunnel_ReadSensor(TUNNEL_SENSOR_SS3) != 0U)) {
        MotorPwm_Stop(CONVEYOR_DRIVER);
    }
}

/* PC commands use the same state machine/interlocks as the local HMI. */
uint32_t Tunnel_GetScanDurationMs(void) {
    return configured_scan_duration_ms;
}
uint32_t Tunnel_GetActiveScanDurationMs(void) {
    return active_scan_duration_ms;
}
uint32_t Tunnel_GetScanElapsedMs(void) {
    uint32_t elapsed;
    if ((ZK7182_IsScanActive() == 0U) && (manual_scan_started == 0U)) {
        return 0U;
    }
    elapsed = HAL_GetTick() - inventory_session_tick;
    return (elapsed < active_scan_duration_ms) ? elapsed : active_scan_duration_ms;
}
uint8_t Tunnel_IsPcMaintenance(void) {
    return pc_maintenance;
}
uint32_t Tunnel_GetPcAge(void) {
    return HAL_GetTick() - pc_heartbeat_tick;
}
uint8_t Tunnel_GetStopFlags(void) {
    return (emergency_latched ? 1U : 0U) | (gate_homed ? 2U : 0U) |
           (MotorPwm_IsReleased() ? 4U : 0U) | (pc_supervised ? 8U : 0U) |
           (Hmi_IsControlLocked() ? 16U : 0U);
}
static void Tunnel_StopLatched(uint8_t reason, uint8_t release) {
    if (reason == STOP_EMERGENCY)
        rf_emergency_latched = 1U;
    manual_scan_only = 0U;
    Tunnel_StopTransferMotion();
    if (release)
        MotorPwm_EmergencyStop();
    else
        MotorPwm_SafetyStop(reason);
    ZK7182_PauseScan();
    emergency_latched = 1U;
    gate_homed = 0U;
    gate2_closed_confirmed = 0U;
    /* Revoke the expired session: a late heartbeat cannot resume motion.
       A new PC connection explicitly registers its supervision again. */
    if (reason == STOP_PC_TIMEOUT || reason == STOP_PC_DISCONNECT) {
        pc_supervised = 0U;
        Hmi_SetControlLocked(0U);
    }
    pc_motion_owned = 0U;
    pc_maintenance = 0U;
    pc_lease_expired = 0U;
    ZK7182_CancelScan();
    Hmi_DiscardActionsAfterStop();
    manual_scan_requested = 0U;
    manual_scan_started = 0U;
    manual_scan_stopping = 0U;
    Tunnel_ResetTravel();
    Tunnel_SetState(TUNNEL_STOPPED);
}
void Tunnel_StopForSafety(uint8_t reason) {
    Tunnel_StopLatched(reason, 0U);
}
void Tunnel_PcHeartbeat(void) {
    /* Check the OLD timestamp before accepting a delayed request. */
    Tunnel_Tick1ms();
    if (pc_lease_expired)
        Tunnel_StopForSafety(STOP_PC_TIMEOUT);
    pc_heartbeat_tick = HAL_GetTick();
}

uint8_t Tunnel_RemoteCommand(uint8_t command, uint16_t value) {
    uint32_t interrupt_state;
    uint8_t started;
    Tunnel_PcHeartbeat();
    if (command == 0x17U) /* Explicit Emergency releases torque. */
    {
        Tunnel_StopLatched(STOP_EMERGENCY, 1U);
        return 0U;
    }
    if (command == 0x22U) /* PC session registration / deliberate disconnect. */
    {
        if (value > 1U)
            return 1U;
        if (value && !pc_supervised) {
            Hmi_SetControlLocked(1U);
            Tunnel_StopForSafety(STOP_PC_TAKEOVER);
            pc_supervised = 1U;
        } else if (!value)
            Tunnel_StopForSafety(STOP_PC_DISCONNECT);
        return 0U;
    }
    if (command == 0x12U) {
        uint8_t result = Tunnel_RequestManualScan(value);
        if (!result && value)
            pc_motion_owned = 1U; /* RF also requires the owner's lease. */
        return result;
    }
    if (manual_scan_only)
        return 2U; /* Finish RF/drain before Reset, motion, mode or settings. */
    if (command == 0x18U) {
#ifdef ARDUINO
        if (EspEmergencyHeld() != 0U) {
            return 3U;
        }
#endif
        pc_supervised = 1U;
        Hmi_SetControlLocked(1U);
        pc_maintenance = 0U;
        pc_motion_owned = 1U;
        Tunnel_BeginHome(TUNNEL_HOME_RESET);
        return 0U;
    }
    if (command == 0x19U) {
        if ((value < 1U) || (value > 5U)) {
            return 1U;
        }
        if (((Tunnel_ManualAllowed() == 0U) && (tunnel_state != TUNNEL_STOPPED)) ||
            (MotorPwm_IsRunning(CONVEYOR_DRIVER) != 0U) || (Tunnel_GatesRunning() != 0U) ||
            (ZK7182_IsScanActive() != 0U) || (manual_scan_requested != 0U)) {
            return 2U;
        }
        /* No motion or RF transaction may run while the single-bank MCU flash
           is being programmed/erased. Other HMI commands remain queued. */
        if ((MotorPwm_IsRunning(MOTOR_PWM_DRIVER_2) != 0U) ||
            (MotorPwm_IsRunning(MOTOR_PWM_DRIVER_3) != 0U) || (ZK7182_IsReady() == 0U) ||
            (ZK7182_IsInventoryResponsePending() != 0U)) {
            return 2U;
        }
        if (SpeedStore_Save((uint8_t)value) == 0U) {
            return 8U;
        }
        conveyor_speed_level = (uint8_t)value;
        return 0U;
    }
    if (command == 0x14U) {
        return 7U;
    } /* Retired duration control. */
    if (command == 0x20U) {
        if (value > 1U) {
            return 1U;
        }
        if (value == 0U) {
            pc_maintenance = 0U;
            return 0U;
        }
        if ((Tunnel_GatesRunning() != 0U) || (MotorPwm_IsRunning(CONVEYOR_DRIVER) != 0U) ||
            (ZK7182_IsScanActive() != 0U) || (manual_scan_requested != 0U) ||
            ((Tunnel_ManualAllowed() == 0U) && (tunnel_state != TUNNEL_STOPPED))) {
            return 2U;
        }
        if (ZK7182_IsReady() == 0U) {
            return 4U;
        }
        pc_maintenance = 1U;
        pc_motion_owned = 0U;
        return 0U;
    }
    if ((pc_maintenance != 0U) || (emergency_latched != 0U)) {
        return 2U;
    }
    if (command == 0x13U) {
        if (value > 1U) {
            return 1U;
        }
        if ((tunnel_state != TUNNEL_WAIT_SS1_LOW) && (tunnel_state != TUNNEL_WAIT_REARM) &&
            !((tunnel_state == TUNNEL_STOPPED) && (gate_homed != 0U))) {
            return 2U;
        }
        tunnel_mode = (value == 0U) ? TUNNEL_MODE_AUTO : TUNNEL_MODE_SEMI_AUTO;
        if (tunnel_state == TUNNEL_STOPPED) {
            home_reason = TUNNEL_HOME_NONE;
            Hmi_FlushEvents();
            pc_motion_owned = 1U;
            Tunnel_StartOpeningStepState(TUNNEL_MODE_OPEN_GATE);
        }
        return 0U;
    }
    if ((command == 0x15U) || (command == 0x16U)) {
        if (tunnel_state != TUNNEL_WAIT_CONFIRM) {
            return 2U;
        }
        if (command == 0x15U) {
            Hmi_FlushEvents();
            Tunnel_StartOpeningStepState(TUNNEL_OPEN_STEP);
        } else {
            Tunnel_StartRescan();
        }
        return 0U;
    }
    if ((command < 0x10U) || (command > 0x12U)) {
        return 7U;
    }
    if (value > 1U) {
        return 1U;
    }
    if ((Tunnel_ManualAllowed() == 0U) && (tunnel_state != TUNNEL_STOPPED)) {
        return 2U;
    }
    if (command == 0x10U) {
        if (value != 0U) {
            pc_motion_owned = 1U;
            Tunnel_BeginHome(TUNNEL_HOME_MANUAL);
            return 0U;
        }
        if ((gate_homed == 0U) && (Tunnel_GateClosedForInventory() == 0U)) {
            return 3U;
        }
        MotorPwm_Stop(CONVEYOR_DRIVER);
        ZK7182_CancelScan();
        manual_scan_requested = 0U;
        manual_scan_started = 0U;
        manual_scan_stopping = 0U;
        Hmi_FlushEvents();
        pc_motion_owned = 1U;
        Tunnel_StartOpeningStepState(TUNNEL_MODE_OPEN_GATE);
        return 0U;
    }
    if (value == 0U) {
        MotorPwm_Stop(CONVEYOR_DRIVER);
        return 0U;
    }
    if (Tunnel_ReadSensor(TUNNEL_SENSOR_SS3) != 0U) {
        return 3U;
    }
    if (Tunnel_GatesRunning() != 0U) {
        return 2U;
    }
    if (tunnel_state != TUNNEL_MANUAL) {
        Tunnel_SetState(TUNNEL_MANUAL);
    }
    started = 1U;
    interrupt_state = __get_PRIMASK();
    __disable_irq();
    if (MotorPwm_IsRunning(CONVEYOR_DRIVER) == 0U) {
        started = MotorPwm_StartContinuousArr(CONVEYOR_DRIVER, CONVEYOR_DIRECTION,
                                              Tunnel_GetConveyorArr());
        manual_conveyor_tick = HAL_GetTick();
    }
    if (Tunnel_ReadSensor(TUNNEL_SENSOR_SS3) != 0U) {
        MotorPwm_Stop(CONVEYOR_DRIVER);
    }
    if (interrupt_state == 0U) {
        __enable_irq();
    }
    pc_motion_owned = 1U;
    return (started != 0U) ? 0U : 2U;
}
