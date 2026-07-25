#pragma once

#include <stdint.h>

#include "lvgl.h"

lv_obj_t *splash_screen_create(void);

void splash_screen_set_progress(
    uint8_t progress,
    const char *status
);

void splash_screen_on_show(
    lv_obj_t *screen
);

void splash_screen_on_hide(
    lv_obj_t *screen
);

void splash_screen_destroy(
    lv_obj_t *screen
);
