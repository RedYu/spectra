#include "settings_service.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "display_backlight.h"

#include "system_model.h"
#include "settings_model.h"
#include "storage_service.h"
#include "logging_service.h"
#include "sd_card_driver.h"

#include "gui_config.h"

static const char *TAG = "settings_service";

static const char *CONFIG_FILE_PATH =
    "/storage/device_config.json";

static const char *CONFIG_FILE_PATH_TMP =
    "/storage/device_config.json.tmp";

static const uint32_t SUPPORTED_SCHEMA_VERSION = 1;

static void parse_string(
    const cJSON *object,
    const char *name,
    char *destination,
    size_t destination_size
)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);

    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return;
    }

    strlcpy(destination, item->valuestring, destination_size);
}

static void parse_bool(
    const cJSON *object,
    const char *name,
    bool *destination
)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);

    if (!cJSON_IsBool(item)) {
        return;
    }

    *destination = cJSON_IsTrue(item);
}

static void parse_u8_range(
    const cJSON *object,
    const char *name,
    uint8_t *destination,
    uint8_t minimum,
    uint8_t maximum
)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);

    if (!cJSON_IsNumber(item)) {
        return;
    }

    const double value = item->valuedouble;

    if (value < minimum || value > maximum) {
        ESP_LOGW(
            TAG,
            "Value '%s' is outside the allowed range",
            name
        );

        return;
    }

    *destination = (uint8_t)value;
}

static void parse_u16_range(
    const cJSON *object,
    const char *name,
    uint16_t *destination,
    uint16_t minimum,
    uint16_t maximum
)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);

    if (!cJSON_IsNumber(item)) {
        return;
    }

    const double value = item->valuedouble;

    if (value < minimum || value > maximum) {
        ESP_LOGW(
            TAG,
            "Value '%s' is outside the allowed range",
            name
        );

        return;
    }

    *destination = (uint16_t)value;
}

static void parse_u32(
    const cJSON *object,
    const char *name,
    uint32_t *destination
)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);

    if (!cJSON_IsNumber(item) || item->valuedouble < 0) {
        return;
    }

    *destination = (uint32_t)item->valuedouble;
}

