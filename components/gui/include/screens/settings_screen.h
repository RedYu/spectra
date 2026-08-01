#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the settings-screen LVGL object.
 *
 * Must be called from the GUI task.
 *
 * @return Created screen object, or NULL if creation fails.
 */
lv_obj_t *settings_screen_create(void);

/**
 * @brief Handle the settings screen becoming active.
 *
 * Refreshes controls using the current application settings.
 *
 * @param[in] screen Settings-screen LVGL object.
 */
void settings_screen_on_show(
    lv_obj_t *screen
);

/**
 * @brief Handle the settings screen becoming inactive.
 *
 * @param[in] screen Settings-screen LVGL object.
 */
void settings_screen_on_hide(
    lv_obj_t *screen
);

/**
 * @brief Destroy the settings screen and release its resources.
 *
 * The function must delete the supplied LVGL screen object and clear
 * all internally stored pointers referencing its child objects.
 *
 * @param[in] screen Settings-screen LVGL object.
 */
void settings_screen_destroy(
    lv_obj_t *screen
);

#ifdef __cplusplus
}
#endif
