#pragma once

#include <stdint.h>

#include "lvgl.h"

lv_obj_t *splash_screen_create(void);

void splash_screen_set_progress(
    uint8_t progress,
    const char *status
);
