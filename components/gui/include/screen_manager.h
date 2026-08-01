#pragma once

/**
 * @file screen_manager.h
 * @brief Manage application screens and navigation history.
 *
 * All screen-manager functions and callbacks must be called from the
 * GUI task because they access LVGL objects directly.
 */

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
 *
 * @return Created LVGL screen object, or NULL if creation fails.
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
 *
 * The create callback is required. Lifecycle and destroy callbacks are
 * optional. When destroy is NULL, the screen manager must delete the
 * LVGL object using lv_obj_delete().
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
 *
 * If the requested screen is already active, the function has no
 * effect. The screen is created when it is not currently cached.
 *
 * @param[in] id Screen identifier to show.
 * @param[in] animation LVGL screen-load animation.
 * @param[in] animation_time_ms Animation duration in milliseconds.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if id is invalid,
 * ESP_ERR_INVALID_STATE if the manager is not initialized,
 * ESP_ERR_NO_MEM if the screen or history entry cannot be created,
 * otherwise an ESP-IDF error code.
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
 * The active screen cannot be destroyed. The screen will be created
 * again when it is next shown.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if id is invalid,
 * ESP_ERR_INVALID_STATE if the manager is not initialized or the
 * requested screen is active, otherwise an ESP-IDF error code.
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
