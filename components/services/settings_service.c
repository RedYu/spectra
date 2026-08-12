#include "settings_service.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
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
#include "wifi_credentials_service.h"
#include "usb_network_service.h"

#define SETTINGS_JSON_INDENT_SPACES  (4U)

#define SETTINGS_SERVICE_LOCK_TIMEOUT_MS  (1000U)

static const char *TAG = "settings_service";

static const char *CONFIG_FILE_PATH =
    "/storage/device_config.json";

static const char *CONFIG_FILE_PATH_TMP =
    "/storage/device_config.json.tmp";

static bool s_initialized = false;
static SemaphoreHandle_t s_mutex = NULL;

static ui_theme_mode_t s_applied_theme_mode =
    SETTINGS_UI_THEME_MODE_DEFAULT;

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

static esp_err_t settings_service_set_log_tag_levels_internal(
    const char *warning_tags,
    const char *info_tags,
    const char *debug_tags,
    const char *disabled_tags
);

static esp_err_t settings_service_reload_internal(void);
static esp_err_t settings_service_save_internal(void);

static void settings_service_clear_sensitive_data(
    void *data,
    size_t size
);

static esp_err_t settings_service_load_sta_credentials(
    const app_settings_t *settings,
    wifi_sta_credentials_t *credentials,
    bool *matching
);

