#include "settings_service.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "app_config.h"
#include "gui_config.h"
#include "storage_sd_service.h"
#include "display_backlight.h"
#include "settings_model.h"
#include "storage_service.h"
#include "logging_service.h"
#include "wifi_service.h"
#include "usb_network_service.h"

static const char *TAG = "settings_service";

static const char *CONFIG_FILE_PATH =
    "/storage/device_config.json";

static const char *CONFIG_FILE_PATH_TMP =
    "/storage/device_config.json.tmp";

#define SETTINGS_SERVICE_LOCK_TIMEOUT_MS  (1000U)

static bool s_initialized = false;
static SemaphoreHandle_t s_mutex = NULL;

/*
 * TODO:
 * Move internal-storage write, remove and rename operations to
 * storage_service so settings saving is synchronized with filesystem
 * reads and storage deinitialization. Add a backup or recovery strategy
 * to prevent losing the previous configuration if file replacement
 * fails.
 */

static esp_err_t settings_service_lock(void);
static void settings_service_unlock(void);

static esp_err_t settings_service_reload_internal(void);
static esp_err_t settings_service_save_internal(void);

static esp_err_t settings_service_apply_wifi_enabled(
    bool enabled
);

static esp_err_t settings_service_set_brightness_internal(
    uint8_t brightness
);

static esp_err_t settings_service_set_sd_logging_enabled_internal(
    bool enabled
);

static esp_err_t settings_service_set_animations_enabled_internal(
    bool enabled
);

static esp_err_t settings_service_apply_internal(void);

