#include "web_settings_api.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "settings_model.h"
#include "settings_service.h"
#include "web_api_common.h"

#define WEB_SETTINGS_API_BODY_MAX_SIZE  (2048U)

static const char *TAG =
    "web_settings_api";

static app_settings_t *web_settings_api_allocate_settings(void)
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

static esp_err_t web_settings_api_receive_body(
    httpd_req_t *request,
    char **out_body
)
{
    if ((request == NULL) ||
        (out_body == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *out_body = NULL;

    if ((request->content_len == 0U) ||
        (request->content_len >
         WEB_SETTINGS_API_BODY_MAX_SIZE)) {

        return ESP_ERR_INVALID_SIZE;
    }

    char *body =
        heap_caps_calloc(
            request->content_len + 1U,
            sizeof(char),
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

    if (body == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t received = 0U;

    while (received < request->content_len) {
        const int result =
            httpd_req_recv(
                request,
                body + received,
                request->content_len - received
            );

        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }

        if (result <= 0) {
            heap_caps_free(body);
            return ESP_FAIL;
        }

        received +=
            (size_t)result;
    }

    body[received] = '\0';

    *out_body = body;

    return ESP_OK;
}

static esp_err_t web_settings_api_get_handler(
    httpd_req_t *request
)
{
    if (request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    app_settings_t *settings =
        web_settings_api_allocate_settings();

    if (settings == NULL) {
        return web_api_send_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to allocate settings snapshot"
        );
    }

    esp_err_t result =
        settings_model_get(
            settings
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to get settings: %s",
            esp_err_to_name(result)
        );

        heap_caps_free(settings);

        return web_api_send_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to get settings"
        );
    }

    bool restart_required = false;

    const esp_err_t restart_result =
        settings_service_get_restart_required(
            &restart_required
        );

    if (restart_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to get restart state: %s",
            esp_err_to_name(restart_result)
        );
    }

    bool sta_credentials_configured = false;

    const esp_err_t credentials_result =
        settings_service_get_wifi_sta_credentials_configured(
            &sta_credentials_configured
        );

    if (credentials_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to get Station credential state: %s",
            esp_err_to_name(credentials_result)
        );
    }

    cJSON *response =
        cJSON_CreateObject();

    if (response == NULL) {
        heap_caps_free(settings);
        return ESP_ERR_NO_MEM;
    }

    cJSON *device =
        cJSON_AddObjectToObject(
            response,
            "device"
        );

    cJSON *display =
        cJSON_AddObjectToObject(
            response,
            "display"
        );

    cJSON *logging =
        cJSON_AddObjectToObject(
            response,
            "logging"
        );

    cJSON *tag_levels =
        logging != NULL
            ? cJSON_AddObjectToObject(
                logging,
                "tag_levels"
            )
            : NULL;

    cJSON *ui =
        cJSON_AddObjectToObject(
            response,
            "ui"
        );

    cJSON *network =
        cJSON_AddObjectToObject(
            response,
            "network"
        );

    cJSON *wifi_ap =
        network != NULL
            ? cJSON_AddObjectToObject(
                network,
                "wifi_ap"
            )
            : NULL;

    cJSON *wifi_sta =
        network != NULL
            ? cJSON_AddObjectToObject(
                network,
                "wifi_sta"
            )
            : NULL;

    cJSON *usb_rndis =
        network != NULL
            ? cJSON_AddObjectToObject(
                network,
                "usb_rndis"
            )
            : NULL;

    bool valid =
        (device != NULL) &&
        (display != NULL) &&
        (logging != NULL) &&
        (tag_levels != NULL) &&
        (ui != NULL) &&
        (network != NULL) &&
        (wifi_ap != NULL) &&
        (wifi_sta != NULL) &&
        (usb_rndis != NULL);

    valid = valid &&
        (cJSON_AddNumberToObject(
            response,
            "schema_version",
            settings->schema_version
        ) != NULL);

    valid = valid &&
        (cJSON_AddBoolToObject(
            response,
            "restart_required",
            restart_required
        ) != NULL);

    valid = valid &&
        (cJSON_AddStringToObject(
            device,
            "target",
            settings->device.target
        ) != NULL);

    valid = valid &&
        (cJSON_AddStringToObject(
            device,
            "name",
            settings->device.name
        ) != NULL);

    valid = valid &&
        (cJSON_AddNumberToObject(
            display,
            "brightness",
            settings->display.brightness
        ) != NULL);

    valid = valid &&
        (cJSON_AddBoolToObject(
            logging,
            "sd_enabled",
            settings->logging.sd_enabled
        ) != NULL);

    valid = valid &&
        (cJSON_AddStringToObject(
            tag_levels,
            "warning",
            settings->logging.warning_tags
        ) != NULL);

    valid = valid &&
        (cJSON_AddStringToObject(
            tag_levels,
            "info",
            settings->logging.info_tags
        ) != NULL);

    valid = valid &&
        (cJSON_AddStringToObject(
            tag_levels,
            "debug",
            settings->logging.debug_tags
        ) != NULL);

    valid = valid &&
        (cJSON_AddStringToObject(
            tag_levels,
            "disabled",
            settings->logging.disabled_tags
        ) != NULL);

    valid = valid &&
        (cJSON_AddBoolToObject(
            ui,
            "animations_enabled",
            settings->ui.animations_enabled
        ) != NULL);

    valid = valid &&
        (cJSON_AddBoolToObject(
            wifi_ap,
            "enabled",
            settings->wifi_ap.enabled
        ) != NULL);

    valid = valid &&
        (cJSON_AddStringToObject(
            wifi_ap,
            "ssid",
            settings->wifi_ap.ssid
        ) != NULL);

    valid = valid &&
        (cJSON_AddStringToObject(
            wifi_ap,
            "password",
            settings->wifi_ap.password
        ) != NULL);

    valid = valid &&
        (cJSON_AddBoolToObject(
            wifi_sta,
            "enabled",
            settings->wifi_sta.enabled
        ) != NULL);

    valid = valid &&
        (cJSON_AddStringToObject(
            wifi_sta,
            "ssid",
            settings->wifi_sta.ssid
        ) != NULL);

    valid = valid &&
        (cJSON_AddBoolToObject(
            wifi_sta,
            "credentials_configured",
            sta_credentials_configured
        ) != NULL);

    /*
     * The Station password is stored separately in NVS and must never
     * be returned by the web API.
     */
    valid = valid &&
        (cJSON_AddStringToObject(
            wifi_sta,
            "credential_id",
            settings->wifi_sta.credential_id
        ) != NULL);

    valid = valid &&
        (cJSON_AddBoolToObject(
            usb_rndis,
            "enabled",
            settings->usb_rndis.enabled
        ) != NULL);

    /*
     * cJSON_AddStringToObject() copies all string values, so the
     * settings snapshot is no longer required.
     */
    heap_caps_free(settings);
    settings = NULL;

    if (!valid) {
        cJSON_Delete(response);

        return web_api_send_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to create settings response"
        );
    }

    result =
        web_api_send_json(
            request,
            response
        );

    cJSON_Delete(response);

    return result;
}

