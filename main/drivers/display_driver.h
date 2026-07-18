#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

esp_err_t display_driver_init(void);

esp_err_t display_driver_set_backlight(bool enabled);

esp_lcd_panel_handle_t display_driver_get_panel(void);

esp_lcd_panel_io_handle_t display_driver_get_panel_io(void);
