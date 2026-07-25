#pragma once

#include "lvgl.h"

lv_obj_t *main_screen_create(void);

void main_screen_on_show(
    lv_obj_t *screen
);

void main_screen_on_hide(
    lv_obj_t *screen
);

void main_screen_destroy(
    lv_obj_t *screen
);