static esp_err_t settings_service_apply_wifi_states(
    bool ap_enabled,
    bool sta_enabled
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

static esp_err_t settings_service_set_theme_mode_internal(
    ui_theme_mode_t theme_mode
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

static app_settings_t *settings_service_allocate_settings(void)
{
    app_settings_t *settings =
        heap_caps_calloc(
            1U,
            sizeof(*settings),
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

    return settings;
}

static char *settings_service_format_json_spaces(
    const char *json
)
{
    if (json == NULL) {
        return NULL;
    }

    const size_t source_length =
        strlen(json);

    if (source_length >
        ((SIZE_MAX - 1U) /
         SETTINGS_JSON_INDENT_SPACES)) {

        return NULL;
    }

    const size_t output_capacity =
        (source_length *
         SETTINGS_JSON_INDENT_SPACES) + 1U;

    char *formatted =
        heap_caps_malloc(
            output_capacity,
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

    if (formatted == NULL) {
        formatted =
            malloc(output_capacity);
    }

    if (formatted == NULL) {
        return NULL;
    }

    const char *source = json;
    char *destination = formatted;

    bool line_start = true;

    while (*source != '\0') {
        if (*source == '\t') {
            const size_t space_count =
                line_start
                    ? SETTINGS_JSON_INDENT_SPACES
                    : 1U;

            for (size_t index = 0U;
                 index < space_count;
                 ++index) {

                *destination++ = ' ';
            }

            ++source;
            continue;
        }

        *destination++ = *source;

        if (*source == '\n') {
            line_start = true;
        } else if ((*source != ' ') &&
                   (*source != '\r')) {

            line_start = false;
        }

        ++source;
    }

    *destination = '\0';

    return formatted;
}

static void settings_service_clear_sensitive_data(
    void *data,
    size_t size
)
{
    if (data == NULL) {
        return;
    }

    volatile uint8_t *bytes =
        (volatile uint8_t *)data;

    while (size > 0U) {
        *bytes = 0U;

        ++bytes;
        --size;
    }
}

static esp_err_t settings_service_load_sta_credentials(
    const app_settings_t *settings,
    wifi_sta_credentials_t *credentials,
    bool *matching
)
{
    if ((settings == NULL) ||
        (credentials == NULL) ||
        (matching == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    memset(
        credentials,
        0,
        sizeof(*credentials)
    );

    *matching = false;

    if ((settings->wifi_sta.ssid[0] == '\0') ||
        (settings->wifi_sta.credential_id[0] == '\0')) {

        return ESP_OK;
    }

    const esp_err_t result =
        wifi_credentials_service_get(
            credentials
        );

    if (result == ESP_ERR_NOT_FOUND) {
        return ESP_OK;
    }

    if (result != ESP_OK) {
        return result;
    }

    *matching =
        (strcmp(
            settings->wifi_sta.ssid,
            credentials->ssid
        ) == 0) &&
        (strcmp(
            settings->wifi_sta.credential_id,
            credentials->credential_id
        ) == 0);

    return ESP_OK;
}

static esp_err_t settings_service_parse_log_tag_list(
    const cJSON *object,
    const char *name,
    char *destination,
    size_t destination_size
)
{
    if ((object == NULL) ||
        (name == NULL) ||
        (destination == NULL) ||
        (destination_size == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    const cJSON *value =
        cJSON_GetObjectItemCaseSensitive(
            object,
            name
        );

    if (!cJSON_IsString(value) ||
        (value->valuestring == NULL)) {

        ESP_LOGE(
            TAG,
            "Missing or invalid logging.tag_levels.%s",
            name
        );

        return ESP_ERR_INVALID_RESPONSE;
    }

    if (strnlen(
            value->valuestring,
            destination_size
        ) >= destination_size) {

        ESP_LOGE(
            TAG,
            "logging.tag_levels.%s is too long",
            name
        );

        return ESP_ERR_INVALID_SIZE;
    }

    (void)strlcpy(
        destination,
        value->valuestring,
        destination_size
    );

    return ESP_OK;
}

static const char *settings_service_theme_mode_to_string(
    ui_theme_mode_t theme_mode
)
{
    switch (theme_mode) {
        case UI_THEME_MODE_DARK:
            return "dark";

        case UI_THEME_MODE_LIGHT:
        default:
            return "light";
    }
}

static esp_err_t settings_service_parse_theme_mode(
    const cJSON *ui,
    ui_theme_mode_t *theme_mode
)
{
    if ((ui == NULL) ||
        (theme_mode == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    const cJSON *theme =
        cJSON_GetObjectItemCaseSensitive(
            ui,
            "theme"
        );

    /*
     * Keep compatibility with configuration files created before
     * theme selection was introduced.
     */
    if (theme == NULL) {
        *theme_mode =
            SETTINGS_UI_THEME_MODE_DEFAULT;

        return ESP_OK;
    }

    if (!cJSON_IsString(theme) ||
        (theme->valuestring == NULL)) {

        ESP_LOGE(
            TAG,
            "Invalid ui.theme"
        );

        return ESP_ERR_INVALID_RESPONSE;
    }

    if (strcmp(
            theme->valuestring,
            "light"
        ) == 0) {

        *theme_mode =
            UI_THEME_MODE_LIGHT;

        return ESP_OK;
    }

    if (strcmp(
            theme->valuestring,
            "dark"
        ) == 0) {

        *theme_mode =
            UI_THEME_MODE_DARK;

        return ESP_OK;
    }

    ESP_LOGE(
        TAG,
        "Unsupported ui.theme: %s",
        theme->valuestring
    );

    return ESP_ERR_INVALID_RESPONSE;
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

    app_settings_t *parsed_settings = NULL;

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
    parsed_settings =
        settings_service_allocate_settings();

    if (parsed_settings == NULL) {
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    result =
        settings_model_set_defaults(
            parsed_settings
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

    parsed_settings->schema_version =
        version;

    strlcpy(
        parsed_settings->device.target,
        device_target->valuestring,
        sizeof(parsed_settings->device.target)
    );

    strlcpy(
        parsed_settings->device.name,
        device_name->valuestring,
        sizeof(parsed_settings->device.name)
    );

    parsed_settings->display.brightness =
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

    parsed_settings->logging.sd_enabled =
        cJSON_IsTrue(sd_enabled);

    /*
     * Log-level configuration is optional for compatibility with
     * configuration files created before this feature was added.
     * Default values remain active when the object is missing.
     */
    const cJSON *tag_levels =
        cJSON_GetObjectItemCaseSensitive(
            logging,
            "tag_levels"
        );

    if (tag_levels != NULL) {
        if (!cJSON_IsObject(tag_levels)) {
            ESP_LOGE(
                TAG,
                "Invalid logging.tag_levels configuration"
            );

            result = ESP_ERR_INVALID_RESPONSE;
            goto cleanup;
        }

        result =
            settings_service_parse_log_tag_list(
                tag_levels,
                "warning",
                parsed_settings->logging.warning_tags,
                sizeof(
                    parsed_settings->logging.warning_tags
                )
            );

        if (result != ESP_OK) {
            goto cleanup;
        }

        result =
            settings_service_parse_log_tag_list(
                tag_levels,
                "info",
                parsed_settings->logging.info_tags,
                sizeof(
                    parsed_settings->logging.info_tags
                )
            );

        if (result != ESP_OK) {
            goto cleanup;
        }

        result =
            settings_service_parse_log_tag_list(
                tag_levels,
                "debug",
                parsed_settings->logging.debug_tags,
                sizeof(
                    parsed_settings->logging.debug_tags
                )
            );

        if (result != ESP_OK) {
            goto cleanup;
        }

        result =
            settings_service_parse_log_tag_list(
                tag_levels,
                "disabled",
                parsed_settings->logging.disabled_tags,
                sizeof(
                    parsed_settings->logging.disabled_tags
                )
            );

        if (result != ESP_OK) {
            goto cleanup;
        }
    }

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

    parsed_settings->ui.animations_enabled =
        cJSON_IsTrue(animations_enabled);

    result =
        settings_service_parse_theme_mode(
            ui,
            &parsed_settings->ui.theme_mode
        );

    if (result != ESP_OK) {
        goto cleanup;
    }

    ESP_LOGD(
        TAG,
        "UI theme: %s",
        settings_service_theme_mode_to_string(
            parsed_settings->ui.theme_mode
        )
    );

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

    const size_t ap_ssid_length =
        strlen(
            wifi_ssid->valuestring
        );

    const size_t ap_password_length =
        strlen(
            wifi_password->valuestring
        );

    if ((ap_ssid_length == 0U) ||
        (ap_ssid_length >=
        SETTINGS_WIFI_AP_SSID_MAX_LENGTH) ||
        (ap_password_length >=
        SETTINGS_WIFI_PASSWORD_MAX_LENGTH)) {

        result = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    if ((ap_password_length != 0U) &&
        (ap_password_length <
        SETTINGS_WIFI_PASSWORD_MIN_LENGTH)) {

        result = ESP_ERR_INVALID_ARG;
        goto cleanup;
    }

    const cJSON *wifi_sta =
        cJSON_GetObjectItemCaseSensitive(
            network,
            "wifi_sta"
        );

    /*
    * wifi_sta is optional while schema version 1 is still under
    * development. Missing values retain their defaults.
    */
    if (wifi_sta != NULL) {
        if (!cJSON_IsObject(wifi_sta)) {
            result = ESP_ERR_INVALID_RESPONSE;
            goto cleanup;
        }

        const cJSON *sta_enabled =
            cJSON_GetObjectItemCaseSensitive(
                wifi_sta,
                "enabled"
            );

        const cJSON *sta_ssid =
            cJSON_GetObjectItemCaseSensitive(
                wifi_sta,
                "ssid"
            );

        const cJSON *credential_id =
            cJSON_GetObjectItemCaseSensitive(
                wifi_sta,
                "credential_id"
            );

        if (!cJSON_IsBool(sta_enabled) ||
            !cJSON_IsString(sta_ssid) ||
            (sta_ssid->valuestring == NULL) ||
            !cJSON_IsString(credential_id) ||
            (credential_id->valuestring == NULL)) {

            result = ESP_ERR_INVALID_RESPONSE;
            goto cleanup;
        }

        const bool sta_is_enabled =
            cJSON_IsTrue(sta_enabled);

        const size_t sta_ssid_length =
            strlen(
                sta_ssid->valuestring
            );

        const size_t credential_id_length =
            strlen(
                credential_id->valuestring
            );

        if ((sta_ssid_length >=
            SETTINGS_WIFI_STA_SSID_MAX_LENGTH) ||
            (credential_id_length >=
            SETTINGS_WIFI_CREDENTIAL_ID_LENGTH)) {

            result = ESP_ERR_INVALID_SIZE;
            goto cleanup;
        }

        if (sta_is_enabled &&
            ((sta_ssid_length == 0U) ||
            (credential_id_length == 0U))) {

            result = ESP_ERR_INVALID_ARG;
            goto cleanup;
        }

        if ((credential_id_length != 0U) &&
            (credential_id_length !=
            (SETTINGS_WIFI_CREDENTIAL_ID_LENGTH - 1U))) {

            result = ESP_ERR_INVALID_SIZE;
            goto cleanup;
        }

        parsed_settings->wifi_sta.enabled =
            sta_is_enabled;

        (void)strlcpy(
            parsed_settings->wifi_sta.ssid,
            sta_ssid->valuestring,
            sizeof(parsed_settings->wifi_sta.ssid)
        );

        (void)strlcpy(
            parsed_settings->wifi_sta.credential_id,
            credential_id->valuestring,
            sizeof(parsed_settings->wifi_sta.credential_id)
        );
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

    parsed_settings->wifi_ap.enabled =
        cJSON_IsTrue(wifi_enabled);

    (void)strlcpy(
        parsed_settings->wifi_ap.ssid,
        wifi_ssid->valuestring,
        sizeof(parsed_settings->wifi_ap.ssid)
    );

    (void)strlcpy(
        parsed_settings->wifi_ap.password,
        wifi_password->valuestring,
        sizeof(parsed_settings->wifi_ap.password)
    );

    parsed_settings->usb_rndis.enabled =
        cJSON_IsTrue(usb_enabled);

    /*
     * All configuration values are valid.
     *
     * Apply the fully validated configuration.
     */
    *settings =
        *parsed_settings;

    ESP_LOGI(
        TAG,
        "Configuration validated for target: %s",
        parsed_settings->device.target
    );

    ESP_LOGD(
        TAG,
        "Display brightness: %u%%",
        parsed_settings->display.brightness
    );

    ESP_LOGD(
        TAG,
        "SD card logging: %s",
        parsed_settings->logging.sd_enabled
            ? "enabled"
            : "disabled"
    );

    ESP_LOGD(
        TAG,
        "UI animations: %s",
        parsed_settings->ui.animations_enabled
            ? "enabled"
            : "disabled"
    );

cleanup:
    heap_caps_free(
        parsed_settings
    );

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

    cJSON *tag_levels =
        cJSON_AddObjectToObject(
            logging,
            "tag_levels"
        );

    if (tag_levels == NULL) {
        goto error;
    }

    if (cJSON_AddStringToObject(
            tag_levels,
            "warning",
            settings->logging.warning_tags
        ) == NULL) {

        goto error;
    }

    if (cJSON_AddStringToObject(
            tag_levels,
            "info",
            settings->logging.info_tags
        ) == NULL) {

        goto error;
    }

    if (cJSON_AddStringToObject(
            tag_levels,
            "debug",
            settings->logging.debug_tags
        ) == NULL) {

        goto error;
    }

    if (cJSON_AddStringToObject(
            tag_levels,
            "disabled",
            settings->logging.disabled_tags
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

    if (cJSON_AddStringToObject(
            ui,
            "theme",
            settings_service_theme_mode_to_string(
                settings->ui.theme_mode
            )
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

    cJSON *wifi_sta =
        cJSON_AddObjectToObject(
            network,
            "wifi_sta"
        );

    if (wifi_sta == NULL) {
        goto error;
    }

    if ((cJSON_AddBoolToObject(
            wifi_sta,
            "enabled",
            settings->wifi_sta.enabled
        ) == NULL) ||
        (cJSON_AddStringToObject(
            wifi_sta,
            "ssid",
            settings->wifi_sta.ssid
        ) == NULL) ||
        (cJSON_AddStringToObject(
            wifi_sta,
            "credential_id",
            settings->wifi_sta.credential_id
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
    app_settings_t *settings =
        settings_service_allocate_settings();

    if (settings == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result =
        settings_model_get(
            settings
        );

    if (result != ESP_OK) {
        heap_caps_free(settings);
        return result;
    }

    cJSON *root =
        settings_service_create_json(
            settings
        );

    heap_caps_free(settings);
    settings = NULL;

    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    char *json_with_tabs =
        cJSON_Print(root);

    cJSON_Delete(root);

    if (json_with_tabs == NULL) {
        return ESP_ERR_NO_MEM;
    }

    char *json_text =
        settings_service_format_json_spaces(
            json_with_tabs
        );

    cJSON_free(
        json_with_tabs
    );

    if (json_text == NULL) {
        return ESP_ERR_NO_MEM;
    }

    FILE *file = fopen(
        CONFIG_FILE_PATH_TMP,
        "w"
    );

    if (file == NULL) {
        free(json_text);
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

    free(json_text);
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
    app_settings_t *settings =
        settings_service_allocate_settings();

    if (settings == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result =
        settings_model_set_defaults(
            settings
        );

    if (result != ESP_OK) {
        goto cleanup;
    }

    char *file_data = NULL;
    size_t file_size = 0U;

    result =
        storage_service_read_file(
            CONFIG_FILE_PATH,
            &file_data,
            &file_size
        );

    if (result == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(
            TAG,
            "Configuration file not found, using defaults"
        );

        result =
            settings_model_set(
                settings
            );

        if (result == ESP_OK) {
            result =
                settings_service_apply_internal();
        }

        goto cleanup;
    }

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to read configuration: %s",
            esp_err_to_name(result)
        );

        result =
            settings_model_set(
                settings
            );

        if (result == ESP_OK) {
            result =
                settings_service_apply_internal();
        }

        if (result == ESP_OK) {
            ESP_LOGW(
                TAG,
                "Default settings applied after read failure"
            );
        }

        goto cleanup;
    }

    ESP_LOGI(
        TAG,
        "Configuration file loaded: %u bytes",
        (unsigned int)file_size
    );

    result =
        parse_config(
            file_data,
            settings
        );

    free(file_data);
    file_data = NULL;

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Invalid configuration, using defaults"
        );

        result =
            settings_model_set_defaults(
                settings
            );

        if (result == ESP_OK) {
            result =
                settings_model_set(
                    settings
                );
        }

        if (result == ESP_OK) {
            result =
                settings_service_apply_internal();
        }

        if (result == ESP_OK) {
            ESP_LOGW(
                TAG,
                "Default settings applied after validation failure"
            );
        }

        goto cleanup;
    }

    result =
        settings_model_set(
            settings
        );

    if (result == ESP_OK) {
        result =
            settings_service_apply_internal();
    }

    if (result == ESP_OK) {
        ESP_LOGI(
            TAG,
            "Configuration applied successfully"
        );
    }

cleanup:
    free(file_data);

    heap_caps_free(
        settings
    );

    return result;
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

static esp_err_t settings_service_set_theme_mode_internal(
    ui_theme_mode_t theme_mode
)
{
    if ((theme_mode != UI_THEME_MODE_LIGHT) &&
        (theme_mode != UI_THEME_MODE_DARK)) {

        return ESP_ERR_INVALID_ARG;
    }

    app_settings_t settings;

    esp_err_t result =
        settings_model_get(
            &settings
        );

    if (result != ESP_OK) {
        return result;
    }

    settings.ui.theme_mode =
        theme_mode;

    return settings_model_set(
        &settings
    );
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

static esp_err_t settings_service_apply_wifi_states(
    bool ap_enabled,
    bool sta_enabled
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

    if (!info.initialized) {
        result =
            wifi_service_init();

        if (result != ESP_OK) {
            return result;
        }
    }

    /*
     * Enable requested interfaces before disabling the others.
     * This avoids stopping the Wi-Fi driver when switching between
     * AP, STA and APSTA modes.
     */
    if (ap_enabled) {
        result =
            wifi_service_set_ap_enabled(
                true
            );

        if (result != ESP_OK) {
            return result;
        }
    }

    if (sta_enabled) {
        result =
            wifi_service_set_sta_enabled(
                true
            );

        if (result != ESP_OK) {
            return result;
        }
    }

    if (!ap_enabled) {
        result =
            wifi_service_set_ap_enabled(
                false
            );

        if (result != ESP_OK) {
            return result;
        }
    }

    if (!sta_enabled) {
        result =
            wifi_service_set_sta_enabled(
                false
            );

        if (result != ESP_OK) {
            return result;
        }
    }

    result =
        wifi_service_get_info(
            &info
        );

    if (result != ESP_OK) {
        return result;
    }

    if (ap_enabled || sta_enabled) {
        if (!info.started) {
            return wifi_service_start();
        }
    } else if (info.started) {
        return wifi_service_stop();
    }

    return ESP_OK;
}

static esp_err_t settings_service_apply_internal(void)
{
    app_settings_t *settings =
        settings_service_allocate_settings();

    if (settings == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result =
        settings_model_get(
            settings
        );

    if (result != ESP_OK) {
        goto cleanup;
    }

    result =
        logging_service_set_tag_levels(
            settings->logging.warning_tags,
            settings->logging.info_tags,
            settings->logging.debug_tags,
            settings->logging.disabled_tags
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to apply log tag levels: %s",
            esp_err_to_name(result)
        );

        goto cleanup;
    }

    result =
        wifi_service_set_ap_credentials(
            settings->wifi_ap.ssid,
            settings->wifi_ap.password
        );

    if (result != ESP_OK) {
        goto cleanup;
    }

    wifi_sta_credentials_t sta_credentials;
    bool sta_credentials_match = false;

    result =
        settings_service_load_sta_credentials(
            settings,
            &sta_credentials,
            &sta_credentials_match
        );

    if (result != ESP_OK) {
        settings_service_clear_sensitive_data(
            &sta_credentials,
            sizeof(sta_credentials)
        );

        goto cleanup;
    }

    /*
     * Do not copy the password into the Wi-Fi service until STA is
     * actually requested.
     */
    if (settings->wifi_sta.enabled &&
        sta_credentials_match) {

        result =
            wifi_service_set_sta_credentials(
                sta_credentials.ssid,
                sta_credentials.password
            );

        if (result != ESP_OK) {
            settings_service_clear_sensitive_data(
                &sta_credentials,
                sizeof(sta_credentials)
            );

            goto cleanup;
        }
    }

    const bool sta_runtime_enabled =
        settings->wifi_sta.enabled &&
        sta_credentials_match;

    if (settings->wifi_sta.enabled &&
        !sta_credentials_match) {

        ESP_LOGW(
            TAG,
            "Wi-Fi STA credentials are missing or do not match "
            "the current settings"
        );
    }

    settings_service_clear_sensitive_data(
        &sta_credentials,
        sizeof(sta_credentials)
    );

    result =
        settings_service_apply_wifi_states(
            settings->wifi_ap.enabled,
            sta_runtime_enabled
        );

    if (result != ESP_OK) {
        goto cleanup;
    }

    result = display_backlight_set_brightness(
        settings->display.brightness
    );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to apply display brightness: %s",
            esp_err_to_name(result)
        );

        goto cleanup;
    }

    gui_config_set_animations_enabled(
        settings->ui.animations_enabled
    );

    bool file_enabled = false;

    result = logging_service_get_file_enabled(
        &file_enabled
    );

    if (result != ESP_OK) {
        goto cleanup;
    }

    if (!settings->logging.sd_enabled) {
        if (file_enabled) {
            result =
                logging_service_disable_file();

            if (result != ESP_OK) {
                goto cleanup;
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

            result = ESP_OK;

        } else if (result != ESP_OK) {
            goto cleanup;

        } else if ((storage_state ==
                    STORAGE_SD_STATE_MOUNTED) &&
                   !file_enabled) {

            result =
                logging_service_enable_file();

            if (result != ESP_OK) {
                goto cleanup;
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
        "Settings applied"
    );

    ESP_LOGI(
        TAG,
        "Display: brightness=%u%%, animations=%s, theme=%s",
        (unsigned int)settings->display.brightness,
        settings->ui.animations_enabled
            ? "enabled"
            : "disabled",
        settings_service_theme_mode_to_string(
            settings->ui.theme_mode
        )
    );

    ESP_LOGI(
        TAG,
        "Logging: SD=%s",
        settings->logging.sd_enabled
            ? "enabled"
            : "disabled"
    );

    ESP_LOGI(
        TAG,
        "Network: Wi-Fi AP=%s, Wi-Fi STA=%s",
        settings->wifi_ap.enabled
            ? "enabled"
            : "disabled",
        settings->wifi_sta.enabled
            ? "enabled"
            : "disabled"
    );

    ESP_LOGI(
        TAG,
        "Network: USB RNDIS=%s",
        settings->usb_rndis.enabled
            ? "enabled"
            : "disabled"
    );

    result = ESP_OK;

cleanup:
    heap_caps_free(
        settings
    );

    return result;
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

esp_err_t settings_service_set_theme_mode(
    ui_theme_mode_t theme_mode
)
{
    const esp_err_t lock_result =
        settings_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    esp_err_t result =
        ESP_ERR_INVALID_STATE;

    if (s_initialized) {
        result =
            settings_service_set_theme_mode_internal(
                theme_mode
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

esp_err_t settings_service_set_wifi_sta_enabled(
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

    wifi_sta_credentials_t credentials;
    bool credentials_match = false;

    result =
        settings_service_load_sta_credentials(
            &previous,
            &credentials,
            &credentials_match
        );

    if (result != ESP_OK) {
        settings_service_clear_sensitive_data(
            &credentials,
            sizeof(credentials)
        );

        settings_service_unlock();

        return result;
    }

    if (enabled &&
        !credentials_match) {

        settings_service_clear_sensitive_data(
            &credentials,
            sizeof(credentials)
        );

        settings_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    if (enabled) {
        result =
            wifi_service_set_sta_credentials(
                credentials.ssid,
                credentials.password
            );

        if (result != ESP_OK) {
            settings_service_clear_sensitive_data(
                &credentials,
                sizeof(credentials)
            );

            settings_service_unlock();

            return result;
        }
    }

    settings_service_clear_sensitive_data(
        &credentials,
        sizeof(credentials)
    );

    app_settings_t updated =
        previous;

    updated.wifi_sta.enabled =
        enabled;

    result =
        settings_model_set(
            &updated
        );

    if (result == ESP_OK) {
        result =
            settings_service_apply_wifi_states(
                previous.wifi_ap.enabled,
                enabled
            );
    }

    if (result != ESP_OK) {
        (void)settings_model_set(
            &previous
        );

        const esp_err_t restore_result =
            settings_service_apply_wifi_states(
                previous.wifi_ap.enabled,
                previous.wifi_sta.enabled &&
                credentials_match
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

esp_err_t settings_service_set_wifi_sta_credentials(
    const char *ssid,
    const char *password
)
{
    if ((ssid == NULL) ||
        (password == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    const size_t ssid_length =
        strlen(
            ssid
        );

    const size_t password_length =
        strlen(
            password
        );

    if (ssid_length == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((ssid_length >=
         SETTINGS_WIFI_STA_SSID_MAX_LENGTH) ||
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

    char credential_id[
        SETTINGS_WIFI_CREDENTIAL_ID_LENGTH
    ];

    memset(
        credential_id,
        0,
        sizeof(credential_id)
    );

    result =
        wifi_credentials_service_set(
            ssid,
            password,
            credential_id,
            sizeof(credential_id)
        );

    if (result != ESP_OK) {
        settings_service_clear_sensitive_data(
            credential_id,
            sizeof(credential_id)
        );

        settings_service_unlock();

        return result;
    }

    app_settings_t updated =
        previous;

    (void)strlcpy(
        updated.wifi_sta.ssid,
        ssid,
        sizeof(updated.wifi_sta.ssid)
    );

    (void)strlcpy(
        updated.wifi_sta.credential_id,
        credential_id,
        sizeof(updated.wifi_sta.credential_id)
    );

    settings_service_clear_sensitive_data(
        credential_id,
        sizeof(credential_id)
    );

    result =
        settings_model_set(
            &updated
        );

    /*
     * Keep credentials only in NVS while STA is disabled. They will
     * be loaded into the Wi-Fi service when STA is enabled.
     */
    if ((result == ESP_OK) &&
        updated.wifi_sta.enabled) {

        result =
            wifi_service_set_sta_credentials(
                ssid,
                password
            );
    }

    settings_service_unlock();

    return result;
}

/*
 * TODO:
 * Add wifi_service_clear_sta_credentials() and call it after disabling
 * STA and removing its credentials from NVS. Currently, the credentials
 * are removed from persistent storage, but the Wi-Fi service may retain
 * a copy of the previous SSID and password in RAM until it is
 * reconfigured, deinitialized or the device is restarted.
 */
esp_err_t settings_service_clear_wifi_sta_credentials(void)
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

    if (result != ESP_OK) {
        settings_service_unlock();

        return result;
    }

    result =
        settings_service_apply_wifi_states(
            settings.wifi_ap.enabled,
            false
        );

    if (result != ESP_OK) {
        settings_service_unlock();

        return result;
    }

    result =
        wifi_credentials_service_clear();

    if (result != ESP_OK) {
        settings_service_unlock();

        return result;
    }

    settings.wifi_sta.enabled =
        false;

    settings.wifi_sta.ssid[0] =
        '\0';

    settings.wifi_sta.credential_id[0] =
        '\0';

    result =
        settings_model_set(
            &settings
        );

    settings_service_unlock();

    return result;
}

esp_err_t settings_service_get_wifi_sta_credentials_configured(
    bool *configured
)
{
    if (configured == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *configured = false;

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

    wifi_sta_credentials_t credentials;
    bool matching = false;

    result =
        settings_service_load_sta_credentials(
            &settings,
            &credentials,
            &matching
        );

    settings_service_clear_sensitive_data(
        &credentials,
        sizeof(credentials)
    );

    if (result == ESP_OK) {
        *configured =
            matching;
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
            settings_service_apply_wifi_states(
                enabled,
                previous.wifi_sta.enabled
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
            settings_service_apply_wifi_states(
                enabled,
                previous.wifi_sta.enabled
            );
    }

    if (result != ESP_OK) {
        (void)settings_model_set(
            &previous
        );

        const esp_err_t restore_result =
            settings_service_apply_wifi_states(
                previous.wifi_ap.enabled,
                previous.wifi_sta.enabled
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

esp_err_t settings_service_set_wifi_ap_credentials(
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
         SETTINGS_WIFI_AP_SSID_MAX_LENGTH) ||
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

    const bool theme_restart_required =
        settings.ui.theme_mode !=
        s_applied_theme_mode;

    usb_network_service_info_t usb_info;

    result =
        usb_network_service_get_info(
            &usb_info
        );

    bool usb_restart_required = false;

    if (result == ESP_OK) {
        const bool usb_active =
            usb_info.initialized &&
            usb_info.started;

        usb_restart_required =
            settings.usb_rndis.enabled !=
            usb_active;
    }

    *restart_required =
        usb_restart_required ||
        theme_restart_required;

    settings_service_unlock();

    return result;
}

static esp_err_t settings_service_set_log_tag_levels_internal(
    const char *warning_tags,
    const char *info_tags,
    const char *debug_tags,
    const char *disabled_tags
)
{
    if ((warning_tags == NULL) ||
        (info_tags == NULL) ||
        (debug_tags == NULL) ||
        (disabled_tags == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    if ((strnlen(
            warning_tags,
            SETTINGS_LOG_TAG_LIST_MAX_LENGTH
        ) >= SETTINGS_LOG_TAG_LIST_MAX_LENGTH) ||
        (strnlen(
            info_tags,
            SETTINGS_LOG_TAG_LIST_MAX_LENGTH
        ) >= SETTINGS_LOG_TAG_LIST_MAX_LENGTH) ||
        (strnlen(
            debug_tags,
            SETTINGS_LOG_TAG_LIST_MAX_LENGTH
        ) >= SETTINGS_LOG_TAG_LIST_MAX_LENGTH) ||
        (strnlen(
            disabled_tags,
            SETTINGS_LOG_TAG_LIST_MAX_LENGTH
        ) >= SETTINGS_LOG_TAG_LIST_MAX_LENGTH)) {

        return ESP_ERR_INVALID_SIZE;
    }

    typedef struct
    {
        app_settings_t previous;
        app_settings_t updated;

    } settings_update_context_t;

    settings_update_context_t *context =
        heap_caps_calloc(
            1U,
            sizeof(*context),
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

    if (context == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result =
        settings_model_get(
            &context->previous
        );

    if (result != ESP_OK) {
        heap_caps_free(context);
        return result;
    }

    context->updated =
        context->previous;

    (void)strlcpy(
        context->updated.logging.warning_tags,
        warning_tags,
        sizeof(
            context->updated.logging.warning_tags
        )
    );

    (void)strlcpy(
        context->updated.logging.info_tags,
        info_tags,
        sizeof(
            context->updated.logging.info_tags
        )
    );

    (void)strlcpy(
        context->updated.logging.debug_tags,
        debug_tags,
        sizeof(
            context->updated.logging.debug_tags
        )
    );

    (void)strlcpy(
        context->updated.logging.disabled_tags,
        disabled_tags,
        sizeof(
            context->updated.logging.disabled_tags
        )
    );

    result =
        settings_model_set(
            &context->updated
        );

    if (result == ESP_OK) {
        result =
            logging_service_set_tag_levels(
                context->updated.logging.warning_tags,
                context->updated.logging.info_tags,
                context->updated.logging.debug_tags,
                context->updated.logging.disabled_tags
            );
    }

    if (result != ESP_OK) {
        (void)settings_model_set(
            &context->previous
        );

        (void)logging_service_set_tag_levels(
            context->previous.logging.warning_tags,
            context->previous.logging.info_tags,
            context->previous.logging.debug_tags,
            context->previous.logging.disabled_tags
        );
    }

    heap_caps_free(context);

    return result;
}

esp_err_t settings_service_set_log_tag_levels(
    const char *warning_tags,
    const char *info_tags,
    const char *debug_tags,
    const char *disabled_tags
)
{
    const esp_err_t lock_result =
        settings_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    esp_err_t result =
        ESP_ERR_INVALID_STATE;

    if (s_initialized) {
        result =
            settings_service_set_log_tag_levels_internal(
                warning_tags,
                info_tags,
                debug_tags,
                disabled_tags
            );
    }

    settings_service_unlock();

    return result;
}

esp_err_t settings_service_mark_theme_applied(
    ui_theme_mode_t theme_mode
)
{
    if ((theme_mode != UI_THEME_MODE_LIGHT) &&
        (theme_mode != UI_THEME_MODE_DARK)) {

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

    s_applied_theme_mode =
        theme_mode;

    settings_service_unlock();

    return ESP_OK;
}
