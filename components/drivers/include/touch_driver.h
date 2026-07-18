#pragma once

#include "esp_err.h"
#include "esp_lcd_touch.h"

esp_err_t touch_driver_init(void);

esp_lcd_touch_handle_t touch_driver_get_handle(void);

esp_err_t touch_driver_read(
    uint16_t *x,
    uint16_t *y,
    bool *pressed
);