#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the SD card management dialog.
 *
 * The dialog checks the current SD card state and allows the user
 * to mount or unmount the card.
 *
 * @param parent Parent object, normally lv_screen_active().
 */
void sd_card_modal_open(
    lv_obj_t *parent
);

#ifdef __cplusplus
}
#endif
