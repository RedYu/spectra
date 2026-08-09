#include "web_network_api.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "cJSON.h"
#include "esp_log.h"

#include "web_api_common.h"
#include "wifi_service.h"
#include "usb_network_service.h"
#include "mdns_service.h"
#include "network_service.h"

static const char *TAG =
    "web_network_api";

static bool web_network_api_add_dns(
    cJSON *response,
    const network_service_dns_info_t *info
)
{
    if ((response == NULL) ||
        (info == NULL)) {

        return false;
    }

    cJSON *dns =
        cJSON_AddObjectToObject(
            response,
            "dns"
        );

    if (dns == NULL) {
        return false;
    }

    return
        (cJSON_AddBoolToObject(
            dns,
            "started",
            info->started
        ) != NULL) &&
        (cJSON_AddStringToObject(
            dns,
            "local_name",
            info->local_name
        ) != NULL);
}

static bool web_network_api_add_mdns(
    cJSON *response,
    const mdns_service_info_t *info
)
{
    if ((response == NULL) ||
        (info == NULL)) {

        return false;
    }

    cJSON *mdns =
        cJSON_AddObjectToObject(
            response,
            "mdns"
        );

    if (mdns == NULL) {
        return false;
    }

    char address[
        MDNS_SERVICE_HOSTNAME_MAX_LENGTH +
        sizeof(".local")
    ];

    address[0] = '\0';

    if (info->started) {
        const int written =
            snprintf(
                address,
                sizeof(address),
                "%s.local",
                info->hostname
            );

        if ((written < 0) ||
            ((size_t)written >= sizeof(address))) {

            return false;
        }
    }

    return
        (cJSON_AddBoolToObject(
            mdns,
            "started",
            info->started
        ) != NULL) &&
        (cJSON_AddStringToObject(
            mdns,
            "hostname",
            info->hostname
        ) != NULL) &&
        (cJSON_AddStringToObject(
            mdns,
            "address",
            address
        ) != NULL) &&
        (cJSON_AddStringToObject(
            mdns,
            "instance_name",
            info->instance_name
        ) != NULL) &&
        (cJSON_AddStringToObject(
            mdns,
            "service",
            info->service
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            mdns,
            "port",
            info->port
        ) != NULL);
}

static const char *web_network_api_get_wifi_mode(
    const wifi_service_info_t *info
)
{
    if (info == NULL) {
        return "unknown";
    }

    if (info->ap_enabled &&
        info->sta_enabled) {

        return "apsta";
    }

    if (info->ap_enabled) {
        return "ap";
    }

    if (info->sta_enabled) {
        return "sta";
    }

    return "disabled";
}

static bool web_network_api_add_clients(
    cJSON *softap,
    const wifi_service_info_t *info
)
{
    if ((softap == NULL) ||
        (info == NULL)) {

        return false;
    }

    cJSON *clients =
        cJSON_AddArrayToObject(
            softap,
            "clients"
        );

    if (clients == NULL) {
        return false;
    }

    size_t client_count =
        info->client_count;

    if (client_count >
        WIFI_SERVICE_MAX_CLIENT_COUNT) {

        client_count =
            WIFI_SERVICE_MAX_CLIENT_COUNT;
    }

    for (size_t index = 0U;
         index < client_count;
         ++index) {

        const wifi_service_client_info_t *client =
            &info->clients[index];

        char mac_address[18];

        const int written =
            snprintf(
                mac_address,
                sizeof(mac_address),
                "%02X:%02X:%02X:%02X:%02X:%02X",
                client->mac[0],
                client->mac[1],
                client->mac[2],
                client->mac[3],
                client->mac[4],
                client->mac[5]
            );

        if ((written < 0) ||
            ((size_t)written >=
             sizeof(mac_address))) {

            return false;
        }

        cJSON *item =
            cJSON_CreateObject();

        if (item == NULL) {
            return false;
        }

        const bool valid =
            (cJSON_AddStringToObject(
                item,
                "mac_address",
                mac_address
            ) != NULL) &&
            (cJSON_AddNumberToObject(
                item,
                "rssi",
                client->rssi
            ) != NULL) &&
            (cJSON_AddStringToObject(
                item,
                "ip_address",
                client->ip_address
            ) != NULL);

        if (!valid) {
            cJSON_Delete(item);
            return false;
        }

        cJSON_AddItemToArray(
            clients,
            item
        );
    }

    return true;
}