static esp_err_t settings_service_lock(void)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_mutex,
            pdMS_TO_TICKS(
                SETTINGS_SERVICE_LOCK_TIMEOUT_MS
            )
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void settings_service_unlock(void)
{
    (void)xSemaphoreGive(
        s_mutex
    );
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

    result = settings_model_set_defaults(
        &parsed_settings
    );

    if (result != ESP_OK) {
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

    if (version != SETTINGS_SCHEMA_VERSION) {
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

    if (strcmp(
            device_target->valuestring,
            SPECTRA_APP_TARGET
        ) != 0) {

        ESP_LOGE(
            TAG,
            "Configuration target mismatch: "
            "expected='%s', received='%s'",
            SPECTRA_APP_TARGET,
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

    if (strlen(device_name->valuestring) >=
        SETTINGS_DEVICE_NAME_MAX_LENGTH) {

        ESP_LOGE(
            TAG,
            "device.name is too long"
        );

        result = ESP_ERR_INVALID_SIZE;
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
            "(allowed %u-%u)",
            brightness->valueint,
            (unsigned int)SETTINGS_DISPLAY_BRIGHTNESS_MIN,
            (unsigned int)SETTINGS_DISPLAY_BRIGHTNESS_MAX
        );

        result = ESP_ERR_INVALID_ARG;
        goto cleanup;
    }

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

    const cJSON *network =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "network"
        );

    if (!cJSON_IsObject(network)) {
        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    const cJSON *wifi_ap =
        cJSON_GetObjectItemCaseSensitive(
            network,
            "wifi_ap"
        );

    if (!cJSON_IsObject(wifi_ap)) {
        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    const cJSON *wifi_enabled =
        cJSON_GetObjectItemCaseSensitive(
            wifi_ap,
            "enabled"
        );

    const cJSON *wifi_ssid =
        cJSON_GetObjectItemCaseSensitive(
            wifi_ap,
            "ssid"
        );

    const cJSON *wifi_password =
        cJSON_GetObjectItemCaseSensitive(
            wifi_ap,
            "password"
        );

    if (!cJSON_IsBool(wifi_enabled) ||
        !cJSON_IsString(wifi_ssid) ||
        (wifi_ssid->valuestring == NULL) ||
        !cJSON_IsString(wifi_password) ||
        (wifi_password->valuestring == NULL)) {

        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    const size_t ssid_length =
        strlen(wifi_ssid->valuestring);

    const size_t password_length =
        strlen(wifi_password->valuestring);

    if ((ssid_length == 0U) ||
        (ssid_length >=
        SETTINGS_WIFI_SSID_MAX_LENGTH) ||
        (password_length >=
        SETTINGS_WIFI_PASSWORD_MAX_LENGTH)) {

        result = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    if ((password_length != 0U) &&
        (password_length <
        SETTINGS_WIFI_PASSWORD_MIN_LENGTH)) {

        result = ESP_ERR_INVALID_ARG;
        goto cleanup;
    }

    const cJSON *usb_rndis =
        cJSON_GetObjectItemCaseSensitive(
            network,
            "usb_rndis"
        );

    if (!cJSON_IsObject(usb_rndis)) {
        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    const cJSON *usb_enabled =
        cJSON_GetObjectItemCaseSensitive(
            usb_rndis,
            "enabled"
        );

    if (!cJSON_IsBool(usb_enabled)) {
        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    parsed_settings.wifi_ap.enabled =
        cJSON_IsTrue(wifi_enabled);

    (void)strlcpy(
        parsed_settings.wifi_ap.ssid,
        wifi_ssid->valuestring,
        sizeof(parsed_settings.wifi_ap.ssid)
    );

    (void)strlcpy(
        parsed_settings.wifi_ap.password,
        wifi_password->valuestring,
        sizeof(parsed_settings.wifi_ap.password)
    );

    parsed_settings.usb_rndis.enabled =
        cJSON_IsTrue(usb_enabled);

    /*
     * All configuration values are valid.
     *
     * Apply the fully validated configuration.
     */
    *settings =
        parsed_settings;

    ESP_LOGI(
        TAG,
        "Configuration validated for target: %s",
        parsed_settings.device.target
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

    cJSON *network =
        cJSON_AddObjectToObject(
            root,
            "network"
        );

    if (network == NULL) {
        goto error;
    }

    cJSON *wifi_ap =
        cJSON_AddObjectToObject(
            network,
            "wifi_ap"
        );

    if (wifi_ap == NULL) {
        goto error;
    }

    if ((cJSON_AddBoolToObject(
            wifi_ap,
            "enabled",
            settings->wifi_ap.enabled
        ) == NULL) ||
        (cJSON_AddStringToObject(
            wifi_ap,
            "ssid",
            settings->wifi_ap.ssid
        ) == NULL) ||
        (cJSON_AddStringToObject(
            wifi_ap,
            "password",
            settings->wifi_ap.password
        ) == NULL)) {

        goto error;
    }

    cJSON *usb_rndis =
        cJSON_AddObjectToObject(
            network,
            "usb_rndis"
        );

    if (usb_rndis == NULL) {
        goto error;
    }

    if (cJSON_AddBoolToObject(
            usb_rndis,
            "enabled",
            settings->usb_rndis.enabled
        ) == NULL) {

        goto error;
    }

    return root;

error:
    cJSON_Delete(root);
    return NULL;
}

static esp_err_t settings_service_save_internal(void)
{
    app_settings_t settings;

    esp_err_t result =
        settings_model_get(
            &settings
        );

    if (result != ESP_OK) {
        return result;
    }

    cJSON *root =
        settings_service_create_json(
            &settings
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

    FILE *file = fopen(
        CONFIG_FILE_PATH_TMP,
        "w"
    );

    if (file == NULL) {
        cJSON_free(json_text);
        return ESP_FAIL;
    }

    const size_t length =
        strlen(json_text);

    const size_t written = fwrite(
        json_text,
        1U,
        length,
        file
    );

    result = ESP_OK;

    if (written != length) {
        result = ESP_FAIL;
    }

    if (fflush(file) != 0) {
        result = ESP_FAIL;
    }

    if (fclose(file) != 0) {
        result = ESP_FAIL;
    }

    file = NULL;

    cJSON_free(json_text);
    json_text = NULL;

    if (result != ESP_OK) {
        (void)remove(
            CONFIG_FILE_PATH_TMP
        );

        ESP_LOGE(
            TAG,
            "Failed to write temporary settings file"
        );

        return result;
    }

    /*
     * SPIFFS may not replace an existing destination during rename.
     * A backup API should be added later for stronger crash safety.
     */
    (void)remove(
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

        (void)remove(
            CONFIG_FILE_PATH_TMP
        );

        return ESP_FAIL;
    }

    ESP_LOGD(
        TAG,
        "Settings saved successfully"
    );

    return ESP_OK;
}

static esp_err_t settings_service_reload_internal(void)
{
    app_settings_t settings;

    esp_err_t result =
        settings_model_set_defaults(
            &settings
        );

    if (result != ESP_OK) {
        return result;
    }

    char *file_data = NULL;
    size_t file_size = 0U;

    result = storage_service_read_file(
        CONFIG_FILE_PATH,
        &file_data,
        &file_size
    );

    if (result == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(
            TAG,
            "Configuration file not found, using defaults"
        );

        result = settings_model_set(
            &settings
        );

        if (result != ESP_OK) {
            return result;
        }

        return settings_service_apply_internal();
    }

    if (result != ESP_OK) {
        const esp_err_t read_result =
            result;

        ESP_LOGE(
            TAG,
            "Failed to read configuration: %s",
            esp_err_to_name(read_result)
        );

        result = settings_model_set(
            &settings
        );

        if (result != ESP_OK) {
            return result;
        }

        result =
            settings_service_apply_internal();

        if (result != ESP_OK) {
            return result;
        }

        ESP_LOGW(
            TAG,
            "Default settings applied after read failure"
        );

        return read_result;
    }

    ESP_LOGI(
        TAG,
        "Configuration file loaded: %u bytes",
        (unsigned int)file_size
    );

    result = parse_config(
        file_data,
        &settings
    );

    free(file_data);
    file_data = NULL;

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Invalid configuration, using defaults"
        );

        const esp_err_t defaults_result =
            settings_model_set_defaults(
                &settings
            );

        if (defaults_result != ESP_OK) {
            return defaults_result;
        }

        const esp_err_t model_result =
            settings_model_set(
                &settings
            );

        if (model_result != ESP_OK) {
            return model_result;
        }

        result = settings_service_apply_internal();

        if (result != ESP_OK) {
            return result;
        }

        ESP_LOGW(
            TAG,
            "Default settings applied after validation failure"
        );

        return ESP_OK;
    }

    result = settings_model_set(
        &settings
    );

    if (result != ESP_OK) {
        return result;
    }

    result = settings_service_apply_internal();

    if (result != ESP_OK) {
        return result;
    }

    ESP_LOGI(
        TAG,
        "Configuration applied successfully"
    );

    return ESP_OK;
}

esp_err_t settings_service_init(void)
{
    /*
     * Service initialization is expected to be called from one startup
     * task. Concurrent first-time initialization is not supported.
     */
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();

        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t result =
        settings_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (s_initialized) {
        settings_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    result = settings_model_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize settings model: %s",
            esp_err_to_name(result)
        );

        settings_service_unlock();
        return result;
    }

    /*
     * Internal reload requires the service to be marked initialized.
     */
    s_initialized = true;

    result =
        settings_service_reload_internal();

    if (result != ESP_OK) {
        s_initialized = false;

        ESP_LOGE(
            TAG,
            "Failed to initialize settings service: %s",
            esp_err_to_name(result)
        );
    }

    settings_service_unlock();

    return result;
}

static esp_err_t settings_service_set_brightness_internal(
    uint8_t brightness
)
{
    app_settings_t previous;

    esp_err_t result =
        settings_model_get(
            &previous
        );

    if (result != ESP_OK) {
        return result;
    }

    app_settings_t updated =
        previous;

    updated.display.brightness =
        brightness;

    result = settings_model_set(
        &updated
    );

    if (result != ESP_OK) {
        return result;
    }

    /*
     * Read the validated value back from the model.
     */
    result = settings_model_get(
        &updated
    );

    if (result != ESP_OK) {
        (void)settings_model_set(
            &previous
        );

        return result;
    }

    result = display_backlight_set_brightness(
        updated.display.brightness
    );

    if (result != ESP_OK) {
        (void)settings_model_set(
            &previous
        );

        return result;
    }

    return ESP_OK;
}

static esp_err_t settings_service_set_sd_logging_enabled_internal(
    bool enabled
)
{
    app_settings_t previous;

    esp_err_t result =
        settings_model_get(
            &previous
        );

    if (result != ESP_OK) {
        return result;
    }

    app_settings_t updated =
        previous;

    updated.logging.sd_enabled =
        enabled;

    result = settings_model_set(
        &updated
    );

    if (result != ESP_OK) {
        return result;
    }

    if (!enabled) {
        result = logging_service_disable_file();

        if (result != ESP_OK) {
            (void)settings_model_set(
                &previous
            );
        }

        return result;
    }

    storage_sd_state_t storage_state;

    result = storage_sd_service_get_state(
        &storage_state
    );

    if (result == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(
            TAG,
            "SD storage service is not started; "
            "SD logging will be applied later"
        );

        /*
         * Preserve the enabled preference. It will be applied when the
         * SD storage service becomes available.
         */
        return ESP_OK;
    }

    if (result != ESP_OK) {
        (void)settings_model_set(
            &previous
        );

        return result;
    }

    /*
     * Keep the preference enabled when no card is present. The SD
     * storage service can enable logging after the card is mounted.
     */
    if (storage_state != STORAGE_SD_STATE_MOUNTED) {
        return ESP_OK;
    }

    bool file_enabled = false;

    result = logging_service_get_file_enabled(
        &file_enabled
    );

    if (result != ESP_OK) {
        (void)settings_model_set(
            &previous
        );

        return result;
    }

    if (!file_enabled) {
        result = logging_service_enable_file();

        if (result != ESP_OK) {
            (void)settings_model_set(
                &previous
            );
        }
    }

    return result;
}

static esp_err_t settings_service_set_animations_enabled_internal(
    bool enabled
)
{
    app_settings_t settings;

    esp_err_t result =
        settings_model_get(
            &settings
        );

    if (result != ESP_OK) {
        return result;
    }

    settings.ui.animations_enabled =
        enabled;

    result = settings_model_set(
        &settings
    );

    if (result != ESP_OK) {
        return result;
    }

    gui_config_set_animations_enabled(
        enabled
    );

    return ESP_OK;
}

static esp_err_t settings_service_apply_wifi_enabled(
    bool enabled
)
{
    wifi_service_info_t info;

    esp_err_t result =
        wifi_service_get_info(
            &info
        );

    if (result != ESP_OK) {
        return result;
    }

    if (enabled) {
        if (!info.initialized) {
            result =
                wifi_service_init();

            if (result != ESP_OK) {
                return result;
            }

            info.started = false;
        }

        if (!info.started) {
            return wifi_service_start();
        }

        return ESP_OK;
    }

    if (info.initialized &&
        info.started) {

        return wifi_service_stop();
    }

    return ESP_OK;
}

static esp_err_t settings_service_apply_internal(void)
{
    app_settings_t settings;

    esp_err_t result =
        settings_model_get(
            &settings
        );

    if (result != ESP_OK) {
        return result;
    }

    result = wifi_service_set_ap_credentials(
        settings.wifi_ap.ssid,
        settings.wifi_ap.password
    );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to apply Wi-Fi credentials: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        settings_service_apply_wifi_enabled(
            settings.wifi_ap.enabled
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to apply Wi-Fi SoftAP state: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result = display_backlight_set_brightness(
        settings.display.brightness
    );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to apply display brightness: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    gui_config_set_animations_enabled(
        settings.ui.animations_enabled
    );

    bool file_enabled = false;

    result = logging_service_get_file_enabled(
        &file_enabled
    );

    if (result != ESP_OK) {
        return result;
    }

    if (!settings.logging.sd_enabled) {
        if (file_enabled) {
            result =
                logging_service_disable_file();

            if (result != ESP_OK) {
                return result;
            }
        }
    } else {
        storage_sd_state_t storage_state;

        result = storage_sd_service_get_state(
            &storage_state
        );

        if (result == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(
                TAG,
                "SD storage service is not started; "
                "SD logging will be applied later"
            );

            return ESP_OK;
        }

        if (result != ESP_OK) {
            return result;
        }

        if ((storage_state ==
             STORAGE_SD_STATE_MOUNTED) &&
            !file_enabled) {

            result =
                logging_service_enable_file();

            if (result != ESP_OK) {
                return result;
            }
        } else if (storage_state !=
                   STORAGE_SD_STATE_MOUNTED) {

            ESP_LOGW(
                TAG,
                "SD logging is enabled in settings, "
                "but the SD card is not mounted"
            );
        }
    }

    ESP_LOGI(
        TAG,
        "Settings applied: brightness=%u%%, "
        "SD logging=%s, animations=%s, "
        "Wi-Fi AP=%s, USB RNDIS=%s",
        (unsigned int)settings.display.brightness,
        settings.logging.sd_enabled
            ? "enabled"
            : "disabled",
        settings.ui.animations_enabled
            ? "enabled"
            : "disabled",
        settings.wifi_ap.enabled
            ? "enabled"
            : "disabled",
        settings.usb_rndis.enabled
            ? "enabled"
            : "disabled"
    );

    return ESP_OK;
}

esp_err_t settings_service_reload(void)
{
    const esp_err_t lock_result =
        settings_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    esp_err_t result = ESP_ERR_INVALID_STATE;

    if (s_initialized) {
        result =
            settings_service_reload_internal();
    }

    settings_service_unlock();

    return result;
}

esp_err_t settings_service_save(void)
{
    const esp_err_t lock_result =
        settings_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    esp_err_t result = ESP_ERR_INVALID_STATE;

    if (s_initialized) {
        result =
            settings_service_save_internal();
    }

    settings_service_unlock();

    return result;
}

esp_err_t settings_service_set_brightness(
    uint8_t brightness
)
{
    const esp_err_t lock_result =
        settings_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    esp_err_t result = ESP_ERR_INVALID_STATE;

    if (s_initialized) {
        result =
            settings_service_set_brightness_internal(
                brightness
            );
    }

    settings_service_unlock();

    return result;
}

esp_err_t settings_service_set_sd_logging_enabled(
    bool enabled
)
{
    const esp_err_t lock_result =
        settings_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    esp_err_t result = ESP_ERR_INVALID_STATE;

    if (s_initialized) {
        result =
            settings_service_set_sd_logging_enabled_internal(
                enabled
            );
    }

    settings_service_unlock();

    return result;
}

esp_err_t settings_service_set_animations_enabled(
    bool enabled
)
{
    const esp_err_t lock_result =
        settings_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    esp_err_t result = ESP_ERR_INVALID_STATE;

    if (s_initialized) {
        result =
            settings_service_set_animations_enabled_internal(
                enabled
            );
    }

    settings_service_unlock();

    return result;
}

esp_err_t settings_service_apply(void)
{
    const esp_err_t lock_result =
        settings_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    esp_err_t result = ESP_ERR_INVALID_STATE;

    if (s_initialized) {
        result =
            settings_service_apply_internal();
    }

    settings_service_unlock();

    return result;
}

esp_err_t settings_service_set_wifi_ap_enabled(
    bool enabled
)
{
    const esp_err_t lock_result =
        settings_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    if (!s_initialized) {
        settings_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    app_settings_t previous;

    esp_err_t result =
        settings_model_get(
            &previous
        );

    if (result != ESP_OK) {
        settings_service_unlock();

        return result;
    }

    if (previous.wifi_ap.enabled == enabled) {
        result =
            settings_service_apply_wifi_enabled(
                enabled
            );

        settings_service_unlock();

        return result;
    }

    app_settings_t updated =
        previous;

    updated.wifi_ap.enabled =
        enabled;

    result =
        settings_model_set(
            &updated
        );

    if (result == ESP_OK) {
        result =
            settings_service_apply_wifi_enabled(
                enabled
            );
    }

    if (result != ESP_OK) {
        (void)settings_model_set(
            &previous
        );

        /*
         * Attempt to restore the previous runtime state.
         */
        const esp_err_t restore_result =
            settings_service_apply_wifi_enabled(
                previous.wifi_ap.enabled
            );

        if (restore_result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Failed to restore previous Wi-Fi state: %s",
                esp_err_to_name(restore_result)
            );
        }
    }

    settings_service_unlock();

    return result;
}

esp_err_t settings_service_set_usb_rndis_enabled(
    bool enabled
)
{
    const esp_err_t lock_result =
        settings_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    if (!s_initialized) {
        settings_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    app_settings_t settings;

    esp_err_t result =
        settings_model_get(
            &settings
        );

    if (result == ESP_OK) {
        settings.usb_rndis.enabled =
            enabled;

        result =
            settings_model_set(
                &settings
            );
    }

    settings_service_unlock();

    return result;
}

esp_err_t settings_service_set_wifi_credentials(
    const char *ssid,
    const char *password
)
{
    if ((ssid == NULL) ||
        (password == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    const size_t ssid_length =
        strlen(ssid);

    const size_t password_length =
        strlen(password);

    if ((ssid_length == 0U) ||
        (ssid_length >=
         SETTINGS_WIFI_SSID_MAX_LENGTH) ||
        (password_length >=
         SETTINGS_WIFI_PASSWORD_MAX_LENGTH)) {

        return ESP_ERR_INVALID_SIZE;
    }

    if ((password_length != 0U) &&
        (password_length <
         SETTINGS_WIFI_PASSWORD_MIN_LENGTH)) {

        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t lock_result =
        settings_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    if (!s_initialized) {
        settings_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    app_settings_t previous;

    esp_err_t result =
        settings_model_get(
            &previous
        );

    if (result != ESP_OK) {
        settings_service_unlock();

        return result;
    }

    if ((strcmp(
            previous.wifi_ap.ssid,
            ssid
        ) == 0) &&
        (strcmp(
            previous.wifi_ap.password,
            password
        ) == 0)) {

        result =
            wifi_service_set_ap_credentials(
                ssid,
                password
            );

        settings_service_unlock();

        return result;
    }

    app_settings_t updated =
        previous;

    (void)strlcpy(
        updated.wifi_ap.ssid,
        ssid,
        sizeof(updated.wifi_ap.ssid)
    );

    (void)strlcpy(
        updated.wifi_ap.password,
        password,
        sizeof(updated.wifi_ap.password)
    );

    result =
        settings_model_set(
            &updated
        );

    if (result == ESP_OK) {
        result =
            wifi_service_set_ap_credentials(
                updated.wifi_ap.ssid,
                updated.wifi_ap.password
            );
    }

    if (result != ESP_OK) {
        (void)settings_model_set(
            &previous
        );

        const esp_err_t restore_result =
            wifi_service_set_ap_credentials(
                previous.wifi_ap.ssid,
                previous.wifi_ap.password
            );

        if (restore_result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Failed to restore previous Wi-Fi credentials: %s",
                esp_err_to_name(restore_result)
            );
        }
    }

    settings_service_unlock();

    return result;
}

esp_err_t settings_service_get_restart_required(
    bool *restart_required
)
{
    if (restart_required == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *restart_required = false;

    const esp_err_t lock_result =
        settings_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    if (!s_initialized) {
        settings_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    app_settings_t settings;

    esp_err_t result =
        settings_model_get(
            &settings
        );

    if (result != ESP_OK) {
        settings_service_unlock();

        return result;
    }

    usb_network_service_info_t usb_info;

    result =
        usb_network_service_get_info(
            &usb_info
        );

    if (result == ESP_OK) {
        const bool usb_active =
            usb_info.initialized &&
            usb_info.started;

        *restart_required =
            settings.usb_rndis.enabled !=
            usb_active;
    }

    settings_service_unlock();

    return result;
}
