/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "screen_manager.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"

#define SCREEN_HISTORY_SIZE  (4U)

static const char *TAG = "screen_manager";

typedef struct
{
    lv_obj_t *object;

} screen_instance_t;

static screen_desc_t s_descriptors[SCREEN_ID_COUNT];
static screen_instance_t s_instances[SCREEN_ID_COUNT];

static screen_id_t s_current =
    SCREEN_ID_INVALID;

static screen_id_t s_history[SCREEN_HISTORY_SIZE];
static uint8_t s_history_count = 0U;

static bool s_initialized = false;
static bool s_transition_in_progress = false;

static void screen_transition_event_cb(
    lv_event_t *event
)
{
    if (lv_event_get_code(event) ==
        LV_EVENT_SCREEN_LOADED) {

        s_transition_in_progress = false;
    }
}

static bool screen_id_is_valid(
    screen_id_t id
)
{
    return (id >= SCREEN_ID_SPLASH) &&
           (id < SCREEN_ID_COUNT);
}

static const char *screen_get_name(
    screen_id_t id
)
{
    if (!screen_id_is_valid(id)) {
        return "invalid";
    }

    const char *name =
        s_descriptors[id].name;

    return name != NULL
        ? name
        : "unnamed";
}

static void history_push(
    screen_id_t id
)
{
    if (!screen_id_is_valid(id)) {
        return;
    }

    /*
     * Avoid duplicate adjacent history entries.
     */
    if ((s_history_count > 0U) &&
        (s_history[s_history_count - 1U] == id)) {
        return;
    }

    /*
     * Remove the oldest history entry when full.
     */
    if (s_history_count >= SCREEN_HISTORY_SIZE) {
        memmove(
            &s_history[0],
            &s_history[1],
            (SCREEN_HISTORY_SIZE - 1U) *
                sizeof(screen_id_t)
        );

        s_history_count =
            SCREEN_HISTORY_SIZE - 1U;
    }

    s_history[s_history_count++] =
        id;
}

static lv_obj_t *screen_get_or_create(
    screen_id_t id
)
{
    screen_instance_t *instance =
        &s_instances[id];

    const screen_desc_t *descriptor =
        &s_descriptors[id];

    /*
     * Return the cached screen.
     */
    if (instance->object != NULL) {
        return instance->object;
    }

    instance->object =
        descriptor->create();

    if (instance->object == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create screen: %s",
            screen_get_name(id)
        );

        return NULL;
    }

    lv_obj_add_event_cb(
        instance->object,
        screen_transition_event_cb,
        LV_EVENT_SCREEN_LOADED,
        NULL
    );

    ESP_LOGD(
        TAG,
        "Created screen: %s",
        screen_get_name(id)
    );

    return instance->object;
}

static esp_err_t screen_manager_load(
    screen_id_t id,
    lv_screen_load_anim_t animation,
    uint32_t animation_time_ms,
    bool add_current_to_history
)
{
    ESP_RETURN_ON_FALSE(
        s_initialized,
        ESP_ERR_INVALID_STATE,
        TAG,
        "Screen manager is not initialized"
    );

    ESP_RETURN_ON_FALSE(
        screen_id_is_valid(id),
        ESP_ERR_INVALID_ARG,
        TAG,
        "Invalid screen ID"
    );

    const screen_desc_t *next_descriptor =
        &s_descriptors[id];

    ESP_RETURN_ON_FALSE(
        next_descriptor->create != NULL,
        ESP_ERR_INVALID_STATE,
        TAG,
        "Screen is not registered"
    );

    if (s_current == id) {
        ESP_LOGD(
            TAG,
            "Screen is already active: %s",
            screen_get_name(id)
        );

        return ESP_OK;
    }

    ESP_RETURN_ON_FALSE(
        !s_transition_in_progress,
        ESP_ERR_INVALID_STATE,
        TAG,
        "Another screen transition is in progress"
    );

    lv_obj_t *next_screen =
        screen_get_or_create(id);

    if (next_screen == NULL) {
        return ESP_FAIL;
    }

    /*
     * Block nested navigation from lifecycle callbacks.
     * No failing operations remain after this point.
     */
    s_transition_in_progress = true;

    const screen_id_t previous_id =
        s_current;

    lv_obj_t *previous_screen =
        NULL;

    if (screen_id_is_valid(previous_id)) {
        previous_screen =
            s_instances[previous_id].object;
    }

    if (add_current_to_history &&
        screen_id_is_valid(previous_id)) {

        history_push(previous_id);
    }

    /*
     * Stop timers or other activity belonging to the old screen.
     */
    if (screen_id_is_valid(previous_id)) {
        const screen_desc_t *previous_descriptor =
            &s_descriptors[previous_id];

        if (previous_descriptor->on_hide != NULL &&
            previous_screen != NULL) {

            previous_descriptor->on_hide(
                previous_screen
            );
        }
    }

    /*
     * Cached screens must not be deleted by LVGL.
     */
    lv_screen_load_anim(
        next_screen,
        animation,
        animation_time_ms,
        0U,
        false
    );

    s_current =
        id;

    /*
     * Refresh or restart the new screen.
     */
    if (next_descriptor->on_show != NULL) {
        next_descriptor->on_show(
            next_screen
        );
    }

    ESP_LOGD(
        TAG,
        "Showing screen: %s",
        screen_get_name(id)
    );

    return ESP_OK;
}

