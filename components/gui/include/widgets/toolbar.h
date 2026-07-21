#pragma once

#include "lvgl.h"

typedef void (*toolbar_action_cb_t)(void);

typedef struct
{
    const char *title;

    const char *left_icon;
    toolbar_action_cb_t left_action;

    const char *right_icon;
    toolbar_action_cb_t right_action;

} toolbar_config_t;

lv_obj_t *toolbar_create(
    lv_obj_t *parent,
    const toolbar_config_t *config
);
