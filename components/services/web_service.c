#include "web_service.h"

#include <stddef.h>

#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "web_service";

static httpd_handle_t s_server = NULL;

/*
 * The page is stored in application flash memory.
 * It does not consume internal storage filesystem space.
 */
static const char s_settings_page[] =
    "<!DOCTYPE html>"
    "<html lang=\"en\">"
    "<head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" "
            "content=\"width=device-width,initial-scale=1\">"

        "<title>Spectra Settings</title>"

        "<style>"
            "*{box-sizing:border-box}"

            "body{"
                "margin:0;"
                "min-height:100vh;"
                "font-family:Arial,sans-serif;"
                "background:#f3f5f7;"
                "color:#20252b;"
            "}"

            ".header{"
                "background:#151a20;"
                "color:#fff;"
                "padding:18px 24px;"
                "font-size:22px;"
                "font-weight:600;"
            "}"

            ".container{"
                "max-width:720px;"
                "margin:32px auto;"
                "padding:0 16px;"
            "}"

            ".card{"
                "background:#fff;"
                "border-radius:12px;"
                "padding:24px;"
                "box-shadow:0 4px 16px rgba(0,0,0,.08);"
                "margin-bottom:20px;"
            "}"

            ".card h2{"
                "margin:0 0 20px;"
                "font-size:20px;"
            "}"

            ".field{"
                "display:flex;"
                "align-items:center;"
                "justify-content:space-between;"
                "gap:20px;"
                "margin:18px 0;"
            "}"

            ".field label{"
                "font-weight:500;"
            "}"

            "input[type=range]{"
                "width:240px;"
            "}"

            ".value{"
                "display:inline-block;"
                "min-width:48px;"
                "text-align:right;"
            "}"

            ".status{"
                "color:#66717c;"
                "font-size:14px;"
            "}"

            ".button{"
                "border:0;"
                "border-radius:8px;"
                "padding:11px 18px;"
                "background:#2476ff;"
                "color:#fff;"
                "font-size:15px;"
                "cursor:pointer;"
            "}"

            ".button:hover{"
                "background:#145fd8;"
            "}"

            "@media(max-width:560px){"
                ".field{"
                    "align-items:flex-start;"
                    "flex-direction:column;"
                "}"

                "input[type=range]{"
                    "width:100%;"
                "}"
            "}"
        "</style>"
    "</head>"

    "<body>"
        "<header class=\"header\">Spectra</header>"

        "<main class=\"container\">"
            "<section class=\"card\">"
                "<h2>Device settings</h2>"

                "<div class=\"field\">"
                    "<label>Device address</label>"
                    "<span>172.16.10.1</span>"
                "</div>"

                "<div class=\"field\">"
                    "<label>Device name</label>"
                    "<span>spectra.device</span>"
                "</div>"
            "</section>"

            "<section class=\"card\">"
                "<h2>Display</h2>"

                "<div class=\"field\">"
                    "<label for=\"brightness\">Brightness</label>"

                    "<div>"
                        "<input "
                            "id=\"brightness\" "
                            "type=\"range\" "
                            "min=\"10\" "
                            "max=\"100\" "
                            "value=\"80\">"

                        "<span "
                            "id=\"brightness-value\" "
                            "class=\"value\">"
                            "80%"
                        "</span>"
                    "</div>"
                "</div>"
            "</section>"

            "<section class=\"card\">"
                "<h2>Application</h2>"

                "<div class=\"field\">"
                    "<label for=\"animations\">GUI animations</label>"
                    "<input "
                        "id=\"animations\" "
                        "type=\"checkbox\">"
                "</div>"

                "<div class=\"field\">"
                    "<label for=\"sd-logging\">SD-card logging</label>"
                    "<input "
                        "id=\"sd-logging\" "
                        "type=\"checkbox\">"
                "</div>"

                "<button "
                    "id=\"save\" "
                    "class=\"button\" "
                    "type=\"button\">"
                    "Save settings"
                "</button>"

                "<p id=\"status\" class=\"status\"></p>"
            "</section>"
        "</main>"

        "<script>"
            "const brightness="
                "document.getElementById('brightness');"

            "const brightnessValue="
                "document.getElementById('brightness-value');"

            "brightness.addEventListener('input',()=>{"
                "brightnessValue.textContent="
                    "brightness.value+'%';"
            "});"

            "document.getElementById('save')"
                ".addEventListener('click',()=>{"
                    "document.getElementById('status')"
                        ".textContent="
                            "'Saving is not connected to the API yet.';"
                "});"
        "</script>"
    "</body>"
    "</html>";

static esp_err_t web_service_root_handler(
    httpd_req_t *request
)
{
    if (request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    httpd_resp_set_type(
        request,
        "text/html; charset=utf-8"
    );

    httpd_resp_set_hdr(
        request,
        "Cache-Control",
        "no-store"
    );

    return httpd_resp_send(
        request,
        s_settings_page,
        HTTPD_RESP_USE_STRLEN
    );
}

esp_err_t web_service_start(void)
{
    if (s_server != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_config_t config =
        HTTPD_DEFAULT_CONFIG();

    config.server_port = 80U;
    config.ctrl_port = 32768U;
    config.max_uri_handlers = 8U;
    config.stack_size = 6144U;

    esp_err_t result =
        httpd_start(
            &s_server,
            &config
        );

    if (result != ESP_OK) {
        s_server = NULL;

        ESP_LOGE(
            TAG,
            "Failed to start HTTP server: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    static const httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = web_service_root_handler,
        .user_ctx = NULL,
    };

    result = httpd_register_uri_handler(
        s_server,
        &root_uri
    );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to register root handler: %s",
            esp_err_to_name(result)
        );

        (void)httpd_stop(
            s_server
        );

        s_server = NULL;

        return result;
    }

    ESP_LOGI(
        TAG,
        "Web server started at http://172.16.10.1"
    );

    return ESP_OK;
}

esp_err_t web_service_stop(void)
{
    if (s_server == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result =
        httpd_stop(
            s_server
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to stop HTTP server: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    s_server = NULL;

    ESP_LOGI(
        TAG,
        "Web server stopped"
    );

    return ESP_OK;
}