static bool web_network_api_add_softap(
    cJSON *wifi,
    const wifi_service_info_t *info
)
{
    if ((wifi == NULL) ||
        (info == NULL)) {

        return false;
    }

    cJSON *softap =
        cJSON_AddObjectToObject(
            wifi,
            "softap"
        );

    if (softap == NULL) {
        return false;
    }

    cJSON *ipv4 =
        cJSON_AddObjectToObject(
            softap,
            "ipv4"
        );

    cJSON *dhcp =
        cJSON_AddObjectToObject(
            softap,
            "dhcp"
        );

    if ((ipv4 == NULL) ||
        (dhcp == NULL)) {

        return false;
    }

    bool valid =
        (cJSON_AddBoolToObject(
            softap,
            "enabled",
            info->ap_enabled
        ) != NULL) &&
        (cJSON_AddStringToObject(
            softap,
            "ssid",
            info->ssid
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            softap,
            "password_configured",
            info->password_configured
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            softap,
            "channel",
            info->channel
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            softap,
            "client_count",
            info->client_count
        ) != NULL);

    valid = valid &&
        (cJSON_AddStringToObject(
            ipv4,
            "address",
            info->ip_address
        ) != NULL) &&
        (cJSON_AddStringToObject(
            ipv4,
            "netmask",
            info->netmask
        ) != NULL) &&
        (cJSON_AddStringToObject(
            ipv4,
            "dns",
            info->dns_address
        ) != NULL);

    valid = valid &&
        (cJSON_AddStringToObject(
            dhcp,
            "start",
            info->dhcp_start
        ) != NULL) &&
        (cJSON_AddStringToObject(
            dhcp,
            "end",
            info->dhcp_end
        ) != NULL);

    valid = valid &&
        web_network_api_add_clients(
            softap,
            info
        );

    return valid;
}

static bool web_network_api_add_station(
    cJSON *wifi,
    const wifi_service_info_t *info
)
{
    if ((wifi == NULL) ||
        (info == NULL)) {

        return false;
    }

    cJSON *station =
        cJSON_AddObjectToObject(
            wifi,
            "station"
        );

    if (station == NULL) {
        return false;
    }

    cJSON *ipv4 =
        cJSON_AddObjectToObject(
            station,
            "ipv4"
        );

    if (ipv4 == NULL) {
        return false;
    }

    return
        (cJSON_AddBoolToObject(
            station,
            "enabled",
            info->sta_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            station,
            "connected",
            info->sta_connected
        ) != NULL) &&
        (cJSON_AddStringToObject(
            station,
            "ssid",
            info->sta_ssid
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            station,
            "rssi",
            info->sta_rssi
        ) != NULL) &&
        (cJSON_AddStringToObject(
            ipv4,
            "address",
            info->sta_ip_address
        ) != NULL) &&
        (cJSON_AddStringToObject(
            ipv4,
            "netmask",
            info->sta_netmask
        ) != NULL) &&
        (cJSON_AddStringToObject(
            ipv4,
            "gateway",
            info->sta_gateway
        ) != NULL) &&
        (cJSON_AddStringToObject(
            ipv4,
            "dns",
            info->sta_dns_address
        ) != NULL);
}

static bool web_network_api_add_usb_rndis(
    cJSON *response,
    const usb_network_service_info_t *info
)
{
    if ((response == NULL) ||
        (info == NULL)) {

        return false;
    }

    cJSON *usb_rndis =
        cJSON_AddObjectToObject(
            response,
            "usb_rndis"
        );

    if (usb_rndis == NULL) {
        return false;
    }

    char mac_address[18];

    const int written =
        snprintf(
            mac_address,
            sizeof(mac_address),
            "%02X:%02X:%02X:%02X:%02X:%02X",
            info->mac[0],
            info->mac[1],
            info->mac[2],
            info->mac[3],
            info->mac[4],
            info->mac[5]
        );

    if ((written < 0) ||
        ((size_t)written >=
         sizeof(mac_address))) {

        return false;
    }

    bool valid =
        (cJSON_AddBoolToObject(
            usb_rndis,
            "initialized",
            info->initialized
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            usb_rndis,
            "started",
            info->started
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            usb_rndis,
            "host_connected",
            info->host_connected
        ) != NULL) &&
        (cJSON_AddStringToObject(
            usb_rndis,
            "mac_address",
            mac_address
        ) != NULL);

    cJSON *ipv4 =
        cJSON_AddObjectToObject(
            usb_rndis,
            "ipv4"
        );

    cJSON *dhcp =
        cJSON_AddObjectToObject(
            usb_rndis,
            "dhcp"
        );

    valid = valid &&
        (ipv4 != NULL) &&
        (dhcp != NULL);

    if (!valid) {
        return false;
    }

    valid =
        (cJSON_AddStringToObject(
            ipv4,
            "address",
            info->ip_address
        ) != NULL) &&
        (cJSON_AddStringToObject(
            ipv4,
            "netmask",
            info->netmask
        ) != NULL) &&
        (cJSON_AddStringToObject(
            ipv4,
            "dns",
            info->dns_address
        ) != NULL) &&
        (cJSON_AddStringToObject(
            dhcp,
            "start",
            info->dhcp_start
        ) != NULL) &&
        (cJSON_AddStringToObject(
            dhcp,
            "end",
            info->dhcp_end
        ) != NULL);

    return valid;
}

