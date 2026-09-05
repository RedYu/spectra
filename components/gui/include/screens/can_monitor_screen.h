#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file can_monitor_screen.h
 * @brief Live CAN traffic monitor screen.
 */

/**
 * @brief Create the CAN monitor screen.
 *
 * Must be called from the GUI task.
 *
 * @return Created LVGL screen object, or NULL if creation fails.
 */
lv_obj_t *can_monitor_screen_create(void);

/**
 * @brief Activate the CAN monitor screen.
 *
 * Refreshes the displayed CAN data and starts periodic UI updates.
 *
 * @param[in] screen CAN monitor LVGL screen object.
 */
void can_monitor_screen_on_show(
    lv_obj_t *screen
);

/**
 * @brief Deactivate the CAN monitor screen.
 *
 * Stops periodic UI updates. CAN reception and monitoring services
 * remain running.
 *
 * @param[in] screen CAN monitor LVGL screen object.
 */
void can_monitor_screen_on_hide(
    lv_obj_t *screen
);

/**
 * @brief Destroy the CAN monitor screen.
 *
 * Deletes the LVGL screen and releases screen-specific resources.
 *
 * @param[in] screen CAN monitor LVGL screen object.
 */
void can_monitor_screen_destroy(
    lv_obj_t *screen
);

#ifdef __cplusplus
}
#endif
