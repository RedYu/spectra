#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the main application screen.
 *
 * Must be called from the GUI task.
 *
 * @return Created LVGL screen object, or NULL if creation fails.
 */
lv_obj_t *main_screen_create(void);

/**
 * @brief Handle activation of the main screen.
 *
 * Starts or refreshes activity associated with the screen.
 *
 * @param[in] screen Main LVGL screen object.
 */
void main_screen_on_show(
    lv_obj_t *screen
);

/**
 * @brief Handle deactivation of the main screen.
 *
 * Stops timers and other activity that should not continue while the
 * screen is inactive.
 *
 * @param[in] screen Main LVGL screen object.
 */
void main_screen_on_hide(
    lv_obj_t *screen
);

/**
 * @brief Destroy the main screen.
 *
 * This callback is responsible for deleting the supplied LVGL screen
 * object and releasing screen-specific resources.
 *
 * @param[in] screen Main LVGL screen object.
 */
void main_screen_destroy(
    lv_obj_t *screen
);

#ifdef __cplusplus
}
#endif