static esp_err_t web_network_api_get_handler(
    httpd_req_t *request
)
{
    if (request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_service_info_t wifi_info = {0};

    const esp_err_t wifi_result =
        wifi_service_get_info(
            &wifi_info
        );

    if (wifi_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to get Wi-Fi information: %s",
            esp_err_to_name(wifi_result)
        );

        return web_api_send_message(
            request,
            "503 Service Unavailable",
            false,
            "Wi-Fi service is not available"
        );
    }

    usb_network_service_info_t usb_info = {0};

    const esp_err_t usb_result =
        usb_network_service_get_info(
            &usb_info
        );

    if (usb_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to get USB network information: %s",
            esp_err_to_name(usb_result)
        );

        return web_api_send_message(
            request,
            "503 Service Unavailable",
            false,
            "USB network service is not available"
        );
    }

    network_service_dns_info_t dns_info = {0};

    const esp_err_t dns_result =
        network_service_get_dns_info(
            &dns_info
        );

    if (dns_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to get DNS information: %s",
            esp_err_to_name(dns_result)
        );

        return web_api_send_message(
            request,
            "503 Service Unavailable",
            false,
            "DNS service information is not available"
        );
    }

    mdns_service_info_t mdns_info = {0};

    const esp_err_t mdns_result =
        mdns_service_get_info(
            &mdns_info
        );

    if (mdns_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to get mDNS information: %s",
            esp_err_to_name(mdns_result)
        );

        return web_api_send_message(
            request,
            "503 Service Unavailable",
            false,
            "mDNS service information is not available"
        );
    }

    cJSON *response =
        cJSON_CreateObject();

    if (response == NULL) {
        return web_api_send_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to allocate network response"
        );
    }

    cJSON *wifi =
        cJSON_AddObjectToObject(
            response,
            "wifi"
        );

    bool valid =
        (wifi != NULL);

    if (valid) {
        valid =
            (cJSON_AddBoolToObject(
                wifi,
                "initialized",
                wifi_info.initialized
            ) != NULL) &&
            (cJSON_AddBoolToObject(
                wifi,
                "started",
                wifi_info.started
            ) != NULL) &&
            (cJSON_AddStringToObject(
                wifi,
                "mode",
                web_network_api_get_wifi_mode(
                    &wifi_info
                )
            ) != NULL);
    }

    if (valid) {
        valid =
            web_network_api_add_softap(
                wifi,
                &wifi_info
            );
    }

    if (valid) {
        valid =
            web_network_api_add_station(
                wifi,
                &wifi_info
            );
    }

    if (valid) {
        valid =
            web_network_api_add_usb_rndis(
                response,
                &usb_info
            );
    }

    if (valid) {
        valid =
            web_network_api_add_dns(
                response,
                &dns_info
            );
    }

    if (valid) {
        valid =
            web_network_api_add_mdns(
                response,
                &mdns_info
            );
    }

    if (!valid) {
        cJSON_Delete(response);

        return web_api_send_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to create network response"
        );
    }

    const esp_err_t result =
        web_api_send_json(
            request,
            response
        );

    cJSON_Delete(response);

    return result;
}

esp_err_t web_network_api_register(
    httpd_handle_t server
)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    static const httpd_uri_t uri = {
        .uri = "/api/network",
        .method = HTTP_GET,
        .handler =
            web_network_api_get_handler,
        .user_ctx = NULL,
    };

    const esp_err_t result =
        httpd_register_uri_handler(
            server,
            &uri
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to register GET /api/network: %s",
            esp_err_to_name(result)
        );
    }

    return result;
}
