#pragma once

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the SD-card management dialog.
 *
 * The dialog reads the current SD-card state and allows the user to
 * mount or unmount the card.
 *
 * Must be called from the GUI task because it accesses LVGL objects
 * directly. Only one SD-card dialog may be open at a time.
 *
 * @param[in] parent Parent object, normally lv_screen_active().
 *
 * @return true if the dialog was opened successfully or was already
 * open; otherwise false.
 */
bool sd_card_modal_open(
    lv_obj_t *parent
);

#ifdef __cplusplus
}
#endif
