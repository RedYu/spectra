#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Available application screens.
 *
 * SCREEN_ID_COUNT is also used as an invalid screen identifier.
 */
typedef enum
{
    SCREEN_ID_SPLASH = 0,
    SCREEN_ID_MAIN,
    SCREEN_ID_SETTINGS,

    SCREEN_ID_COUNT,
    SCREEN_ID_INVALID = SCREEN_ID_COUNT

} screen_id_t;

/**
 * @brief Screen creation callback.
 *
 * The callback must create and return an LVGL screen object.
 */
typedef lv_obj_t *(*screen_create_cb_t)(void);

/**
 * @brief Screen lifecycle callback.
 */
typedef void (*screen_lifecycle_cb_t)(
    lv_obj_t *screen
);

/**
 * @brief Screen descriptor.
 */
typedef struct
{
    screen_id_t id;
    const char *name;

    screen_create_cb_t create;

    /**
     * Called every time the screen becomes active.
     */
    screen_lifecycle_cb_t on_show;

    /**
     * Called when the screen stops being active.
     */
    screen_lifecycle_cb_t on_hide;

    /**
     * Optional custom destroy callback.
     *
     * The callback is responsible for deleting the LVGL screen object.
     */
    screen_lifecycle_cb_t destroy;

} screen_desc_t;

/**
 * @brief Initialize the screen manager.
 *
 * Must be called once from the LVGL task.
 */
esp_err_t screen_manager_init(void);

/**
 * @brief Register a screen descriptor.
 *
 * Must be called before showing the screen.
 */
esp_err_t screen_manager_register(
    const screen_desc_t *descriptor
);

/**
 * @brief Show a screen and add the current screen to history.
 */
esp_err_t screen_manager_show(
    screen_id_t id,
    lv_screen_load_anim_t animation,
    uint32_t animation_time_ms
);

/**
 * @brief Return to the previous screen.
 *
 * The current screen is not added back to history.
 */
esp_err_t screen_manager_back(
    lv_screen_load_anim_t animation,
    uint32_t animation_time_ms
);

/**
 * @brief Destroy a cached inactive screen.
 *
 * The screen will be created again the next time it is shown.
 */
esp_err_t screen_manager_destroy(
    screen_id_t id
);

/**
 * @brief Clear navigation history.
 */
void screen_manager_clear_history(void);

/**
 * @brief Return true if Back navigation is available.
 */
bool screen_manager_can_go_back(void);

/**
 * @brief Get the active screen identifier.
 */
screen_id_t screen_manager_get_current(void);

/**
 * @brief Get the active LVGL screen object.
 */
lv_obj_t *screen_manager_get_current_object(void);

#ifdef __cplusplus
}
#endif
