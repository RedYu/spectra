#include "settings_service.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"

#include "system_model.h"
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

    esp_err_t result = ESP_OK;
    cJSON *root = cJSON_Parse(json_text);

    if (root == NULL) {
        const char *error_position = cJSON_GetErrorPtr();

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
        ESP_LOGE(TAG, "Configuration root must be a JSON object");
        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    /*
     * Validate schema version.
     */
    const cJSON *schema_version =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "schema_version"
        );

    if (!cJSON_IsNumber(schema_version)) {
        ESP_LOGE(TAG, "Missing or invalid schema_version");
        result = ESP_ERR_INVALID_VERSION;
        goto cleanup;
    }

    const uint32_t version =
        (uint32_t)schema_version->valuedouble;

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
     * Validate device identity before applying configuration.
     */
    const cJSON *device =
        cJSON_GetObjectItemCaseSensitive(root, "device");

    if (!cJSON_IsObject(device)) {
        ESP_LOGE(TAG, "Missing device configuration");
        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    const cJSON *device_target =
        cJSON_GetObjectItemCaseSensitive(device, "target");

    if (!cJSON_IsString(device_target) ||
        device_target->valuestring == NULL) {

        ESP_LOGE(TAG, "Missing or invalid device.target");
        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    system_model_t system_model;

    result = system_model_get_snapshot(&system_model);

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
        cJSON_GetObjectItemCaseSensitive(device, "name");

    if (!cJSON_IsString(device_name) ||
        device_name->valuestring == NULL) {

        ESP_LOGE(TAG, "Missing or invalid device.name");
        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    /*
     * The configuration belongs to this device.
     * It is now safe to apply its values.
     */
    settings->schema_version = version;

    strlcpy(
        settings->device.target,
        device_target->valuestring,
        sizeof(settings->device.target)
    );

    strlcpy(
        settings->device.name,
        device_name->valuestring,
        sizeof(settings->device.name)
    );

    /*
     * Parse the remaining configuration fields here.
     */

    ESP_LOGI(
        TAG,
        "Configuration validated for device: %s",
        system_model.device_id
    );

cleanup:
    cJSON_Delete(root);
    return result;
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