esp_err_t screen_manager_init(void)
{
    ESP_RETURN_ON_FALSE(
        !s_initialized,
        ESP_ERR_INVALID_STATE,
        TAG,
        "Screen manager is already initialized"
    );

    memset(
        s_descriptors,
        0,
        sizeof(s_descriptors)
    );

    memset(
        s_instances,
        0,
        sizeof(s_instances)
    );

    memset(
        s_history,
        0,
        sizeof(s_history)
    );

    s_current =
        SCREEN_ID_INVALID;

    s_history_count =
        0U;

    s_transition_in_progress =
        false;

    s_initialized =
        true;

    ESP_LOGI(
        TAG,
        "Screen manager initialized"
    );

    return ESP_OK;
}

esp_err_t screen_manager_register(
    const screen_desc_t *descriptor
)
{
    ESP_RETURN_ON_FALSE(
        s_initialized,
        ESP_ERR_INVALID_STATE,
        TAG,
        "Screen manager is not initialized"
    );

    ESP_RETURN_ON_FALSE(
        descriptor != NULL,
        ESP_ERR_INVALID_ARG,
        TAG,
        "Descriptor is NULL"
    );

    ESP_RETURN_ON_FALSE(
        screen_id_is_valid(descriptor->id),
        ESP_ERR_INVALID_ARG,
        TAG,
        "Invalid screen ID"
    );

    ESP_RETURN_ON_FALSE(
        descriptor->create != NULL,
        ESP_ERR_INVALID_ARG,
        TAG,
        "Create callback is NULL"
    );

    ESP_RETURN_ON_FALSE(
        s_descriptors[descriptor->id].create == NULL,
        ESP_ERR_INVALID_STATE,
        TAG,
        "Screen is already registered"
    );

    s_descriptors[descriptor->id] =
        *descriptor;

    ESP_LOGI(
        TAG,
        "Registered screen: %s (id=%d)",
        screen_get_name(descriptor->id),
        descriptor->id
    );

    return ESP_OK;
}

esp_err_t screen_manager_show(
    screen_id_t id,
    lv_screen_load_anim_t animation,
    uint32_t animation_time_ms
)
{
    return screen_manager_load(
        id,
        animation,
        animation_time_ms,
        true
    );
}

esp_err_t screen_manager_back(
    lv_screen_load_anim_t animation,
    uint32_t animation_time_ms
)
{
    ESP_RETURN_ON_FALSE(
        s_initialized,
        ESP_ERR_INVALID_STATE,
        TAG,
        "Screen manager is not initialized"
    );

    if (s_history_count == 0U) {
        ESP_LOGW(
            TAG,
            "No previous screen in history"
        );

        return ESP_ERR_NOT_FOUND;
    }

    const screen_id_t previous_id =
        s_history[s_history_count - 1U];

    const esp_err_t result =
        screen_manager_load(
            previous_id,
            animation,
            animation_time_ms,
            false
        );

    if (result != ESP_OK) {
        return result;
    }

    /*
     * Remove the history entry only after successful navigation.
     */
    s_history_count--;

    s_history[s_history_count] =
        SCREEN_ID_INVALID;

    return ESP_OK;
}

esp_err_t screen_manager_destroy(
    screen_id_t id
)
{
    ESP_RETURN_ON_FALSE(
        s_initialized,
        ESP_ERR_INVALID_STATE,
        TAG,
        "Screen manager is not initialized"
    );

    ESP_RETURN_ON_FALSE(
        screen_id_is_valid(id),
        ESP_ERR_INVALID_ARG,
        TAG,
        "Invalid screen ID"
    );

    ESP_RETURN_ON_FALSE(
        id != s_current,
        ESP_ERR_INVALID_STATE,
        TAG,
        "Cannot destroy the active screen"
    );

    ESP_RETURN_ON_FALSE(
        !s_transition_in_progress,
        ESP_ERR_INVALID_STATE,
        TAG,
        "Cannot destroy a screen during transition"
    );

    screen_instance_t *instance =
        &s_instances[id];

    if (instance->object == NULL) {
        return ESP_OK;
    }

    const screen_desc_t *descriptor =
        &s_descriptors[id];

    if (descriptor->destroy != NULL) {
        descriptor->destroy(
            instance->object
        );
    } else {
        lv_obj_delete(
            instance->object
        );
    }

    instance->object =
        NULL;

    ESP_LOGD(
        TAG,
        "Destroyed screen: %s",
        screen_get_name(id)
    );

    return ESP_OK;
}

void screen_manager_clear_history(void)
{
    s_history_count = 0U;

    memset(
        s_history,
        0,
        sizeof(s_history)
    );

    ESP_LOGD(
        TAG,
        "Screen history cleared"
    );
}

bool screen_manager_can_go_back(void)
{
    return s_history_count > 0U;
}

screen_id_t screen_manager_get_current(void)
{
    return s_current;
}

lv_obj_t *screen_manager_get_current_object(void)
{
    if (!screen_id_is_valid(s_current)) {
        return NULL;
    }

    return s_instances[s_current].object;
}
