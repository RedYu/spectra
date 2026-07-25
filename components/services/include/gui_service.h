#pragma once

#include "esp_err.h"

esp_err_t gui_service_init(void);
esp_err_t gui_service_start(void);

void gui_service_set_boot_progress(
    uint8_t progress,
    const char *status
);
