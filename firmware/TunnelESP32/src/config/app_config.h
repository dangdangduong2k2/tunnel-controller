#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* CP210x UART side connected to the ZK-7182M. */
#define CP210X_UART_BAUD 115200UL

/* ZK fast inventory uses the reader's existing saved parameters.
 * Only START (50, Target A) and STOP (51) are sent; no reader setup. */
#define ZK7182_READER_ADDRESS 0x00U
#define ZK7182_BOOT_DELAY_MS 250UL
#define ZK7182_COMMAND_TIMEOUT_MS 2000UL
#define ZK7182_START_MAX_FAILURES 3U
#define ZK7182_SILENCE_RESTART_MS 5000UL
#define ZK7182_SILENCE_MAX_RESTARTS 2U
#define ZK7182_STOP_QUIET_MS 200UL
#define ZK7182_STOP_MAX_ATTEMPTS 3U
/* MCU session/transport limits, not settings written to the reader. */
#define ZK7182_SCAN_DURATION_MS 30000UL
#define ZK7182_SCAN_MAX_DURATION_MS 30000UL
#define ZK7182_INVENTORY_END_GUARD_MS 800UL
#define ZK7182_MAX_UNIQUE_TAGS 1024U
#define ZK7182_MAX_EPC_BYTES 64U

/* Set to 1 only when PB0 drives the enable pin of a 5 V USB power switch. */
#define USB_VBUS_SWITCH_ENABLED 0U
#define USB_VBUS_SWITCH_GPIO_PORT GPIOB
#define USB_VBUS_SWITCH_GPIO_CLK_EN() __HAL_RCC_GPIOB_CLK_ENABLE()
#define USB_VBUS_SWITCH_PIN GPIO_PIN_0
#define USB_VBUS_SWITCH_ACTIVE_LEVEL GPIO_PIN_SET

/* DWIN and PC/J5 share UART0 RX44/TX43, with distinct framed protocols.
 * Firmware cannot electrically isolate CH340 TX from the shared RX line. */
#define HMI_UART_BAUD 115200UL
#define DEBUG_UART_BAUD 115200UL
#define DEBUG_PRINTF_ENABLED 0U

/* Set to 1 only for the isolated SS8 -> PULSE1 bench test. */
#define SS8_MOTOR1_TEST_ENABLED 0U
#define SS8_MOTOR1_TEST_DIRECTION 0U
#define SS8_MOTOR1_TEST_PERIOD_US 1000UL

/* Requested tunnel cycle. A 2 MHz timer tick supports 50% duty even for odd
 * microsecond periods. Legacy F103 wrote ARR directly at a 1 MHz timer tick,
 * so its START=2800/RUN=700 mean actual periods of 2801/701 us. */
#define STEP_DRIVER MOTOR_PWM_DRIVER_1
#define STEP2_DRIVER MOTOR_PWM_DRIVER_2
#define CONVEYOR_DRIVER MOTOR_PWM_DRIVER_4
#define STEP_CLOSE_DIRECTION 0U
#define STEP_OPEN_DIRECTION 1U
/* J2 forward direction corrected after the DWIN manual run moved backward (2026-10-26). */
#define CONVEYOR_DIRECTION 0U
#define CONVEYOR_REVERSE_DIRECTION (1U - CONVEYOR_DIRECTION)
#define STEP_START_PERIOD_US 934UL
#define STEP_RUN_PERIOD_US 400UL
#define STEP_CLOSE_ACCEL_PULSES 500UL
#define STEP_CLOSE_CRUISE_PULSES 3500UL
#define STEP_CLOSE_DECEL_PULSES 2000UL
#define STEP_CLOSE_PULSE_COUNT                                                                     \
    (STEP_CLOSE_ACCEL_PULSES + STEP_CLOSE_CRUISE_PULSES + STEP_CLOSE_DECEL_PULSES)
#define STEP_OPEN_ACCEL_PULSES 500UL
#define STEP_OPEN_CRUISE_PULSES 4000UL
#define STEP_OPEN_DECEL_PULSES 600UL
#define STEP_OPEN_PULSE_COUNT                                                                      \
    (STEP_OPEN_ACCEL_PULSES + STEP_OPEN_CRUISE_PULSES + STEP_OPEN_DECEL_PULSES)
/* Medium variant: midpoint of slow/fast pulse periods, 10.5 us = 95.238 kHz.
 * TIM4 retains its 4 MHz tick and exactly 50% duty. */
#define CONVEYOR_TIME_SCALE_NUM 3UL
#define CONVEYOR_TIME_SCALE_DEN 2UL
#define CONVEYOR_PERIOD_NS (7000UL * CONVEYOR_TIME_SCALE_NUM / CONVEYOR_TIME_SCALE_DEN)
#define DRIVER_DIR_SETUP_US 5UL
#define DIRECTION_REVERSAL_DELAY_MS 20UL
/* Compatibility constants for retired midpoint states. Current v1.4 cycle
 * makes one forward inventory pass; only explicit SEMI Rescan reverses. */
#define TUNNEL_CENTER_INVENTORY_MS 3750UL
#define TUNNEL_CENTER_MIN_TRAVEL_MS 2UL
#define TUNNEL_STARTUP_DELAY_MS 1000UL
#define HMI_STATUS_PERIOD_MS 100UL
#define HMI_RESULT_REFRESH_MS 2000UL
/* Counted moves stop on a falling-edge compare interrupt.  Keep at least
 * 200 us until the next rising edge; continuous PWM is not subject to this. */
#define MOTOR_COUNTED_MIN_PERIOD_US 400UL
#define SENSOR_DEBOUNCE_MS 50UL
#define SENSOR_SS1_DEBOUNCE_MS 1000UL
#define STEP_CLOSE_TIMEOUT_MS 30000UL
#define STEP_OPEN_TIMEOUT_MS 30000UL
/* Keep the slow build's safety timeout; only speed and midpoint dwell change. */
#define TUNNEL_CONVEYOR_TIMEOUT_MS 60000UL
/* Legacy secondary per-trip watchdog. The shared 30 s session budget
 * expires no later, including reverse/dwell/drain. SEMI still requires Confirm. */
#define TUNNEL_SS3_TIMEOUT_MS 30000UL
#define TUNNEL_READER_READY_TIMEOUT_MS 10000UL
/* Existing upper bound for early STOP cleanup; the 30 s budget also applies. */
#define TUNNEL_INVENTORY_DRAIN_TIMEOUT_MS 6100UL
#define TUNNEL_HMI_TIMEOUT_MS 5000UL
#define TUNNEL_REARM_DEBOUNCE_MS 100UL
/* Shared by first pass, reverse, center dwell and final pass; never restarted
 * on a state transition. Manual Scan and each explicit Rescan get a new budget. */
#define TUNNEL_INVENTORY_DURATION_MS ZK7182_SCAN_MAX_DURATION_MS
#define HMI_RETRY_PERIOD_MS 100UL

#endif /* APP_CONFIG_H */
