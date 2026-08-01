#pragma once

#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the splash-screen LVGL object.
 *
 * Must be called from the GUI task.
 *
 * @return Created screen object, or NULL if creation fails.
 */
lv_obj_t *splash_screen_create(void);

/**
 * @brief Update splash-screen progress and status text.
 *
 * Must be called from the GUI task after the screen has been created.
 * The progress value is limited to the range from 0 to 100.
 *
 * @param[in] progress Boot progress percentage.
 * @param[in] status Null-terminated status text. Must not be NULL.
 */
void splash_screen_set_progress(
    uint8_t progress,
    const char *status
);

/**
 * @brief Handle the splash screen becoming active.
 *
 * @param[in] screen Splash-screen LVGL object.
 */
void splash_screen_on_show(
    lv_obj_t *screen
);

/**
 * @brief Handle the splash screen becoming inactive.
 *
 * @param[in] screen Splash-screen LVGL object.
 */
void splash_screen_on_hide(
    lv_obj_t *screen
);

/**
 * @brief Destroy the splash screen and release its resources.
 *
 * The function must delete the supplied LVGL screen object and clear
 * all internally stored pointers referencing its child objects.
 *
 * @param[in] screen Splash-screen LVGL object.
 */
void splash_screen_destroy(
    lv_obj_t *screen
);

#ifdef __cplusplus
}
#endif