static bool web_settings_api_is_client_error(
    esp_err_t result
)
{
    return
        (result == ESP_ERR_INVALID_ARG) ||
        (result == ESP_ERR_INVALID_SIZE);
}

static esp_err_t web_settings_api_send_apply_error(
    httpd_req_t *request,
    esp_err_t result
)
{
    ESP_LOGW(
        TAG,
        "Failed to apply settings: %s",
        esp_err_to_name(result)
    );

    if (web_settings_api_is_client_error(result)) {
        return web_api_send_message(
            request,
            "400 Bad Request",
            false,
            "Invalid settings"
        );
    }

    return web_api_send_message(
        request,
        "500 Internal Server Error",
        false,
        "Failed to apply settings"
    );
}

static esp_err_t web_settings_api_put_handler(
    httpd_req_t *request
)
{
    if (request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char *body = NULL;

    esp_err_t result =
        web_settings_api_receive_body(
            request,
            &body
        );

    if (result != ESP_OK) {
        return web_api_send_message(
            request,
            "400 Bad Request",
            false,
            "Invalid request body"
        );
    }

    cJSON *root =
        cJSON_Parse(body);

    heap_caps_free(body);

    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);

        return web_api_send_message(
            request,
            "400 Bad Request",
            false,
            "Invalid JSON object"
        );
    }

    bool applied = false;

    const cJSON *display =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "display"
        );

    if (display != NULL) {
        if (!cJSON_IsObject(display)) {
            goto invalid_settings;
        }

        const cJSON *brightness =
            cJSON_GetObjectItemCaseSensitive(
                display,
                "brightness"
            );

        if (brightness != NULL) {
            if (!cJSON_IsNumber(brightness) ||
                (brightness->valuedouble <
                 SETTINGS_DISPLAY_BRIGHTNESS_MIN) ||
                (brightness->valuedouble >
                 SETTINGS_DISPLAY_BRIGHTNESS_MAX)) {

                goto invalid_settings;
            }

            result =
                settings_service_set_brightness(
                    (uint8_t)brightness->valueint
                );

            if (result != ESP_OK) {
                goto apply_failed;
            }

            applied = true;
        }
    }

    const cJSON *logging =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "logging"
        );

    if (logging != NULL) {
        if (!cJSON_IsObject(logging)) {
            goto invalid_settings;
        }

        const cJSON *sd_enabled =
            cJSON_GetObjectItemCaseSensitive(
                logging,
                "sd_enabled"
            );

        if (sd_enabled != NULL) {
            if (!cJSON_IsBool(sd_enabled)) {
                goto invalid_settings;
            }

            result =
                settings_service_set_sd_logging_enabled(
                    cJSON_IsTrue(sd_enabled)
                );

            if (result != ESP_OK) {
                goto apply_failed;
            }

            applied = true;
        }

        const cJSON *tag_levels =
            cJSON_GetObjectItemCaseSensitive(
                logging,
                "tag_levels"
            );

        if (tag_levels != NULL) {
            if (!cJSON_IsObject(tag_levels)) {
                goto invalid_settings;
            }

            const cJSON *warning =
                cJSON_GetObjectItemCaseSensitive(
                    tag_levels,
                    "warning"
                );

            const cJSON *info =
                cJSON_GetObjectItemCaseSensitive(
                    tag_levels,
                    "info"
                );

            const cJSON *debug =
                cJSON_GetObjectItemCaseSensitive(
                    tag_levels,
                    "debug"
                );

            const cJSON *disabled =
                cJSON_GetObjectItemCaseSensitive(
                    tag_levels,
                    "disabled"
                );

            const bool has_warning =
                warning != NULL;

            const bool has_info =
                info != NULL;

            const bool has_debug =
                debug != NULL;

            const bool has_disabled =
                disabled != NULL;

            if (!has_warning &&
                !has_info &&
                !has_debug &&
                !has_disabled) {

                goto invalid_settings;
            }

            if ((has_warning &&
                 (!cJSON_IsString(warning) ||
                  (warning->valuestring == NULL))) ||
                (has_info &&
                 (!cJSON_IsString(info) ||
                  (info->valuestring == NULL))) ||
                (has_debug &&
                 (!cJSON_IsString(debug) ||
                  (debug->valuestring == NULL))) ||
                (has_disabled &&
                 (!cJSON_IsString(disabled) ||
                  (disabled->valuestring == NULL)))) {

                goto invalid_settings;
            }

            /*
             * Preserve lists omitted from this partial update.
             */
            app_settings_t *current_settings =
                web_settings_api_allocate_settings();

            if (current_settings == NULL) {
                result = ESP_ERR_NO_MEM;
                goto apply_failed;
            }

            result =
                settings_model_get(
                    current_settings
                );

            if (result != ESP_OK) {
                heap_caps_free(
                    current_settings
                );

                goto apply_failed;
            }

            result =
                settings_service_set_log_tag_levels(
                    has_warning
                        ? warning->valuestring
                        : current_settings->
                            logging.warning_tags,

                    has_info
                        ? info->valuestring
                        : current_settings->
                            logging.info_tags,

                    has_debug
                        ? debug->valuestring
                        : current_settings->
                            logging.debug_tags,

                    has_disabled
                        ? disabled->valuestring
                        : current_settings->
                            logging.disabled_tags
                );

            heap_caps_free(
                current_settings
            );

            if (result != ESP_OK) {
                goto apply_failed;
            }

            applied = true;
        }
    }

    const cJSON *ui =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "ui"
        );

    if (ui != NULL) {
        if (!cJSON_IsObject(ui)) {
            goto invalid_settings;
        }

        const cJSON *animations_enabled =
            cJSON_GetObjectItemCaseSensitive(
                ui,
                "animations_enabled"
            );

        if (animations_enabled != NULL) {
            if (!cJSON_IsBool(animations_enabled)) {
                goto invalid_settings;
            }

            result =
                settings_service_set_animations_enabled(
                    cJSON_IsTrue(
                        animations_enabled
                    )
                );

            if (result != ESP_OK) {
                goto apply_failed;
            }

            applied = true;
        }
    }

    const cJSON *network =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "network"
        );

    if (network != NULL) {
        if (!cJSON_IsObject(network)) {
            goto invalid_settings;
        }

        const cJSON *wifi_ap =
            cJSON_GetObjectItemCaseSensitive(
                network,
                "wifi_ap"
            );

        if (wifi_ap != NULL) {
            if (!cJSON_IsObject(wifi_ap)) {
                goto invalid_settings;
            }

            const cJSON *ssid =
                cJSON_GetObjectItemCaseSensitive(
                    wifi_ap,
                    "ssid"
                );

            const cJSON *password =
                cJSON_GetObjectItemCaseSensitive(
                    wifi_ap,
                    "password"
                );

            /*
             * Both credentials must be provided together to avoid
             * unintentionally replacing only one credential.
             */
            if ((ssid != NULL) ||
                (password != NULL)) {

                if (!cJSON_IsString(ssid) ||
                    !cJSON_IsString(password) ||
                    (ssid->valuestring == NULL) ||
                    (password->valuestring == NULL)) {

                    goto invalid_settings;
                }

                result =
                    settings_service_set_wifi_ap_credentials(
                        ssid->valuestring,
                        password->valuestring
                    );

                if (result != ESP_OK) {
                    goto apply_failed;
                }

                applied = true;
            }

            const cJSON *enabled =
                cJSON_GetObjectItemCaseSensitive(
                    wifi_ap,
                    "enabled"
                );

            if (enabled != NULL) {
                if (!cJSON_IsBool(enabled)) {
                    goto invalid_settings;
                }

                result =
                    settings_service_set_wifi_ap_enabled(
                        cJSON_IsTrue(enabled)
                    );

                if (result != ESP_OK) {
                    goto apply_failed;
                }

                applied = true;
            }
        }

        const cJSON *wifi_sta =
            cJSON_GetObjectItemCaseSensitive(
                network,
                "wifi_sta"
            );

        if (wifi_sta != NULL) {
            if (!cJSON_IsObject(wifi_sta)) {
                goto invalid_settings;
            }

            const cJSON *ssid =
                cJSON_GetObjectItemCaseSensitive(
                    wifi_sta,
                    "ssid"
                );

            const cJSON *password =
                cJSON_GetObjectItemCaseSensitive(
                    wifi_sta,
                    "password"
                );

            /*
             * The password is accepted for update but is never
             * returned by GET /api/settings.
             */
            if ((ssid != NULL) ||
                (password != NULL)) {

                if (!cJSON_IsString(ssid) ||
                    !cJSON_IsString(password) ||
                    (ssid->valuestring == NULL) ||
                    (password->valuestring == NULL)) {

                    goto invalid_settings;
                }

                result =
                    settings_service_set_wifi_sta_credentials(
                        ssid->valuestring,
                        password->valuestring
                    );

                if (result != ESP_OK) {
                    goto apply_failed;
                }

                applied = true;
            }

            const cJSON *enabled =
                cJSON_GetObjectItemCaseSensitive(
                    wifi_sta,
                    "enabled"
                );

            if (enabled != NULL) {
                if (!cJSON_IsBool(enabled)) {
                    goto invalid_settings;
                }

                result =
                    settings_service_set_wifi_sta_enabled(
                        cJSON_IsTrue(enabled)
                    );

                if (result != ESP_OK) {
                    goto apply_failed;
                }

                applied = true;
            }
        }

        const cJSON *usb_rndis =
            cJSON_GetObjectItemCaseSensitive(
                network,
                "usb_rndis"
            );

        if (usb_rndis != NULL) {
            if (!cJSON_IsObject(usb_rndis)) {
                goto invalid_settings;
            }

            const cJSON *enabled =
                cJSON_GetObjectItemCaseSensitive(
                    usb_rndis,
                    "enabled"
                );

            if (enabled != NULL) {
                if (!cJSON_IsBool(enabled)) {
                    goto invalid_settings;
                }

                result =
                    settings_service_set_usb_rndis_enabled(
                        cJSON_IsTrue(enabled)
                    );

                if (result != ESP_OK) {
                    goto apply_failed;
                }

                applied = true;
            }
        }
    }

    if (!applied) {
        goto invalid_settings;
    }

    cJSON_Delete(root);

    bool restart_required = false;

    result =
        settings_service_get_restart_required(
            &restart_required
        );

    cJSON *response =
        cJSON_CreateObject();

    if (response == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const bool valid =
        (cJSON_AddBoolToObject(
            response,
            "success",
            true
        ) != NULL) &&
        (cJSON_AddStringToObject(
            response,
            "message",
            "Settings applied"
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            response,
            "restart_required",
            (result == ESP_OK) &&
            restart_required
        ) != NULL);

    if (!valid) {
        cJSON_Delete(response);
        return ESP_ERR_NO_MEM;
    }

    result =
        web_api_send_json(
            request,
            response
        );

    cJSON_Delete(response);

    return result;

invalid_settings:
    cJSON_Delete(root);

    return web_api_send_message(
        request,
        "400 Bad Request",
        false,
        "Invalid settings"
    );

apply_failed:
    cJSON_Delete(root);

    return web_settings_api_send_apply_error(
        request,
        result
    );
}

