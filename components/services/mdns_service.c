#include "mdns_service.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "mdns.h"

#define MDNS_SERVICE_HTTP_PORT  (80U)

#define MDNS_SERVICE_INSTANCE_NAME \
    ("Spectra CAN Analyzer")

#define MDNS_SERVICE_TYPE \
    ("_http._tcp")

#define MDNS_SERVICE_PROTOCOL  ("_tcp")

static const char *TAG =
    "mdns_service";

static bool s_started = false;

static char s_hostname[
    MDNS_SERVICE_HOSTNAME_MAX_LENGTH
];

static esp_err_t mdns_service_build_hostname(void)
{
    uint8_t mac[6] = {0};

    const esp_err_t result =
        esp_read_mac(
            mac,
            ESP_MAC_WIFI_STA
        );

    if (result != ESP_OK) {
        return result;
    }

    const int written =
        snprintf(
            s_hostname,
            sizeof(s_hostname),
            "spectra-%02X%02X%02X",
            mac[3],
            mac[4],
            mac[5]
        );

    if ((written < 0) ||
        ((size_t)written >= sizeof(s_hostname))) {

        s_hostname[0] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t mdns_service_start(void)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result =
        mdns_service_build_hostname();

    if (result != ESP_OK) {
        return result;
    }

    result = mdns_init();

    if (result != ESP_OK) {
        s_hostname[0] = '\0';
        return result;
    }

    result =
        mdns_hostname_set(
            s_hostname
        );

    if (result != ESP_OK) {
        mdns_free();
        s_hostname[0] = '\0';

        return result;
    }

    result =
        mdns_instance_name_set(
            MDNS_SERVICE_INSTANCE_NAME
        );

    if (result != ESP_OK) {
        mdns_free();
        s_hostname[0] = '\0';

        return result;
    }

    mdns_txt_item_t text_records[] = {
        {
            .key = "path",
            .value = "/",
        },
        {
            .key = "device",
            .value = "spectra",
        },
    };

    result =
        mdns_service_add(
            "Spectra Web Interface",
            "_http",
            MDNS_SERVICE_PROTOCOL,
            MDNS_SERVICE_HTTP_PORT,
            text_records,
            sizeof(text_records) /
            sizeof(text_records[0])
        );

    if (result != ESP_OK) {
        mdns_free();
        s_hostname[0] = '\0';

        return result;
    }

    s_started = true;

    ESP_LOGI(
        TAG,
        "mDNS service started: http://%s.local",
        s_hostname
    );

    return ESP_OK;
}

void mdns_service_stop(void)
{
    if (!s_started) {
        return;
    }

    mdns_free();

    s_started = false;
    s_hostname[0] = '\0';

    ESP_LOGI(
        TAG,
        "mDNS service stopped"
    );
}

esp_err_t mdns_service_get_hostname(
    char *hostname,
    size_t hostname_size
)
{
    if ((hostname == NULL) ||
        (hostname_size == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    hostname[0] = '\0';

    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    const size_t required_size =
        strlen(s_hostname) + 1U;

    if (hostname_size < required_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    (void)strlcpy(
        hostname,
        s_hostname,
        hostname_size
    );

    return ESP_OK;
}

esp_err_t mdns_service_get_info(
    mdns_service_info_t *info
)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(
        info,
        0,
        sizeof(*info)
    );

    info->started = s_started;
    info->port = MDNS_SERVICE_HTTP_PORT;

    (void)strlcpy(
        info->instance_name,
        MDNS_SERVICE_INSTANCE_NAME,
        sizeof(info->instance_name)
    );

    (void)strlcpy(
        info->service,
        MDNS_SERVICE_TYPE,
        sizeof(info->service)
    );

    if (s_started) {
        (void)strlcpy(
            info->hostname,
            s_hostname,
            sizeof(info->hostname)
        );
    }

    return ESP_OK;
}
