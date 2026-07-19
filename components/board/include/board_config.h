#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"

#define BOARD_HARDWARE_VERSION "Rev A"

#define LCD_H_RES              480
#define LCD_V_RES              320

#define LCD_SPI_HOST           SPI2_HOST
#define LCD_SPI_CLOCK_HZ       (60 * 1000 * 1000)
#define LCD_PIN_MOSI           GPIO_NUM_15
#define LCD_PIN_MISO           GPIO_NUM_16
#define LCD_PIN_SCLK           GPIO_NUM_17
#define LCD_PIN_CS             GPIO_NUM_18
#define LCD_PIN_DC             GPIO_NUM_3
#define LCD_PIN_RST            GPIO_NUM_38
#define LCD_PIN_BACKLIGHT      GPIO_NUM_21
#define LCD_PIN_BUSY           GPIO_NUM_NC

#define TOUCH_I2C_PORT         I2C_NUM_0
#define TOUCH_I2C_FREQ_HZ      400000
#define TOUCH_PIN_SDA          GPIO_NUM_1
#define TOUCH_PIN_SCL          GPIO_NUM_2
#define TOUCH_PIN_INT          GPIO_NUM_13
#define TOUCH_PIN_RST          GPIO_NUM_NC

#define LCD_BACKLIGHT_ON_LEVEL     1
#define LCD_BACKLIGHT_OFF_LEVEL    0

#define LCD_DRAW_BUFFER_LINES       40
#define LV_BUFFER_SIZE (LCD_H_RES * LCD_DRAW_BUFFER_LINES)


/*
 * LVGL renders the screen in partial updates.
 *
 * 40 lines:
 * 480 × 40 × 2 = 38,400 bytes per RGB565 draw buffer.
 */
#define LVGL_DRAW_BUFFER_LINES 40

#define LVGL_TICK_PERIOD_MS    2
#define LVGL_HANDLER_MIN_MS    2
#define LVGL_HANDLER_MAX_MS    20

#define LCD_SWAP_XY   true
#define LCD_MIRROR_X  true
#define LCD_MIRROR_Y  false

#define TOUCH_SWAP_XY   true
#define TOUCH_MIRROR_X  false
#define TOUCH_MIRROR_Y  false