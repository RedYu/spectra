#pragma once

#include "esp_err.h"
#include "lvgl.h"

esp_err_t lvgl_port_init(void);

/*
 * Call this function continuously from the GUI task only.
 * Returns the recommended delay before the next call.
 */
uint32_t lvgl_port_handler(void);

lv_display_t *lvgl_port_get_display(void);
lv_indev_t *lvgl_port_get_touch(void);
