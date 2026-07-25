#pragma once

#include "lvgl.h"

lv_obj_t *settings_screen_create(void);

void settings_screen_on_show(
    lv_obj_t *screen
);

void settings_screen_on_hide(
    lv_obj_t *screen
);

void settings_screen_destroy(
    lv_obj_t *screen
);
