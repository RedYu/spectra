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
#define CAN_FD_SPI_CLOCK_HZ           (10 * 1000 * 1000)

#define CAN_FD_PIN_MOSI               GPIO_NUM_11
#define CAN_FD_PIN_MISO               GPIO_NUM_14
#define CAN_FD_PIN_SCLK               GPIO_NUM_12
#define CAN_FD_PIN_CS                 GPIO_NUM_10
#define CAN_FD_PIN_INT                GPIO_NUM_7

/*
 * Passive low-level-trigger buzzer.
 */
#define BUZZER_PIN_SIGNAL             GPIO_NUM_8
#define BUZZER_ACTIVE_LEVEL           (0)
#define BUZZER_INACTIVE_LEVEL         (1)

/*
 * Battery-voltage measurement.
 *
 * GPIO4 corresponds to ADC1 channel 3 on ESP32-S3.
 */
#define BATTERY_PIN_VOLTAGE            GPIO_NUM_4

#define BATTERY_DIVIDER_HIGH_OHM       (100000U)
#define BATTERY_DIVIDER_LOW_OHM        (100000U)

#define BATTERY_FILTER_CAPACITOR_NF    (100U)