static esp_err_t web_settings_api_reload_handler(
    httpd_req_t *request
)
{
    if (request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t result =
        settings_service_reload();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to reload settings: %s",
            esp_err_to_name(result)
        );

        return web_api_send_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to reload settings"
        );
    }

    return web_api_send_message(
        request,
        "200 OK",
        true,
        "Settings reloaded"
    );
}

static esp_err_t
web_settings_api_clear_sta_credentials_handler(
    httpd_req_t *request
)
{
    if (request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t result =
        settings_service_clear_wifi_sta_credentials();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to clear Station credentials: %s",
            esp_err_to_name(result)
        );

        return web_api_send_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to clear Station credentials"
        );
    }

    return web_api_send_message(
        request,
        "200 OK",
        true,
        "Station credentials cleared"
    );
}

static esp_err_t web_settings_api_save_handler(
    httpd_req_t *request
)
{
    if (request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t result =
        settings_service_save();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to save settings: %s",
            esp_err_to_name(result)
        );

        return web_api_send_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to save settings"
        );
    }

    return web_api_send_message(
        request,
        "200 OK",
        true,
        "Settings saved"
    );
}

esp_err_t web_settings_api_register(
    httpd_handle_t server
)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    static const httpd_uri_t get_uri = {
        .uri = "/api/settings",
        .method = HTTP_GET,
        .handler =
            web_settings_api_get_handler,
        .user_ctx = NULL,
    };

    static const httpd_uri_t put_uri = {
        .uri = "/api/settings",
        .method = HTTP_PUT,
        .handler =
            web_settings_api_put_handler,
        .user_ctx = NULL,
    };

    static const httpd_uri_t save_uri = {
        .uri = "/api/settings/save",
        .method = HTTP_POST,
        .handler =
            web_settings_api_save_handler,
        .user_ctx = NULL,
    };

    static const httpd_uri_t reload_uri = {
        .uri = "/api/settings/reload",
        .method = HTTP_POST,
        .handler =
            web_settings_api_reload_handler,
        .user_ctx = NULL,
    };

    static const httpd_uri_t
    clear_sta_credentials_uri = {
        .uri = "/api/settings/wifi/sta/credentials",
        .method = HTTP_DELETE,
        .handler =
            web_settings_api_clear_sta_credentials_handler,
        .user_ctx = NULL,
    };

    esp_err_t result =
        httpd_register_uri_handler(
            server,
            &get_uri
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to register GET /api/settings: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        httpd_register_uri_handler(
            server,
            &put_uri
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to register PUT /api/settings: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        httpd_register_uri_handler(
            server,
            &save_uri
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to register POST /api/settings/save: %s",
            esp_err_to_name(result)
        );
    }

    result = httpd_register_uri_handler(
        server,
        &reload_uri
    );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to register POST /api/settings/reload: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result = httpd_register_uri_handler(
        server,
        &clear_sta_credentials_uri
    );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to register Station credential removal: %s",
            esp_err_to_name(result)
        );
    }

    return result;
}
