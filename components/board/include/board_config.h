/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"

#define BOARD_HARDWARE_VERSION "Rev A"

#define LCD_H_RES              (480U)
#define LCD_V_RES              (320U)

#define LCD_SD_SPI_HOST        SPI2_HOST
#define LCD_SPI_CLOCK_HZ       (40 * 1000 * 1000)
#define LCD_PIN_MOSI           GPIO_NUM_15
#define LCD_PIN_MISO           GPIO_NUM_16
#define LCD_PIN_SCLK           GPIO_NUM_17
#define LCD_PIN_CS             GPIO_NUM_18
#define LCD_PIN_DC             GPIO_NUM_3
#define LCD_PIN_RST            GPIO_NUM_38
#define LCD_PIN_BACKLIGHT      GPIO_NUM_21
#define LCD_PIN_BUSY           GPIO_NUM_NC

#define SD_PIN_CS              GPIO_NUM_9
#define SD_SPI_CLOCK_KHZ       (20 * 1000)

#define TOUCH_I2C_PORT         I2C_NUM_0
#define TOUCH_I2C_FREQ_HZ      (400 * 1000)
#define TOUCH_PIN_SDA          GPIO_NUM_1
#define TOUCH_PIN_SCL          GPIO_NUM_2
#define TOUCH_PIN_INT          GPIO_NUM_13
#define TOUCH_PIN_RST          GPIO_NUM_NC

/*
 * Application buttons.
 *
 * The service button shares GPIO0 with the ESP32-S3 download-mode
 * strapping input. It is treated as an application input only after
 * startup. The power button is also connected to the AXP313A PWRON
 * input; the hardware must prevent either input from back-powering
 * the other power domain.
 */
#define SERVICE_BUTTON_PIN               GPIO_NUM_0
#define SERVICE_BUTTON_ACTIVE_LEVEL      (0)

#define POWER_BUTTON_PIN                 GPIO_NUM_47
#define POWER_BUTTON_ACTIVE_LEVEL        (0)

/*
 * MCP23017 GPIO-expander assignments.
 *
 * Each output controls an external switch or transistor that connects a
 * 120-ohm resistor between CAN_H and CAN_L. The MCP23017 pin must never
 * carry CAN-bus current directly.
 */
#define CAN_PRIMARY_TERMINATION_EXPANDER_PIN   (0U)
#define CAN_SECONDARY_TERMINATION_EXPANDER_PIN (1U)

#define CAN_PRIMARY_TERMINATION_ACTIVE_LEVEL   (1)
#define CAN_SECONDARY_TERMINATION_ACTIVE_LEVEL (1)

/* Reserved for future CAN transceiver mode control. */
#define CAN_PRIMARY_STANDBY_EXPANDER_PIN        (2U)
#define CAN_SECONDARY_STANDBY_EXPANDER_PIN      (3U)

#define CAN_PRIMARY_STANDBY_ACTIVE_LEVEL        (1)
#define CAN_SECONDARY_STANDBY_ACTIVE_LEVEL      (1)

/* Reserved for a future automotive CAN connector multiplexer. */
#define CAN_ROUTE_SELECT_0_EXPANDER_PIN          (4U)
#define CAN_ROUTE_SELECT_1_EXPANDER_PIN          (5U)
#define CAN_ROUTE_ENABLE_EXPANDER_PIN            (6U)

#define CAN_ROUTE_SELECT_0_ACTIVE_LEVEL          (1)
#define CAN_ROUTE_SELECT_1_ACTIVE_LEVEL          (1)
#define CAN_ROUTE_ENABLE_ACTIVE_LEVEL            (1)

#define LCD_SWAP_XY   true
#define LCD_MIRROR_X  true
#define LCD_MIRROR_Y  false

#define TOUCH_SWAP_XY   true
#define TOUCH_MIRROR_X  false
#define TOUCH_MIRROR_Y  false

/*
 * Primary Classical CAN interface using the ESP32-S3 TWAI
 * controller and an external TCAN1042HGV transceiver.
 */
#define CAN_PRIMARY_PIN_TX            GPIO_NUM_5
#define CAN_PRIMARY_PIN_RX            GPIO_NUM_6

/*
 * Secondary CAN FD interface using a dedicated SPI3 bus,
 * MCP2518FD controller and TCAN1042HGV transceiver.
 */
#define CAN_FD_SPI_HOST               SPI3_HOST
#define CAN_FD_SPI_CLOCK_HZ           (8 * 1000 * 1000)

#define CAN_FD_PIN_MOSI               GPIO_NUM_11
#define CAN_FD_PIN_MISO               GPIO_NUM_14
#define CAN_FD_PIN_SCLK               GPIO_NUM_12
#define CAN_FD_PIN_CS                 GPIO_NUM_10
#define CAN_FD_PIN_INT                GPIO_NUM_7

#define CAN_FD_EXTERNAL_OSCILLATOR_HZ (20U * 1000U * 1000U)
#define CAN_FD_SYSTEM_CLOCK_HZ        (20U * 1000U * 1000U)

/*
 * Passive low-level-trigger buzzer.
 */
#define BUZZER_PIN_SIGNAL              GPIO_NUM_8
#define BUZZER_ACTIVE_LEVEL            (0)
#define BUZZER_INACTIVE_LEVEL          (1)
#define BUZZER_LEDC_TIMER              LEDC_TIMER_1
#define BUZZER_LEDC_CHANNEL            LEDC_CHANNEL_1
#define BUZZER_LEDC_SPEED_MODE         LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_DUTY_RESOLUTION    LEDC_TIMER_10_BIT
#define BUZZER_LEDC_DUTY_50_PERCENT    (512U)
#define BUZZER_DEFAULT_FREQUENCY_HZ    (2000U)

/*
 * Battery-voltage measurement.
 *
 * GPIO4 corresponds to ADC1 channel 3 on ESP32-S3.
 */
#define BATTERY_PIN_VOLTAGE            GPIO_NUM_4

#define BATTERY_DIVIDER_HIGH_OHM       (100000U)
#define BATTERY_DIVIDER_LOW_OHM        (100000U)

#define BATTERY_FILTER_CAPACITOR_NF    (100U)
