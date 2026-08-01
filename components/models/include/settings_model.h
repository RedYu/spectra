#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_DEVICE_NAME_MAX_LENGTH          (32U)
#define SETTINGS_DEVICE_TARGET_MAX_LENGTH        (32U)

#define SETTINGS_DISPLAY_BRIGHTNESS_MIN          (10U)
#define SETTINGS_DISPLAY_BRIGHTNESS_MAX          (100U)
#define SETTINGS_DISPLAY_BRIGHTNESS_DEFAULT      (80U)

#define SETTINGS_LOGGING_SD_ENABLED_DEFAULT      (false)
#define SETTINGS_UI_ANIMATIONS_ENABLED_DEFAULT   (false)

typedef struct
{
    char target[SETTINGS_DEVICE_TARGET_MAX_LENGTH];
    char name[SETTINGS_DEVICE_NAME_MAX_LENGTH];

} device_settings_t;

typedef struct
{
    uint8_t brightness;

} display_settings_t;

typedef struct
{
    bool sd_enabled;

} logging_settings_t;

typedef struct
{
    bool animations_enabled;

} ui_settings_t;

typedef struct
{
    uint32_t schema_version;

    device_settings_t device;
    display_settings_t display;
    logging_settings_t logging;
    ui_settings_t ui;

} app_settings_t;

/**
 * @brief Initialize the settings model and its synchronization resources.
 *
 * @return ESP_OK on success, ESP_ERR_NO_MEM if synchronization
 * resources cannot be created, otherwise an ESP-IDF error code.
 */
esp_err_t settings_model_init(void);

/**
 * @brief Fill a settings structure with default values.
 *
 * @param[out] settings Settings structure to initialize.
 *
 * @return ESP_OK on success or ESP_ERR_INVALID_ARG if settings is NULL.
 */
esp_err_t settings_model_set_defaults(
    app_settings_t *settings
);

/**
 * @brief Validate and store the current application settings.
 *
 * @param[in] settings Settings to store.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if settings is NULL,
 * ESP_ERR_INVALID_STATE if the model is not initialized, or
 * ESP_ERR_TIMEOUT if the model lock cannot be acquired.
 */
esp_err_t settings_model_set(
    const app_settings_t *settings
);

/**
 * @brief Copy the current application settings.
 *
 * @param[out] settings Destination settings structure.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if settings is NULL,
 * ESP_ERR_INVALID_STATE if the model is not initialized, or
 * ESP_ERR_TIMEOUT if the model lock cannot be acquired.
 */
esp_err_t settings_model_get(
    app_settings_t *settings
);

#ifdef __cplusplus
}
#endif
