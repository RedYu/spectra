#include "settings_service.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"

#include "settings_model.h"
#include "storage_service.h"

static const char *TAG = "settings_service";

static const char *CONFIG_FILE_PATH =
    "/storage/device_config.json";

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

    cJSON *root = cJSON_Parse(json_text);

    if (root == NULL) {
        const char *error_position = cJSON_GetErrorPtr();

        ESP_LOGE(
            TAG,
            "Invalid JSON near: %s",
            error_position != NULL ? error_position : "unknown position"
        );

        return ESP_ERR_INVALID_RESPONSE;
    }

    if (!cJSON_IsObject(root)) {
        ESP_LOGE(TAG, "Configuration root must be a JSON object");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *schema_version =
        cJSON_GetObjectItemCaseSensitive(root, "schema_version");

    if (!cJSON_IsNumber(schema_version)) {
        ESP_LOGE(TAG, "Missing schema_version");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_VERSION;
    }

    const uint32_t version =
        (uint32_t)schema_version->valuedouble;

    if (version != SUPPORTED_SCHEMA_VERSION) {
        ESP_LOGE(
            TAG,
            "Unsupported schema version: %u",
            (unsigned int)version
        );

        cJSON_Delete(root);
        return ESP_ERR_INVALID_VERSION;
    }

    settings->schema_version = version;

    const cJSON *device =
        cJSON_GetObjectItemCaseSensitive(root, "device");

    if (cJSON_IsObject(device)) {
        parse_string(
            device,
            "name",
            settings->device.name,
            sizeof(settings->device.name)
        );
    }

    cJSON_Delete(root);

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
        return ESP_OK;
    }

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to read configuration: %s",
            esp_err_to_name(err)
        );

        settings_model_set(&settings);
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

        return err;
    }

    settings_model_set(&settings);

    ESP_LOGI(TAG, "Configuration applied successfully");

    return ESP_OK;
}

esp_err_t settings_service_init(void)
{
    return settings_service_reload();
}
