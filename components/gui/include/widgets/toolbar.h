#pragma once

#include <stdbool.h>

#include "lvgl.h"

typedef void (*toolbar_action_cb_t)(void);

typedef struct
{
    const char *title;

    const lv_image_dsc_t *left_icon;
    toolbar_action_cb_t left_action;

    const lv_image_dsc_t *right_icon;
    toolbar_action_cb_t right_action;

    bool show_sd_status;
    bool show_ota_status;

} toolbar_config_t;

typedef struct
{
    lv_obj_t *root;
    lv_obj_t *sd_icon;
    lv_obj_t *ota_icon;

} toolbar_t;

toolbar_t toolbar_create(
    lv_obj_t *parent,
    const toolbar_config_t *config
);

void toolbar_set_sd_mounted(
    toolbar_t *toolbar,
    bool mounted
);

void toolbar_set_ota_available(
    toolbar_t *toolbar,
    bool available
);