static esp_err_t parse_config(
    const char *json_text,
    app_settings_t *settings
)
{
    if (json_text == NULL || settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ESP_OK;

    cJSON *root =
        cJSON_Parse(json_text);

    if (root == NULL) {
        const char *error_position =
            cJSON_GetErrorPtr();

        ESP_LOGE(
            TAG,
            "Invalid JSON near: %s",
            error_position != NULL
                ? error_position
                : "unknown position"
        );

        return ESP_ERR_INVALID_RESPONSE;
    }

    if (!cJSON_IsObject(root)) {
        ESP_LOGE(
            TAG,
            "Configuration root must be a JSON object"
        );

        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    /*
     * Parse into a temporary object so the current settings remain
     * unchanged if validation fails.
     */
    app_settings_t parsed_settings;

    settings_model_set_defaults(
        &parsed_settings
    );

    /*
     * Validate schema version.
     */
    const cJSON *schema_version =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "schema_version"
        );

    if (!cJSON_IsNumber(schema_version)) {
        ESP_LOGE(
            TAG,
            "Missing or invalid schema_version"
        );

        result = ESP_ERR_INVALID_VERSION;
        goto cleanup;
    }

    /*
     * Schema version must be a non-negative integer.
     */
    if (schema_version->valuedouble < 0 ||
        schema_version->valuedouble !=
            (double)schema_version->valueint) {

        ESP_LOGE(
            TAG,
            "schema_version must be an integer"
        );

        result = ESP_ERR_INVALID_VERSION;
        goto cleanup;
    }

    const uint32_t version =
        (uint32_t)schema_version->valueint;

    if (version != SUPPORTED_SCHEMA_VERSION) {
        ESP_LOGE(
            TAG,
            "Unsupported schema version: %u",
            (unsigned int)version
        );

        result = ESP_ERR_INVALID_VERSION;
        goto cleanup;
    }

    /*
     * Validate device configuration.
     */
    const cJSON *device =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "device"
        );

    if (!cJSON_IsObject(device)) {
        ESP_LOGE(
            TAG,
            "Missing device configuration"
        );

        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    const cJSON *device_target =
        cJSON_GetObjectItemCaseSensitive(
            device,
            "target"
        );

    if (!cJSON_IsString(device_target) ||
        device_target->valuestring == NULL) {

        ESP_LOGE(
            TAG,
            "Missing or invalid device.target"
        );

        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    system_model_t system_model;

    result =
        system_model_get_snapshot(
            &system_model
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to read system model: %s",
            esp_err_to_name(result)
        );

        goto cleanup;
    }

    if (strcmp(
            device_target->valuestring,
            system_model.device_id
        ) != 0) {

        ESP_LOGE(
            TAG,
            "Configuration device mismatch: "
            "expected='%s', received='%s'",
            system_model.device_id,
            device_target->valuestring
        );

        result = ESP_ERR_NOT_SUPPORTED;
        goto cleanup;
    }

    const cJSON *device_name =
        cJSON_GetObjectItemCaseSensitive(
            device,
            "name"
        );

    if (!cJSON_IsString(device_name) ||
        device_name->valuestring == NULL) {

        ESP_LOGE(
            TAG,
            "Missing or invalid device.name"
        );

        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    /*
     * Validate display configuration.
     */
    const cJSON *display =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "display"
        );

    if (!cJSON_IsObject(display)) {
        ESP_LOGE(
            TAG,
            "Missing display configuration"
        );

        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    const cJSON *brightness =
        cJSON_GetObjectItemCaseSensitive(
            display,
            "brightness"
        );

    if (!cJSON_IsNumber(brightness)) {
        ESP_LOGE(
            TAG,
            "Missing or invalid display.brightness"
        );

        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    /*
     * Brightness must be an integer.
     */
    if (brightness->valuedouble !=
        (double)brightness->valueint) {

        ESP_LOGE(
            TAG,
            "display.brightness must be an integer"
        );

        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    if (brightness->valueint <
            SETTINGS_DISPLAY_BRIGHTNESS_MIN ||
        brightness->valueint >
            SETTINGS_DISPLAY_BRIGHTNESS_MAX) {

        ESP_LOGE(
            TAG,
            "display.brightness is out of range: %d "
            "(allowed %d-%d)",
            brightness->valueint,
            SETTINGS_DISPLAY_BRIGHTNESS_MIN,
            SETTINGS_DISPLAY_BRIGHTNESS_MAX
        );

        result = ESP_ERR_INVALID_ARG;
        goto cleanup;
    }

    /*
     * All configuration values are valid.
     */
    parsed_settings.schema_version =
        version;

    strlcpy(
        parsed_settings.device.target,
        device_target->valuestring,
        sizeof(parsed_settings.device.target)
    );

    strlcpy(
        parsed_settings.device.name,
        device_name->valuestring,
        sizeof(parsed_settings.device.name)
    );

    parsed_settings.display.brightness =
        (uint8_t)brightness->valueint;

    /*
     * Validate logging configuration.
     */
    const cJSON *logging =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "logging"
        );

    if (!cJSON_IsObject(logging)) {
        ESP_LOGE(
            TAG,
            "Missing logging configuration"
        );

        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    const cJSON *sd_enabled =
        cJSON_GetObjectItemCaseSensitive(
            logging,
            "sd_enabled"
        );

    if (!cJSON_IsBool(sd_enabled)) {
        ESP_LOGE(
            TAG,
            "Missing or invalid logging.sd_enabled"
        );

        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    parsed_settings.logging.sd_enabled =
        cJSON_IsTrue(sd_enabled);

    /*
     * Validate UI configuration.
     */
    const cJSON *ui =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "ui"
        );

    if (!cJSON_IsObject(ui)) {
        ESP_LOGE(
            TAG,
            "Missing UI configuration"
        );

        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    const cJSON *animations_enabled =
        cJSON_GetObjectItemCaseSensitive(
            ui,
            "animations_enabled"
        );

    if (!cJSON_IsBool(animations_enabled)) {
        ESP_LOGE(
            TAG,
            "Missing or invalid ui.animations_enabled"
        );

        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    parsed_settings.ui.animations_enabled =
        cJSON_IsTrue(animations_enabled);

    /*
     * Apply the fully validated configuration.
     */
    *settings =
        parsed_settings;

    ESP_LOGI(
        TAG,
        "Configuration validated for device: %s",
        system_model.device_id
    );

    ESP_LOGD(
        TAG,
        "Display brightness: %u%%",
        parsed_settings.display.brightness
    );

    ESP_LOGD(
        TAG,
        "SD card logging: %s",
        parsed_settings.logging.sd_enabled
            ? "enabled"
            : "disabled"
    );

    ESP_LOGD(
        TAG,
        "UI animations: %s",
        parsed_settings.ui.animations_enabled
            ? "enabled"
            : "disabled"
    );

cleanup:
    cJSON_Delete(root);

    return result;
}

static cJSON *settings_service_create_json(
    const app_settings_t *settings
)
{
    if (settings == NULL) {
        return NULL;
    }

    cJSON *root =
        cJSON_CreateObject();

    if (root == NULL) {
        return NULL;
    }

    if (cJSON_AddNumberToObject(
            root,
            "schema_version",
            settings->schema_version
        ) == NULL) {

        goto error;
    }

    cJSON *device =
        cJSON_AddObjectToObject(
            root,
            "device"
        );

    if (device == NULL) {
        goto error;
    }

    if (cJSON_AddStringToObject(
            device,
            "target",
            settings->device.target
        ) == NULL) {

        goto error;
    }

    if (cJSON_AddStringToObject(
            device,
            "name",
            settings->device.name
        ) == NULL) {

        goto error;
    }

    cJSON *display =
        cJSON_AddObjectToObject(
            root,
            "display"
        );

    if (display == NULL) {
        goto error;
    }

    if (cJSON_AddNumberToObject(
            display,
            "brightness",
            settings->display.brightness
        ) == NULL) {

        goto error;
    }

    cJSON *logging =
        cJSON_AddObjectToObject(
            root,
            "logging"
        );

    if (logging == NULL) {
        goto error;
    }

    if (cJSON_AddBoolToObject(
            logging,
            "sd_enabled",
            settings->logging.sd_enabled
        ) == NULL) {

        goto error;
    }

    cJSON *ui =
        cJSON_AddObjectToObject(
            root,
            "ui"
        );

    if (ui == NULL) {
        goto error;
    }

    if (cJSON_AddBoolToObject(
            ui,
            "animations_enabled",
            settings->ui.animations_enabled
        ) == NULL) {

        goto error;
    }

    return root;

error:
    cJSON_Delete(root);
    return NULL;
}

esp_err_t settings_service_save(void)
{
    const app_settings_t *settings =
        settings_model_get();

    if (settings == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root =
        settings_service_create_json(
            settings
        );

    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    char *json_text =
        cJSON_Print(root);

    cJSON_Delete(root);

    if (json_text == NULL) {
        return ESP_ERR_NO_MEM;
    }

    FILE *file =
        fopen(
            CONFIG_FILE_PATH_TMP,
            "w"
        );

    if (file == NULL) {
        cJSON_free(json_text);
        return ESP_FAIL;
    }

    const size_t length =
        strlen(json_text);

    const size_t written =
        fwrite(
            json_text,
            1,
            length,
            file
        );

    esp_err_t result =
        ESP_OK;

    if (written != length) {
        result =
            ESP_FAIL;
    }

    if (fflush(file) != 0) {
        result =
            ESP_FAIL;
    }

    if (fclose(file) != 0) {
        result =
            ESP_FAIL;
    }

    cJSON_free(json_text);

    if (result != ESP_OK) {
        remove(
            CONFIG_FILE_PATH_TMP
        );

        ESP_LOGE(
            TAG,
            "Failed to write temporary settings file"
        );

        return result;
    }

    remove(
        CONFIG_FILE_PATH
    );

    if (rename(
            CONFIG_FILE_PATH_TMP,
            CONFIG_FILE_PATH
        ) != 0) {

        ESP_LOGE(
            TAG,
            "Failed to replace settings file"
        );

        remove(
            CONFIG_FILE_PATH_TMP
        );

        return ESP_FAIL;
    }

    ESP_LOGI(
        TAG,
        "Settings saved successfully"
    );

    return ESP_OK;
}

esp_err_t settings_service_reload(void)
{
    app_settings_t settings;

    /*
     * Always begin with a complete valid configuration.
     * Values found in JSON override these defaults.
     */
    settings_model_set_defaults(&settings);

    char *file_data = NULL;
    size_t file_size = 0;

    esp_err_t err = storage_service_read_file(
        CONFIG_FILE_PATH,
        &file_data,
        &file_size
    );

    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(
            TAG,
            "Configuration file not found, using defaults"
        );

        settings_model_set(&settings);
        return settings_service_apply();
    }

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to read configuration: %s",
            esp_err_to_name(err)
        );

        settings_model_set(&settings);
        settings_service_apply();
        return err;
    }

    ESP_LOGI(
        TAG,
        "Configuration file loaded: %u bytes",
        (unsigned int)file_size
    );

    err = parse_config(file_data, &settings);

    free(file_data);

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Invalid configuration, using defaults"
        );

        settings_model_set_defaults(&settings);
        settings_model_set(&settings);
        settings_service_apply();

        return err;
    }

    settings_model_set(&settings);

    ESP_LOGI(TAG, "Configuration applied successfully");

    return settings_service_apply();
}

esp_err_t settings_service_init(void)
{
    return settings_service_reload();
}

esp_err_t settings_service_set_brightness(
    uint8_t brightness
)
{
    const app_settings_t *current =
        settings_model_get();

    if (current == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    app_settings_t updated =
        *current;

    updated.display.brightness =
        brightness;

    settings_model_set(
        &updated
    );

    const app_settings_t *applied =
        settings_model_get();

    return display_backlight_set_brightness(
        applied->display.brightness
    );
}

esp_err_t settings_service_set_sd_logging_enabled(
    bool enabled
)
{
    const app_settings_t *current =
        settings_model_get();

    if (current == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    app_settings_t updated =
        *current;

    updated.logging.sd_enabled =
        enabled;

    settings_model_set(
        &updated
    );

    const app_settings_t *applied =
        settings_model_get();

    if (applied == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (applied->logging.sd_enabled) {
        if (sd_card_driver_is_mounted()) {
            return logging_service_enable_file();
        }

        return ESP_OK;
    }

    return logging_service_disable_file();
}

esp_err_t settings_service_set_animations_enabled(
    bool enabled
)
{
    const app_settings_t *current =
        settings_model_get();

    if (current == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    app_settings_t updated =
        *current;

    updated.ui.animations_enabled =
        enabled;

    settings_model_set(
        &updated
    );

    const app_settings_t *applied =
        settings_model_get();

    if (applied == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    gui_config_set_animations_enabled(
        applied->ui.animations_enabled
    );

    return ESP_OK;
}

esp_err_t settings_service_apply(void)
{
    const app_settings_t *settings =
        settings_model_get();

    if (settings == NULL) {
        ESP_LOGE(
            TAG,
            "Settings model is unavailable"
        );

        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(
        display_backlight_set_brightness(
            settings->display.brightness
        ),
        TAG,
        "Failed to apply display brightness"
    );

    gui_config_set_animations_enabled(
        settings->ui.animations_enabled
    );

    if (!settings->logging.sd_enabled) {
        if (logging_service_is_file_enabled()) {
            ESP_RETURN_ON_ERROR(
                logging_service_disable_file(),
                TAG,
                "Failed to disable SD logging"
            );
        }
    } else if (sd_card_driver_is_mounted()) {
        if (!logging_service_is_file_enabled()) {
            ESP_RETURN_ON_ERROR(
                logging_service_enable_file(),
                TAG,
                "Failed to enable SD logging"
            );
        }
    } else {
        ESP_LOGW(
            TAG,
            "SD logging is enabled in settings, "
            "but the SD card is not mounted"
        );
    }

    ESP_LOGI(
        TAG,
        "Settings applied: brightness=%u%%, "
        "SD logging=%s, animations=%s",
        settings->display.brightness,
        settings->logging.sd_enabled
            ? "enabled"
            : "disabled",
        settings->ui.animations_enabled
            ? "enabled"
            : "disabled"
    );

    return ESP_OK;
}
