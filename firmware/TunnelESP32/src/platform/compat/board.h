#ifndef BOARD_H
#define BOARD_H

/* Legacy virtual identifiers used by the portable cycle and host tests.
 * Physical ESP32 wiring is ONLY in BoardProfile.h / EspMotors.cpp.
 * The STM32 timer/port names below are not used to configure ESP32 pins. */

#include "stm32f4xx_hal.h"

/* Four independent 50% PWM channels routed to the four driver outputs. */
#define DRIVER1_PULSE_PORT GPIOA
#define DRIVER1_PULSE_PIN GPIO_PIN_0
#define DRIVER1_PULSE_AF GPIO_AF2_TIM5
#define DRIVER1_TIMER TIM5
#define DRIVER1_TIMER_CHANNEL TIM_CHANNEL_1
#define DRIVER1_DIR_PORT GPIOA
#define DRIVER1_DIR_PIN GPIO_PIN_1

#define DRIVER2_PULSE_PORT GPIOA
#define DRIVER2_PULSE_PIN GPIO_PIN_5
#define DRIVER2_PULSE_AF GPIO_AF1_TIM2
#define DRIVER2_TIMER TIM2
#define DRIVER2_TIMER_CHANNEL TIM_CHANNEL_1
#define DRIVER2_DIR_PORT GPIOA
#define DRIVER2_DIR_PIN GPIO_PIN_4

#define DRIVER3_PULSE_PORT GPIOA
#define DRIVER3_PULSE_PIN GPIO_PIN_6
#define DRIVER3_PULSE_AF GPIO_AF2_TIM3
#define DRIVER3_TIMER TIM3
#define DRIVER3_TIMER_CHANNEL TIM_CHANNEL_1
#define DRIVER3_DIR_PORT GPIOA
#define DRIVER3_DIR_PIN GPIO_PIN_7

#define DRIVER4_PULSE_PORT GPIOD
#define DRIVER4_PULSE_PIN GPIO_PIN_12
#define DRIVER4_PULSE_AF GPIO_AF2_TIM4
#define DRIVER4_TIMER TIM4
#define DRIVER4_TIMER_CHANNEL TIM_CHANNEL_1
#define DRIVER4_DIR_PORT GPIOD
#define DRIVER4_DIR_PIN GPIO_PIN_11

/* Isolated tunnel sensors.  All are active LOW after the optocouplers. */
#define SENSOR_SS1_PORT GPIOC
#define SENSOR_SS1_PIN GPIO_PIN_1
#define SENSOR_SS2_PORT GPIOC
#define SENSOR_SS2_PIN GPIO_PIN_2
#define SENSOR_SS3_PORT GPIOC
#define SENSOR_SS3_PIN GPIO_PIN_3
#define SENSOR_SS4_PORT GPIOC
#define SENSOR_SS4_PIN GPIO_PIN_4
#define SENSOR_SS7_PORT GPIOC
#define SENSOR_SS7_PIN GPIO_PIN_7

/* Compatibility aliases retained for modules/debug expressions made for the
 * first F407 state machine. */
#define SENSOR_IN1_PORT SENSOR_SS1_PORT
#define SENSOR_IN1_PIN SENSOR_SS1_PIN
#define SENSOR_IN2_PORT SENSOR_SS2_PORT
#define SENSOR_IN2_PIN SENSOR_SS2_PIN
#define SENSOR_DOOR_CLOSED_PORT SENSOR_SS3_PORT
#define SENSOR_DOOR_CLOSED_PIN SENSOR_SS3_PIN
#define SENSOR_DOOR_OPEN_PORT SENSOR_SS4_PORT
#define SENSOR_DOOR_OPEN_PIN SENSOR_SS4_PIN

/* Eighth isolated input, used by the temporary motor-output bench test. */
#define SENSOR_SS8_PORT GPIOC
#define SENSOR_SS8_PIN GPIO_PIN_8

/* EL3H7 collectors have external 10 kOhm pull-ups; an active sensor reads LOW. */
#define SENSOR_ACTIVE_LEVEL GPIO_PIN_RESET

/* Schematic UART assignments. */
#define HMI_UART_INSTANCE USART1
#define HMI_UART_TX_PORT GPIOA
#define HMI_UART_TX_PIN GPIO_PIN_9
#define HMI_UART_RX_PORT GPIOA
#define HMI_UART_RX_PIN GPIO_PIN_10
#define HMI_UART_AF GPIO_AF7_USART1

#define DEBUG_UART_INSTANCE USART3
#define DEBUG_UART_TX_PORT GPIOD
#define DEBUG_UART_TX_PIN GPIO_PIN_8
#define DEBUG_UART_RX_PORT GPIOD
#define DEBUG_UART_RX_PIN GPIO_PIN_9
#define DEBUG_UART_AF GPIO_AF7_USART3

#endif /* BOARD_H */
