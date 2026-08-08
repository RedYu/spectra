#include "wifi_service.h"

#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "esp_netif_ip_addr.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/ip4_addr.h"
#include "dhcpserver/dhcpserver.h"

#include "network_service.h"

#define WIFI_SERVICE_START_TIMEOUT_MS  (3000U)
#define WIFI_SERVICE_LOCK_TIMEOUT_MS   (1000U)

#define WIFI_SERVICE_AP_IP_ADDRESS   "172.16.20.1"

#define WIFI_SERVICE_AP_IP_A         (172U)
#define WIFI_SERVICE_AP_IP_B         (16U)
#define WIFI_SERVICE_AP_IP_C         (20U)
#define WIFI_SERVICE_AP_IP_D         (1U)

#define WIFI_SERVICE_AP_NETMASK_A    (255U)
#define WIFI_SERVICE_AP_NETMASK_B    (255U)
#define WIFI_SERVICE_AP_NETMASK_C    (255U)
#define WIFI_SERVICE_AP_NETMASK_D    (0U)

#define WIFI_SERVICE_AP_CHANNEL      (6U)

#define WIFI_SERVICE_AP_DHCP_START_D  (2U)
#define WIFI_SERVICE_AP_DHCP_END_D    (10U)

#define WIFI_SERVICE_AP_SSID_SUFFIX_LENGTH  (7U)

#define WIFI_SERVICE_AP_SSID_BASE_MAX_LENGTH \
    (WIFI_SERVICE_AP_SSID_MAX_LENGTH -      \
     WIFI_SERVICE_AP_SSID_SUFFIX_LENGTH - 1U)

static const char *TAG = "wifi_service";

/*
 * TODO: Resolve DHCP-assigned client IPv4 addresses by MAC address.
 * The required ESP-NETIF DHCP API depends on the ESP-IDF version.
 */

static SemaphoreHandle_t
    s_state_mutex = NULL;

static SemaphoreHandle_t
    s_started_semaphore = NULL;

static esp_netif_t *s_ap_netif = NULL;
static esp_netif_t *s_sta_netif = NULL;

static SemaphoreHandle_t s_scan_mutex = NULL;

static wifi_service_scan_info_t s_scan_info = {
    .state = WIFI_SERVICE_SCAN_STATE_IDLE,
    .result_count = 0U,
    .truncated = false,
    .last_error = ESP_OK,
};

static wifi_service_scan_result_t s_scan_results[
    WIFI_SERVICE_SCAN_MAX_RESULT_COUNT
];

static bool s_scan_temporary_sta = false;

static esp_event_handler_instance_t
    s_wifi_event_instance = NULL;

static esp_event_handler_instance_t
    s_ip_event_instance = NULL;

static bool s_initialized = false;
static bool s_started = false;

static atomic_bool s_ap_enabled =
    ATOMIC_VAR_INIT(true);

static atomic_bool s_sta_enabled =
    ATOMIC_VAR_INIT(false);

static atomic_bool s_sta_connected =
    ATOMIC_VAR_INIT(false);

static atomic_bool s_reconnect_allowed =
    ATOMIC_VAR_INIT(false);

static char s_ap_ssid_base[
    WIFI_SERVICE_AP_SSID_MAX_LENGTH
] = "Spectra";

static char s_ap_ssid[
    WIFI_SERVICE_AP_SSID_MAX_LENGTH
];

static char s_ap_password[
    WIFI_SERVICE_AP_PASSWORD_MAX_LENGTH
] = "spectra123";

static char s_sta_ssid[
    WIFI_SERVICE_STA_SSID_MAX_LENGTH
] = "";

static char s_sta_password[
    WIFI_SERVICE_STA_PASSWORD_MAX_LENGTH
] = "";

static esp_err_t wifi_service_scan_lock(void);

static void wifi_service_scan_unlock(void);

static void wifi_service_handle_scan_done(
    const wifi_event_sta_scan_done_t *event
);

static void wifi_service_sort_scan_results(void);

static void wifi_service_reset_scan_locked(void);

static bool wifi_service_scan_is_running(void);

static esp_err_t wifi_service_configure_dns(
    esp_netif_t *netif,
    const esp_netif_ip_info_t *ip_info
);

static wifi_mode_t wifi_service_get_mode(void);

static esp_err_t wifi_service_apply_mode(void);

static esp_err_t wifi_service_configure_sta(void);

static esp_err_t wifi_service_start_dhcp_server(void);

static void wifi_service_ip_event_handler(
    void *argument,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
);

static esp_err_t wifi_service_set_interface_enabled_locked(
    bool station,
    bool enabled
);

static esp_err_t wifi_service_lock(void)
{
    if (s_state_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_state_mutex,
            pdMS_TO_TICKS(
                WIFI_SERVICE_LOCK_TIMEOUT_MS
            )
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void wifi_service_unlock(void)
{
    if (s_state_mutex != NULL) {
        (void)xSemaphoreGive(
            s_state_mutex
        );
    }
}

static esp_err_t wifi_service_scan_lock(void)
{
    if (s_scan_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_scan_mutex,
            pdMS_TO_TICKS(
                WIFI_SERVICE_LOCK_TIMEOUT_MS
            )
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void wifi_service_scan_unlock(void)
{
    if (s_scan_mutex != NULL) {
        (void)xSemaphoreGive(
            s_scan_mutex
        );
    }
}

static void wifi_service_reset_scan_locked(void)
{
    memset(
        s_scan_results,
        0,
        sizeof(s_scan_results)
    );

    s_scan_info.state =
        WIFI_SERVICE_SCAN_STATE_IDLE;

    s_scan_info.result_count = 0U;
    s_scan_info.truncated = false;
    s_scan_info.last_error = ESP_OK;

    s_scan_temporary_sta = false;
}

static bool wifi_service_scan_is_running(void)
{
    if (wifi_service_scan_lock() != ESP_OK) {
        /*
         * Conservatively reject mode changes when the scan state
         * cannot be inspected.
         */
        return true;
    }

    const bool running =
        s_scan_info.state ==
        WIFI_SERVICE_SCAN_STATE_RUNNING;

    wifi_service_scan_unlock();

    return running;
}

static void wifi_service_sort_scan_results(void)
{
    for (size_t index = 1U;
         index < s_scan_info.result_count;
         ++index) {

        const wifi_service_scan_result_t current =
            s_scan_results[index];

        size_t position = index;

        while ((position > 0U) &&
               (s_scan_results[position - 1U].rssi <
                current.rssi)) {

            s_scan_results[position] =
                s_scan_results[position - 1U];

            --position;
        }

        s_scan_results[position] =
            current;
    }
}

static void wifi_service_handle_scan_done(
    const wifi_event_sta_scan_done_t *event
)
{
    if (wifi_service_scan_lock() != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to lock completed scan"
        );

        return;
    }

    if (s_scan_info.state !=
        WIFI_SERVICE_SCAN_STATE_RUNNING) {

        wifi_service_scan_unlock();

        (void)esp_wifi_clear_ap_list();

        return;
    }

    if ((event == NULL) ||
        (event->status != 0U)) {

        (void)esp_wifi_clear_ap_list();

        s_scan_info.state =
            WIFI_SERVICE_SCAN_STATE_ERROR;

        s_scan_info.result_count = 0U;
        s_scan_info.truncated = false;
        s_scan_info.last_error = ESP_FAIL;

        goto restore_mode;
    }

    uint16_t discovered_count = 0U;

    esp_err_t result =
        esp_wifi_scan_get_ap_num(
            &discovered_count
        );

    if (result != ESP_OK) {
        s_scan_info.state =
            WIFI_SERVICE_SCAN_STATE_ERROR;

        s_scan_info.last_error = result;

        goto restore_mode;
    }

    uint16_t requested_count =
        discovered_count;

    if (requested_count >
        WIFI_SERVICE_SCAN_MAX_RESULT_COUNT) {

        requested_count =
            WIFI_SERVICE_SCAN_MAX_RESULT_COUNT;
    }

    wifi_ap_record_t *records = NULL;

    if (requested_count > 0U) {
        /*
         * Scan records are temporary and do not require DMA-capable
         * memory, so prefer PSRAM to preserve internal heap.
         */
        records = heap_caps_calloc(
            requested_count,
            sizeof(*records),
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

        if (records == NULL) {
            records = calloc(
                requested_count,
                sizeof(*records)
            );
        }

        if (records == NULL) {
            (void)esp_wifi_clear_ap_list();

            s_scan_info.state =
                WIFI_SERVICE_SCAN_STATE_ERROR;

            s_scan_info.last_error =
                ESP_ERR_NO_MEM;

            goto restore_mode;
        }

        result =
            esp_wifi_scan_get_ap_records(
                &requested_count,
                records
            );

        if (result != ESP_OK) {
            free(records);

            s_scan_info.state =
                WIFI_SERVICE_SCAN_STATE_ERROR;

            s_scan_info.last_error = result;

            goto restore_mode;
        }
    } else {
        /*
         * Explicitly release the driver's empty scan list.
         */
        (void)esp_wifi_clear_ap_list();
    }

    memset(
        s_scan_results,
        0,
        sizeof(s_scan_results)
    );

    for (size_t index = 0U;
         index < requested_count;
         ++index) {

        wifi_service_scan_result_t *destination =
            &s_scan_results[index];

        const wifi_ap_record_t *source =
            &records[index];

        size_t ssid_length =
            strnlen(
                (const char *)source->ssid,
                sizeof(source->ssid)
            );

        if (ssid_length >=
            sizeof(destination->ssid)) {

            ssid_length =
                sizeof(destination->ssid) - 1U;
        }

        memcpy(
            destination->ssid,
            source->ssid,
            ssid_length
        );

        destination->ssid[ssid_length] = '\0';

        memcpy(
            destination->bssid,
            source->bssid,
            sizeof(destination->bssid)
        );

        destination->rssi =
            source->rssi;

        destination->channel =
            source->primary;

        destination->password_required =
            source->authmode != WIFI_AUTH_OPEN;
    }

    free(records);

    s_scan_info.result_count =
        requested_count;

    s_scan_info.truncated =
        discovered_count > requested_count;

    s_scan_info.state =
        WIFI_SERVICE_SCAN_STATE_COMPLETE;

    s_scan_info.last_error = ESP_OK;

    wifi_service_sort_scan_results();

restore_mode:
    if (s_scan_temporary_sta) {
        const esp_err_t restore_result =
            esp_wifi_set_mode(
                WIFI_MODE_AP
            );

        if (restore_result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Failed to restore AP mode after scan: %s",
                esp_err_to_name(restore_result)
            );

            s_scan_info.state =
                WIFI_SERVICE_SCAN_STATE_ERROR;

            s_scan_info.last_error =
                restore_result;
        }

        s_scan_temporary_sta = false;
    }

    const wifi_service_scan_state_t final_state =
        s_scan_info.state;

    const size_t result_count =
        s_scan_info.result_count;

    const esp_err_t scan_error =
        s_scan_info.last_error;

    wifi_service_scan_unlock();

    if (final_state ==
        WIFI_SERVICE_SCAN_STATE_COMPLETE) {

        ESP_LOGI(
            TAG,
            "Wi-Fi scan completed: %u network(s)",
            (unsigned int)result_count
        );
    } else {
        ESP_LOGE(
            TAG,
            "Wi-Fi scan failed: %s",
            esp_err_to_name(scan_error)
        );
    }
}

static esp_err_t wifi_service_build_ssid(void)
{
    uint8_t mac[
        WIFI_SERVICE_MAC_ADDRESS_LENGTH
    ];

    const esp_err_t result =
        esp_read_mac(
            mac,
            ESP_MAC_WIFI_SOFTAP
        );

    if (result != ESP_OK) {
        return result;
    }

    const size_t base_length =
        strlen(s_ap_ssid_base);

    if ((base_length == 0U) ||
        (base_length >
         WIFI_SERVICE_AP_SSID_BASE_MAX_LENGTH)) {

        return ESP_ERR_INVALID_SIZE;
    }

    const int written =
        snprintf(
            s_ap_ssid,
            sizeof(s_ap_ssid),
            "%s-%02X%02X%02X",
            s_ap_ssid_base,
            mac[3],
            mac[4],
            mac[5]
        );

    if ((written < 0) ||
        ((size_t)written >=
         sizeof(s_ap_ssid))) {

        s_ap_ssid[0] = '\0';

        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static wifi_mode_t wifi_service_get_mode(void)
{
    const bool ap_enabled =
        atomic_load(&s_ap_enabled);

    const bool sta_enabled =
        atomic_load(&s_sta_enabled);

    if (ap_enabled && sta_enabled) {
        return WIFI_MODE_APSTA;
    }

    if (ap_enabled) {
        return WIFI_MODE_AP;
    }

    if (sta_enabled) {
        return WIFI_MODE_STA;
    }

    return WIFI_MODE_NULL;
}

static esp_err_t wifi_service_apply_mode(void)
{
    return esp_wifi_set_mode(
        wifi_service_get_mode()
    );
}

static void wifi_service_event_handler(
    void *argument,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
)
{
    (void)argument;
    (void)event_base;

    if (event_id == WIFI_EVENT_SCAN_DONE) {
        wifi_service_handle_scan_done(
            (const wifi_event_sta_scan_done_t *)
                event_data
        );

        return;
    }

    /*
     * When SoftAP is enabled, wait specifically for AP_START because
     * its DHCP server is checked immediately after this notification.
     * In Station-only mode, STA_START is sufficient.
     */
    const bool startup_completed =
        (atomic_load(&s_ap_enabled) &&
         (event_id == WIFI_EVENT_AP_START)) ||
        (!atomic_load(&s_ap_enabled) &&
         atomic_load(&s_sta_enabled) &&
         (event_id == WIFI_EVENT_STA_START));

    if (startup_completed &&
        (s_started_semaphore != NULL)) {

        (void)xSemaphoreGive(
            s_started_semaphore
        );
    }

    if (event_id == WIFI_EVENT_STA_START) {
        if (atomic_load(&s_sta_enabled) &&
            atomic_load(&s_reconnect_allowed)) {
            const esp_err_t result =
                esp_wifi_connect();

            if (result != ESP_OK) {
                ESP_LOGW(
                    TAG,
                    "Failed to start Station connection: %s",
                    esp_err_to_name(result)
                );
            }
        }

        return;
    }

    if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event =
            event_data;

        atomic_store(
            &s_sta_connected,
            false
        );

        if (event != NULL) {
            ESP_LOGW(
                TAG,
                "Station disconnected: "
                "reason=%u, RSSI=%d, BSSID=" MACSTR,
                (unsigned int)event->reason,
                (int)event->rssi,
                MAC2STR(event->bssid)
            );
        } else {
            ESP_LOGW(
                TAG,
                "Station disconnected without event data"
            );
        }

        if (atomic_load(&s_sta_enabled) &&
            atomic_load(&s_reconnect_allowed)) {

            const esp_err_t result =
                esp_wifi_connect();

            if (result != ESP_OK) {
                ESP_LOGW(
                    TAG,
                    "Failed to reconnect Station: %s",
                    esp_err_to_name(result)
                );
            }
        }

        return;
    }

    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *event =
            event_data;

        if (event != NULL) {
            ESP_LOGI(
                TAG,
                "SoftAP client connected, AID=%d",
                event->aid
            );
        }

        return;
    }

    if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *event =
            event_data;

        if (event != NULL) {
            ESP_LOGI(
                TAG,
                "SoftAP client disconnected, AID=%d",
                event->aid
            );
        }
    }
}

static void wifi_service_ip_event_handler(
    void *argument,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
)
{
    (void)argument;
    (void)event_base;

    if (event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event =
            event_data;

        if (event != NULL) {
            ESP_LOGI(
                TAG,
                "Station connected, IP=" IPSTR,
                IP2STR(
                    &event->ip_info.ip
                )
            );
        }

        bool network_ready = false;

        if (s_sta_netif != NULL) {
            const esp_err_t default_result =
                esp_netif_set_default_netif(
                    s_sta_netif
                );

            if (default_result != ESP_OK) {
                ESP_LOGW(
                    TAG,
                    "Failed to select Station as default interface: %s",
                    esp_err_to_name(default_result)
                );
            } else {
                network_ready = true;
            }

            esp_netif_dns_info_t dns_info = {0};

            const esp_err_t dns_result =
                esp_netif_get_dns_info(
                    s_sta_netif,
                    ESP_NETIF_DNS_MAIN,
                    &dns_info
                );

            if ((dns_result == ESP_OK) &&
                (dns_info.ip.type ==
                 ESP_IPADDR_TYPE_V4) &&
                (dns_info.ip.u_addr.ip4.addr != 0U)) {

                ESP_LOGI(
                    TAG,
                    "Station DNS: " IPSTR,
                    IP2STR(
                        &dns_info.ip.u_addr.ip4
                    )
                );
            } else if (dns_result != ESP_OK) {
                ESP_LOGW(
                    TAG,
                    "Failed to get Station DNS: %s",
                    esp_err_to_name(dns_result)
                );
            } else {
                ESP_LOGW(
                    TAG,
                    "Station did not receive a valid IPv4 DNS server"
                );
            }
        }

        atomic_store(
            &s_sta_connected,
            network_ready
        );

        return;
    }

    if (event_id == IP_EVENT_STA_LOST_IP) {
        atomic_store(
            &s_sta_connected,
            false
        );
    }
}

static esp_err_t wifi_service_configure_ip(void)
{
    if (s_ap_netif == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result =
        esp_netif_dhcps_stop(
            s_ap_netif
        );

    if ((result != ESP_OK) &&
        (result !=
         ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)) {

        return result;
    }

    esp_netif_ip_info_t ip_info = {0};

    IP4_ADDR(
        &ip_info.ip,
        WIFI_SERVICE_AP_IP_A,
        WIFI_SERVICE_AP_IP_B,
        WIFI_SERVICE_AP_IP_C,
        WIFI_SERVICE_AP_IP_D
    );

    IP4_ADDR(
        &ip_info.gw,
        WIFI_SERVICE_AP_IP_A,
        WIFI_SERVICE_AP_IP_B,
        WIFI_SERVICE_AP_IP_C,
        WIFI_SERVICE_AP_IP_D
    );

    IP4_ADDR(
        &ip_info.netmask,
        WIFI_SERVICE_AP_NETMASK_A,
        WIFI_SERVICE_AP_NETMASK_B,
        WIFI_SERVICE_AP_NETMASK_C,
        WIFI_SERVICE_AP_NETMASK_D
    );

    result = esp_netif_set_ip_info(
        s_ap_netif,
        &ip_info
    );

    if (result != ESP_OK) {
        return result;
    }

    result = wifi_service_configure_dns(
        s_ap_netif,
        &ip_info
    );

    if (result != ESP_OK) {
        return result;
    }

    const dhcps_lease_t lease = {
        .enable = true,

        .start_ip = {
            .addr = ESP_IP4TOADDR(
                WIFI_SERVICE_AP_IP_A,
                WIFI_SERVICE_AP_IP_B,
                WIFI_SERVICE_AP_IP_C,
                WIFI_SERVICE_AP_DHCP_START_D
            ),
        },

        .end_ip = {
            .addr = ESP_IP4TOADDR(
                WIFI_SERVICE_AP_IP_A,
                WIFI_SERVICE_AP_IP_B,
                WIFI_SERVICE_AP_IP_C,
                WIFI_SERVICE_AP_DHCP_END_D
            ),
        },
    };

    result = esp_netif_dhcps_option(
        s_ap_netif,
        ESP_NETIF_OP_SET,
        ESP_NETIF_REQUESTED_IP_ADDRESS,
        (void *)&lease,
        sizeof(lease)
    );

    if (result != ESP_OK) {
        return result;
    }

    return ESP_OK;
}

static esp_err_t wifi_service_start_dhcp_server(void)
{
    if (s_ap_netif == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_dhcp_status_t status =
        ESP_NETIF_DHCP_INIT;

    esp_err_t result =
        esp_netif_dhcps_get_status(
            s_ap_netif,
            &status
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to get DHCP server state: %s (0x%04X)",
            esp_err_to_name(result),
            (unsigned int)result
        );

        return result;
    }

    if (status == ESP_NETIF_DHCP_STARTED) {
        return ESP_OK;
    }

    result =
        esp_netif_dhcps_start(
            s_ap_netif
        );

    if (result ==
        ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {

        return ESP_OK;
    }

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to start DHCP server: %s (0x%04X)",
            esp_err_to_name(result),
            (unsigned int)result
        );

        return result;
    }

    return ESP_OK;
}

static esp_err_t wifi_service_configure_ap(void)
{
    wifi_config_t config = {0};

    const size_t ssid_length =
        strlen(s_ap_ssid);

    const size_t password_length =
        strlen(s_ap_password);

    if ((ssid_length == 0U) ||
        (ssid_length >
         sizeof(config.ap.ssid))) {

        return ESP_ERR_INVALID_SIZE;
    }

    if ((password_length != 0U) &&
        ((password_length <
          WIFI_SERVICE_AP_PASSWORD_MIN_LENGTH) ||
         (password_length >=
          sizeof(config.ap.password)))) {

        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(
        config.ap.ssid,
        s_ap_ssid,
        ssid_length
    );

    config.ap.ssid_len =
        (uint8_t)ssid_length;

    if (password_length > 0U) {
        memcpy(
            config.ap.password,
            s_ap_password,
            password_length
        );

        config.ap.authmode =
            WIFI_AUTH_WPA2_PSK;
    } else {
        config.ap.authmode =
            WIFI_AUTH_OPEN;
    }

    config.ap.channel =
        WIFI_SERVICE_AP_CHANNEL;

    config.ap.max_connection =
        WIFI_SERVICE_MAX_CLIENT_COUNT;

    config.ap.pmf_cfg.capable = true;
    config.ap.pmf_cfg.required = false;

    return esp_wifi_set_config(
        WIFI_IF_AP,
        &config
    );
}

static esp_err_t wifi_service_configure_sta(void)
{
    const size_t ssid_length =
        strlen(s_sta_ssid);

    const size_t password_length =
        strlen(s_sta_password);

    if ((ssid_length == 0U) ||
        (ssid_length >=
         WIFI_SERVICE_STA_SSID_MAX_LENGTH)) {

        return ESP_ERR_INVALID_SIZE;
    }

    if (password_length >=
        WIFI_SERVICE_STA_PASSWORD_MAX_LENGTH) {

        return ESP_ERR_INVALID_SIZE;
    }

    if ((password_length != 0U) &&
        (password_length <
         WIFI_SERVICE_STA_PASSWORD_MIN_LENGTH)) {

        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t config = {0};

    memcpy(
        config.sta.ssid,
        s_sta_ssid,
        ssid_length
    );

    if (password_length > 0U) {
        memcpy(
            config.sta.password,
            s_sta_password,
            password_length
        );

        config.sta.threshold.authmode =
            WIFI_AUTH_WPA2_PSK;
    } else {
        config.sta.threshold.authmode =
            WIFI_AUTH_OPEN;
    }

    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;

    return esp_wifi_set_config(
        WIFI_IF_STA,
        &config
    );
}

static esp_err_t wifi_service_configure_dns(
    esp_netif_t *netif,
    const esp_netif_ip_info_t *ip_info
)
{
    if ((netif == NULL) ||
        (ip_info == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_dns_info_t dns_info = {0};

    dns_info.ip.type =
        ESP_IPADDR_TYPE_V4;

    dns_info.ip.u_addr.ip4.addr =
        ip_info->ip.addr;

    esp_err_t result =
        esp_netif_set_dns_info(
            netif,
            ESP_NETIF_DNS_MAIN,
            &dns_info
        );

    if (result != ESP_OK) {
        return result;
    }

    /*
     * Enable the DNS server option in DHCP responses.
     */
    uint8_t dns_offer_enabled = 1U;

    return esp_netif_dhcps_option(
        netif,
        ESP_NETIF_OP_SET,
        ESP_NETIF_DOMAIN_NAME_SERVER,
        &dns_offer_enabled,
        sizeof(dns_offer_enabled)
    );
}

static void wifi_service_cleanup(void)
{
    if (s_ip_event_instance != NULL) {
        (void)esp_event_handler_instance_unregister(
            IP_EVENT,
            ESP_EVENT_ANY_ID,
            s_ip_event_instance
        );

        s_ip_event_instance = NULL;
    }

    if (s_wifi_event_instance != NULL) {
        (void)esp_event_handler_instance_unregister(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            s_wifi_event_instance
        );

        s_wifi_event_instance = NULL;
    }

    if (s_started_semaphore != NULL) {
        vSemaphoreDelete(
            s_started_semaphore
        );

        s_started_semaphore = NULL;
    }

    if (s_sta_netif != NULL) {
        esp_netif_destroy_default_wifi(
            s_sta_netif
        );

        s_sta_netif = NULL;
    }

    if (s_ap_netif != NULL) {
        esp_netif_destroy_default_wifi(
            s_ap_netif
        );

        s_ap_netif = NULL;
    }

    atomic_store(
        &s_sta_connected,
        false
    );

    atomic_store(
        &s_reconnect_allowed,
        false
    );

    s_initialized = false;
    s_started = false;
}

esp_err_t wifi_service_init(void)
{
    if (s_state_mutex == NULL) {
        s_state_mutex =
            xSemaphoreCreateMutex();

        if (s_state_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_scan_mutex == NULL) {
        s_scan_mutex =
            xSemaphoreCreateMutex();

        if (s_scan_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t result =
        wifi_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (s_initialized) {
        wifi_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    result =
        wifi_service_scan_lock();

    if (result != ESP_OK) {
        wifi_service_unlock();

        return result;
    }

    wifi_service_reset_scan_locked();

    wifi_service_scan_unlock();

    bool network_initialized = false;

    result = network_service_get_initialized(
        &network_initialized
    );

    if (result != ESP_OK) {
        wifi_service_unlock();

        return result;
    }

    if (!network_initialized) {
        ESP_LOGE(
            TAG,
            "Global network stack is not initialized"
        );

        wifi_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    /*
     * The remainder of initialization runs while holding the service
     * mutex, preventing concurrent start, stop and getter operations.
     */

    s_started_semaphore =
        xSemaphoreCreateBinary();

    if (s_started_semaphore == NULL) {
        wifi_service_unlock();
        return ESP_ERR_NO_MEM;
    }

    result =
        wifi_service_build_ssid();

    if (result != ESP_OK) {
        wifi_service_cleanup();
        wifi_service_unlock();

        return result;
    }

    s_ap_netif =
        esp_netif_create_default_wifi_ap();

    if (s_ap_netif == NULL) {
        wifi_service_cleanup();
        wifi_service_unlock();

        return ESP_ERR_NO_MEM;
    }

    s_sta_netif =
        esp_netif_create_default_wifi_sta();

    if (s_sta_netif == NULL) {
        wifi_service_cleanup();
        wifi_service_unlock();

        return ESP_ERR_NO_MEM;
    }

    esp_netif_dns_info_t fallback_dns = {
        .ip = {
            .u_addr = {
                .ip4 = {
                    .addr = ESP_IP4TOADDR(
                        1U,
                        1U,
                        1U,
                        1U
                    ),
                },
            },
            .type = ESP_IPADDR_TYPE_V4,
        },
    };

    const esp_err_t dns_result =
        esp_netif_set_dns_info(
            s_sta_netif,
            ESP_NETIF_DNS_FALLBACK,
            &fallback_dns
        );

    if (dns_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to configure fallback DNS: %s",
            esp_err_to_name(dns_result)
        );
    }

    wifi_init_config_t wifi_config =
        WIFI_INIT_CONFIG_DEFAULT();

    /*
     * Do not persist the current SoftAP configuration. The PHY subsystem
     * may still use the globally initialized NVS for RF calibration data.
     */
    wifi_config.nvs_enable = 0;

    ESP_LOGI(
        TAG,
        "Wi-Fi init memory: "
        "internal=%u, largest=%u, dma=%u, psram=%u",
        (unsigned int)heap_caps_get_free_size(
            MALLOC_CAP_INTERNAL |
            MALLOC_CAP_8BIT
        ),
        (unsigned int)heap_caps_get_largest_free_block(
            MALLOC_CAP_INTERNAL |
            MALLOC_CAP_8BIT
        ),
        (unsigned int)heap_caps_get_free_size(
            MALLOC_CAP_DMA
        ),
        (unsigned int)heap_caps_get_free_size(
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        )
    );

    result = esp_wifi_init(
        &wifi_config
    );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize Wi-Fi: %s, "
            "internal=%u, largest=%u",
            esp_err_to_name(result),
            (unsigned int)heap_caps_get_free_size(
                MALLOC_CAP_INTERNAL |
                MALLOC_CAP_8BIT
            ),
            (unsigned int)heap_caps_get_largest_free_block(
                MALLOC_CAP_INTERNAL |
                MALLOC_CAP_8BIT
            )
        );

        wifi_service_cleanup();
        wifi_service_unlock();

        return result;
    }

    result = esp_wifi_set_storage(
        WIFI_STORAGE_RAM
    );

    if (result != ESP_OK) {
        (void)esp_wifi_deinit();
        wifi_service_cleanup();
        wifi_service_unlock();

        return result;
    }

    result =
        esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            wifi_service_event_handler,
            NULL,
            &s_wifi_event_instance
        );

    if (result != ESP_OK) {
        (void)esp_wifi_deinit();
        wifi_service_cleanup();
        wifi_service_unlock();

        return result;
    }

    result =
        esp_event_handler_instance_register(
            IP_EVENT,
            ESP_EVENT_ANY_ID,
            wifi_service_ip_event_handler,
            NULL,
            &s_ip_event_instance
        );

    if (result != ESP_OK) {
        (void)esp_wifi_deinit();
        wifi_service_cleanup();
        wifi_service_unlock();

        return result;
    }

    result =
        wifi_service_apply_mode();

    if (result != ESP_OK) {
        (void)esp_wifi_deinit();
        wifi_service_cleanup();
        wifi_service_unlock();

        return result;
    }

    result = wifi_service_configure_ip();

    if (result != ESP_OK) {
        (void)esp_wifi_deinit();
        wifi_service_cleanup();
        wifi_service_unlock();

        return result;
    }

    if (atomic_load(&s_ap_enabled)) {
        result =
            wifi_service_configure_ap();

        if (result != ESP_OK) {
            (void)esp_wifi_deinit();
            wifi_service_cleanup();
            wifi_service_unlock();

            return result;
        }
    }

    if (atomic_load(&s_sta_enabled)) {
        result =
            wifi_service_configure_sta();

        if (result != ESP_OK) {
            (void)esp_wifi_deinit();
            wifi_service_cleanup();
            wifi_service_unlock();

            return result;
        }
    }

    s_initialized = true;

    ESP_LOGI(
        TAG,
        "Wi-Fi service initialized: AP=%s, STA=%s",
        atomic_load(&s_ap_enabled)
            ? "enabled"
            : "disabled",
        atomic_load(&s_sta_enabled)
            ? "enabled"
            : "disabled"
    );

    wifi_service_unlock();

    return ESP_OK;
}

esp_err_t wifi_service_start(void)
{
    esp_err_t result =
        wifi_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!s_initialized ||
        s_started ||
        (s_started_semaphore == NULL)) {

        wifi_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    const wifi_mode_t mode =
        wifi_service_get_mode();

    if (mode == WIFI_MODE_NULL) {
        wifi_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    result =
        wifi_service_apply_mode();

    if (result != ESP_OK) {
        wifi_service_unlock();

        return result;
    }

    /*
     * Remove a stale notification left by an earlier start attempt.
     */
    while (xSemaphoreTake(
               s_started_semaphore,
               0U
           ) == pdTRUE) {
    }

    atomic_store(
        &s_sta_connected,
        false
    );

    atomic_store(
        &s_reconnect_allowed,
        true
    );

    result =
        esp_wifi_start();

    if (result != ESP_OK) {
        atomic_store(
            &s_reconnect_allowed,
            false
        );

        wifi_service_unlock();

        return result;
    }

    const esp_err_t ps_result =
        esp_wifi_set_ps(
            WIFI_PS_NONE
        );

    if (ps_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to disable Wi-Fi power saving: %s",
            esp_err_to_name(ps_result)
        );
    }

    /*
     * AP_START or STA_START indicates that at least one configured
     * interface has started.
     */
    if (xSemaphoreTake(
            s_started_semaphore,
            pdMS_TO_TICKS(
                WIFI_SERVICE_START_TIMEOUT_MS
            )
        ) != pdTRUE) {

        ESP_LOGE(
            TAG,
            "Timed out waiting for Wi-Fi startup"
        );

        atomic_store(
            &s_reconnect_allowed,
            false
        );

        (void)esp_wifi_stop();

        wifi_service_unlock();

        return ESP_ERR_TIMEOUT;
    }

    /*
     * AP_START confirms that the SoftAP network interface is ready.
     * Start DHCP explicitly if the default ESP-NETIF handler has not
     * already started it.
     */
    if (atomic_load(&s_ap_enabled)) {
        result =
            wifi_service_start_dhcp_server();

        if (result != ESP_OK) {
            atomic_store(
                &s_reconnect_allowed,
                false
            );

            const esp_err_t stop_result =
                esp_wifi_stop();

            if (stop_result != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "Failed to stop Wi-Fi after DHCP error: %s",
                    esp_err_to_name(stop_result)
                );
            }

            atomic_store(
                &s_sta_connected,
                false
            );

            wifi_service_unlock();

            return result;
        }
    }

    s_started = true;

    if (mode == WIFI_MODE_APSTA) {
        ESP_LOGI(
            TAG,
            "Wi-Fi started in APSTA mode: SSID=%s",
            s_ap_ssid
        );

    } else if (mode == WIFI_MODE_AP) {
        ESP_LOGI(
            TAG,
            "Wi-Fi started in AP mode: SSID=%s, IP=%s",
            s_ap_ssid,
            WIFI_SERVICE_AP_IP_ADDRESS
        );

    } else {
        ESP_LOGI(
            TAG,
            "Wi-Fi started in STA mode: SSID=%s",
            s_sta_ssid
        );
    }

    wifi_service_unlock();

    return ESP_OK;
}

esp_err_t wifi_service_stop(void)
{
    esp_err_t result =
        wifi_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!s_initialized) {
        wifi_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    if (!s_started) {
        wifi_service_unlock();

        return ESP_OK;
    }

    result =
        wifi_service_scan_lock();

    if (result != ESP_OK) {
        wifi_service_unlock();

        return result;
    }

    if (s_scan_info.state ==
        WIFI_SERVICE_SCAN_STATE_RUNNING) {

        const esp_err_t stop_scan_result =
            esp_wifi_scan_stop();

        if ((stop_scan_result != ESP_OK) &&
            (stop_scan_result !=
            ESP_ERR_WIFI_NOT_STARTED)) {

            wifi_service_scan_unlock();
            wifi_service_unlock();

            return stop_scan_result;
        }
    }

    wifi_service_reset_scan_locked();

    wifi_service_scan_unlock();

    /*
     * Prevent the disconnect event from starting a new connection
     * while the driver is being stopped intentionally.
     */
    const bool reconnect_was_allowed =
        atomic_exchange(
            &s_reconnect_allowed,
            false
        );

    result =
        esp_wifi_stop();

    if (result != ESP_OK) {
        atomic_store(
            &s_reconnect_allowed,
            reconnect_was_allowed
        );

        wifi_service_unlock();

        return result;
    }

    atomic_store(
        &s_sta_connected,
        false
    );

    s_started = false;

    ESP_LOGI(
        TAG,
        "Wi-Fi service stopped"
    );

    wifi_service_unlock();

    return ESP_OK;
}

esp_err_t wifi_service_deinit(void)
{
    esp_err_t result =
        wifi_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!s_initialized) {
        wifi_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    result =
        wifi_service_scan_lock();

    if (result != ESP_OK) {
        wifi_service_unlock();

        return result;
    }

    if (s_scan_info.state ==
        WIFI_SERVICE_SCAN_STATE_RUNNING) {

        const esp_err_t stop_scan_result =
            esp_wifi_scan_stop();

        if ((stop_scan_result != ESP_OK) &&
            (stop_scan_result !=
            ESP_ERR_WIFI_NOT_STARTED)) {

            wifi_service_scan_unlock();
            wifi_service_unlock();

            return stop_scan_result;
        }
    }

    wifi_service_reset_scan_locked();

    wifi_service_scan_unlock();

    const bool reconnect_was_allowed =
        atomic_exchange(
            &s_reconnect_allowed,
            false
        );

    if (s_started) {
        result =
            esp_wifi_stop();

        if (result != ESP_OK) {
            atomic_store(
                &s_reconnect_allowed,
                reconnect_was_allowed
            );

            wifi_service_unlock();

            return result;
        }

        s_started = false;
    }

    atomic_store(
        &s_sta_connected,
        false
    );

    atomic_store(
        &s_reconnect_allowed,
        false
    );

    result =
        esp_wifi_deinit();

    if (result != ESP_OK) {
        wifi_service_unlock();

        return result;
    }

    wifi_service_cleanup();

    ESP_LOGI(
        TAG,
        "Wi-Fi service deinitialized"
    );

    wifi_service_unlock();

    return ESP_OK;
}

esp_err_t wifi_service_get_started(
    bool *started
)
{
    if (started == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *started = false;

    const esp_err_t result =
        wifi_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!s_initialized) {
        wifi_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    *started = s_started;

    wifi_service_unlock();

    return ESP_OK;
}

esp_err_t wifi_service_get_ap_ssid(
    char *ssid,
    size_t ssid_size
)
{
    if ((ssid == NULL) ||
        (ssid_size == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    ssid[0] = '\0';

    const esp_err_t result =
        wifi_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!s_initialized) {
        wifi_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    const size_t required_size =
        strlen(s_ap_ssid) + 1U;

    if (required_size > ssid_size) {
        wifi_service_unlock();

        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(
        ssid,
        s_ap_ssid,
        required_size
    );

    wifi_service_unlock();

    return ESP_OK;
}

esp_err_t wifi_service_get_ap_ip_address(
    char *address,
    size_t address_size
)
{
    if ((address == NULL) ||
        (address_size == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    address[0] = '\0';

    const esp_err_t result =
        wifi_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!s_initialized) {
        wifi_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    const size_t required_size =
        sizeof(WIFI_SERVICE_AP_IP_ADDRESS);

    if (required_size > address_size) {
        wifi_service_unlock();

        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(
        address,
        WIFI_SERVICE_AP_IP_ADDRESS,
        required_size
    );

    wifi_service_unlock();

    return ESP_OK;
}

esp_err_t wifi_service_get_info(
    wifi_service_info_t *info
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

    const esp_err_t lock_result =
        wifi_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    info->initialized =
        s_initialized;

    info->started =
        s_started;

    info->ap_enabled =
        atomic_load(&s_ap_enabled);

    info->sta_enabled =
        atomic_load(&s_sta_enabled);

    info->sta_connected =
        s_started &&
        atomic_load(&s_sta_connected);

    /*
     * Return configured values even when an interface is disabled.
     */
    (void)strlcpy(
        info->ssid,
        s_ap_ssid,
        sizeof(info->ssid)
    );

    info->password_configured =
        s_ap_password[0] != '\0';

    info->channel =
        WIFI_SERVICE_AP_CHANNEL;

    (void)strlcpy(
        info->ip_address,
        WIFI_SERVICE_AP_IP_ADDRESS,
        sizeof(info->ip_address)
    );

    (void)snprintf(
        info->netmask,
        sizeof(info->netmask),
        "%u.%u.%u.%u",
        WIFI_SERVICE_AP_NETMASK_A,
        WIFI_SERVICE_AP_NETMASK_B,
        WIFI_SERVICE_AP_NETMASK_C,
        WIFI_SERVICE_AP_NETMASK_D
    );

    (void)snprintf(
        info->dhcp_start,
        sizeof(info->dhcp_start),
        "%u.%u.%u.%u",
        WIFI_SERVICE_AP_IP_A,
        WIFI_SERVICE_AP_IP_B,
        WIFI_SERVICE_AP_IP_C,
        WIFI_SERVICE_AP_DHCP_START_D
    );

    (void)snprintf(
        info->dhcp_end,
        sizeof(info->dhcp_end),
        "%u.%u.%u.%u",
        WIFI_SERVICE_AP_IP_A,
        WIFI_SERVICE_AP_IP_B,
        WIFI_SERVICE_AP_IP_C,
        WIFI_SERVICE_AP_DHCP_END_D
    );

    (void)strlcpy(
        info->dns_address,
        WIFI_SERVICE_AP_IP_ADDRESS,
        sizeof(info->dns_address)
    );

    (void)strlcpy(
        info->sta_ssid,
        s_sta_ssid,
        sizeof(info->sta_ssid)
    );

    if (!s_initialized ||
        !s_started) {

        wifi_service_unlock();

        return ESP_OK;
    }

    /*
     * Query SoftAP clients only when the AP interface is enabled.
     */
    if (info->ap_enabled) {
        wifi_sta_list_t station_list = {0};

        const esp_err_t station_result =
            esp_wifi_ap_get_sta_list(
                &station_list
            );

        if (station_result != ESP_OK) {
            wifi_service_unlock();

            return station_result;
        }

        size_t client_count =
            station_list.num;

        if (client_count >
            WIFI_SERVICE_MAX_CLIENT_COUNT) {

            client_count =
                WIFI_SERVICE_MAX_CLIENT_COUNT;
        }

        for (size_t index = 0U;
             index < client_count;
             ++index) {

            memcpy(
                info->clients[index].mac,
                station_list.sta[index].mac,
                sizeof(info->clients[index].mac)
            );

            info->clients[index].rssi =
                station_list.sta[index].rssi;

            info->clients[index]
                .ip_address[0] = '\0';
        }

        info->client_count =
            client_count;
    }

    if (info->sta_connected &&
        (s_sta_netif != NULL)) {

        esp_netif_ip_info_t ip_info = {0};

        esp_err_t result =
            esp_netif_get_ip_info(
                s_sta_netif,
                &ip_info
            );

        if (result != ESP_OK) {
            wifi_service_unlock();

            return result;
        }

        (void)snprintf(
            info->sta_ip_address,
            sizeof(info->sta_ip_address),
            IPSTR,
            IP2STR(&ip_info.ip)
        );

        (void)snprintf(
            info->sta_netmask,
            sizeof(info->sta_netmask),
            IPSTR,
            IP2STR(&ip_info.netmask)
        );

        (void)snprintf(
            info->sta_gateway,
            sizeof(info->sta_gateway),
            IPSTR,
            IP2STR(&ip_info.gw)
        );

        esp_netif_dns_info_t dns_info = {0};

        result =
            esp_netif_get_dns_info(
                s_sta_netif,
                ESP_NETIF_DNS_MAIN,
                &dns_info
            );

        if (result == ESP_OK &&
            (dns_info.ip.type ==
             ESP_IPADDR_TYPE_V4)) {

            (void)snprintf(
                info->sta_dns_address,
                sizeof(info->sta_dns_address),
                IPSTR,
                IP2STR(
                    &dns_info.ip.u_addr.ip4
                )
            );
        }

        wifi_ap_record_t ap_record = {0};

        result =
            esp_wifi_sta_get_ap_info(
                &ap_record
            );

        if (result == ESP_OK) {
            info->sta_rssi =
                ap_record.rssi;

        } else if (result ==
                   ESP_ERR_WIFI_NOT_CONNECT) {

            /*
             * Connection state changed while the snapshot was being
             * collected. Return an internally consistent snapshot.
             */
            info->sta_connected = false;

            info->sta_ip_address[0] = '\0';
            info->sta_netmask[0] = '\0';
            info->sta_gateway[0] = '\0';
            info->sta_dns_address[0] = '\0';

        } else {
            wifi_service_unlock();

            return result;
        }
    }

    wifi_service_unlock();

    return ESP_OK;
}

static esp_err_t wifi_service_set_interface_enabled_locked(
    bool station,
    bool enabled
)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (wifi_service_scan_is_running()) {
        return ESP_ERR_INVALID_STATE;
    }

    atomic_bool *state =
        station
            ? &s_sta_enabled
            : &s_ap_enabled;

    const bool previous_enabled =
        atomic_load(state);

    if (previous_enabled == enabled) {
        return ESP_OK;
    }

    if (station &&
        enabled &&
        (s_sta_ssid[0] == '\0')) {

        return ESP_ERR_INVALID_STATE;
    }

    const bool was_started =
        s_started;

    if (was_started) {
        atomic_store(
            &s_reconnect_allowed,
            false
        );
    }

    /*
     * Update the requested state before calculating the new mode.
     */
    atomic_store(
        state,
        enabled
    );

    const wifi_mode_t new_mode =
        wifi_service_get_mode();

    esp_err_t result = ESP_OK;

    if ((new_mode == WIFI_MODE_NULL) &&
        was_started) {

        result =
            esp_wifi_stop();

        if (result == ESP_OK) {
            s_started = false;

            atomic_store(
                &s_sta_connected,
                false
            );
        }

    } else {
        result =
            wifi_service_apply_mode();

        if (result == ESP_OK &&
            enabled) {

            result =
                station
                    ? wifi_service_configure_sta()
                    : wifi_service_configure_ap();
        }
    }

    if (result != ESP_OK) {
        /*
         * Restore the requested interface state and previous mode.
         */
        atomic_store(
            state,
            previous_enabled
        );

        if (was_started) {
            const esp_err_t restore_result =
                wifi_service_apply_mode();

            if (restore_result != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "Failed to restore previous Wi-Fi mode: %s",
                    esp_err_to_name(restore_result)
                );
            }
        }

        atomic_store(
            &s_reconnect_allowed,
            was_started &&
            atomic_load(&s_sta_enabled)
        );

        return result;
    }

    atomic_store(
        &s_reconnect_allowed,
        s_started &&
        atomic_load(&s_sta_enabled)
    );

    if (station &&
        !enabled) {

        atomic_store(
            &s_sta_connected,
            false
        );
    }

    /*
     * WIFI_EVENT_STA_START initiates the Station connection after the
     * driver starts. Do not call esp_wifi_connect() here as well because
     * that would race with the event handler.
     */

    ESP_LOGI(
        TAG,
        "%s interface %s",
        station ? "Station" : "SoftAP",
        enabled ? "enabled" : "disabled"
    );

    return result;
}

esp_err_t wifi_service_set_ap_enabled(
    bool enabled
)
{
    const esp_err_t lock_result =
        wifi_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    const esp_err_t result =
        wifi_service_set_interface_enabled_locked(
            false,
            enabled
        );

    wifi_service_unlock();

    return result;
}

esp_err_t wifi_service_set_sta_enabled(
    bool enabled
)
{
    const esp_err_t lock_result =
        wifi_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    const esp_err_t result =
        wifi_service_set_interface_enabled_locked(
            true,
            enabled
        );

    wifi_service_unlock();

    return result;
}

esp_err_t wifi_service_set_ap_credentials(
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
        (ssid_length >
        WIFI_SERVICE_AP_SSID_BASE_MAX_LENGTH)) {

        return ESP_ERR_INVALID_SIZE;
    }

    if (password_length >=
        WIFI_SERVICE_AP_PASSWORD_MAX_LENGTH) {

        return ESP_ERR_INVALID_SIZE;
    }

    if ((password_length != 0U) &&
        (password_length <
         WIFI_SERVICE_AP_PASSWORD_MIN_LENGTH)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (s_state_mutex == NULL) {
        s_state_mutex =
            xSemaphoreCreateMutex();

        if (s_state_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t result =
        wifi_service_lock();

    if (result != ESP_OK) {
        return result;
    }
    
    if (s_initialized &&
        wifi_service_scan_is_running()) {

        wifi_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    if ((strcmp(s_ap_ssid_base, ssid) == 0) &&
        (strcmp(s_ap_password, password) == 0)) {

        if (s_ap_ssid[0] == '\0') {
            result =
                wifi_service_build_ssid();

            if (result != ESP_OK) {
                wifi_service_unlock();

                return result;
            }
        }

        wifi_service_unlock();

        return ESP_OK;
    }

    char previous_ssid_base[
        WIFI_SERVICE_AP_SSID_MAX_LENGTH
    ];

    char previous_ssid[
        WIFI_SERVICE_AP_SSID_MAX_LENGTH
    ];

    char previous_password[
        WIFI_SERVICE_AP_PASSWORD_MAX_LENGTH
    ];

    (void)strlcpy(
        previous_ssid_base,
        s_ap_ssid_base,
        sizeof(previous_ssid_base)
    );

    (void)strlcpy(
        previous_ssid,
        s_ap_ssid,
        sizeof(previous_ssid)
    );

    (void)strlcpy(
        previous_password,
        s_ap_password,
        sizeof(previous_password)
    );

    (void)strlcpy(
        s_ap_ssid_base,
        ssid,
        sizeof(s_ap_ssid_base)
    );

    (void)strlcpy(
        s_ap_password,
        password,
        sizeof(s_ap_password)
    );

    result =
        wifi_service_build_ssid();

    if (result != ESP_OK) {
        (void)strlcpy(
            s_ap_ssid_base,
            previous_ssid_base,
            sizeof(s_ap_ssid_base)
        );

        (void)strlcpy(
            s_ap_ssid,
            previous_ssid,
            sizeof(s_ap_ssid)
        );

        (void)strlcpy(
            s_ap_password,
            previous_password,
            sizeof(s_ap_password)
        );

        wifi_service_unlock();

        return result;
    }

    /*
     * Before initialization, only store the configuration. It will be
     * applied by wifi_service_init().
     */
    if (!s_initialized ||
        !atomic_load(&s_ap_enabled)) {

        /*
         * Store the credentials. They will be applied when SoftAP is
         * initialized or enabled.
         */
        wifi_service_unlock();

        return ESP_OK;
    }

    result =
        wifi_service_configure_ap();

    if (result != ESP_OK) {
        (void)strlcpy(
            s_ap_ssid_base,
            previous_ssid_base,
            sizeof(s_ap_ssid_base)
        );

        (void)strlcpy(
            s_ap_ssid,
            previous_ssid,
            sizeof(s_ap_ssid)
        );

        (void)strlcpy(
            s_ap_password,
            previous_password,
            sizeof(s_ap_password)
        );

        const esp_err_t restore_result =
            wifi_service_configure_ap();

        if (restore_result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Failed to restore previous SoftAP configuration: %s",
                esp_err_to_name(restore_result)
            );
        }

        wifi_service_unlock();

        return result;
    }

    ESP_LOGI(
        TAG,
        "SoftAP credentials updated: SSID=%s",
        s_ap_ssid
    );

    wifi_service_unlock();

    return ESP_OK;
}

esp_err_t wifi_service_set_sta_credentials(
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
         WIFI_SERVICE_STA_SSID_MAX_LENGTH)) {

        return ESP_ERR_INVALID_SIZE;
    }

    if (password_length >=
        WIFI_SERVICE_STA_PASSWORD_MAX_LENGTH) {

        return ESP_ERR_INVALID_SIZE;
    }

    if ((password_length != 0U) &&
        (password_length <
         WIFI_SERVICE_STA_PASSWORD_MIN_LENGTH)) {

        return ESP_ERR_INVALID_ARG;
    }

    /*
     * Credentials may be configured before service initialization.
     */
    if (s_state_mutex == NULL) {
        s_state_mutex =
            xSemaphoreCreateMutex();

        if (s_state_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t result =
        wifi_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (s_initialized &&
        wifi_service_scan_is_running()) {

        wifi_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    if ((strcmp(s_sta_ssid, ssid) == 0) &&
        (strcmp(s_sta_password, password) == 0)) {

        wifi_service_unlock();

        return ESP_OK;
    }

    char previous_ssid[
        WIFI_SERVICE_STA_SSID_MAX_LENGTH
    ];

    char previous_password[
        WIFI_SERVICE_STA_PASSWORD_MAX_LENGTH
    ];

    (void)strlcpy(
        previous_ssid,
        s_sta_ssid,
        sizeof(previous_ssid)
    );

    (void)strlcpy(
        previous_password,
        s_sta_password,
        sizeof(previous_password)
    );

    (void)strlcpy(
        s_sta_ssid,
        ssid,
        sizeof(s_sta_ssid)
    );

    (void)strlcpy(
        s_sta_password,
        password,
        sizeof(s_sta_password)
    );

    /*
     * Store credentials without touching the driver when the service
     * or Station interface is inactive.
     */
    if (!s_initialized ||
        !atomic_load(&s_sta_enabled)) {

        wifi_service_unlock();

        return ESP_OK;
    }

    const bool reconnect_was_allowed =
        atomic_load(&s_reconnect_allowed);

    atomic_store(
        &s_reconnect_allowed,
        false
    );

    if (s_started) {
        result =
            esp_wifi_disconnect();

        if ((result != ESP_OK) &&
            (result !=
             ESP_ERR_WIFI_NOT_CONNECT)) {

            goto restore;
        }

        atomic_store(
            &s_sta_connected,
            false
        );
    }

    result =
        wifi_service_configure_sta();

    if (result != ESP_OK) {
        goto restore;
    }

    atomic_store(
        &s_reconnect_allowed,
        reconnect_was_allowed
    );

    if (s_started) {
        result =
            esp_wifi_connect();

        if (result != ESP_OK) {
            goto restore;
        }
    }

    ESP_LOGI(
        TAG,
        "Station credentials updated: SSID=%s",
        s_sta_ssid
    );

    wifi_service_unlock();

    return ESP_OK;

restore:
    {
        const esp_err_t original_result =
            result;

        (void)strlcpy(
            s_sta_ssid,
            previous_ssid,
            sizeof(s_sta_ssid)
        );

        (void)strlcpy(
            s_sta_password,
            previous_password,
            sizeof(s_sta_password)
        );

        const esp_err_t restore_result =
            wifi_service_configure_sta();

        if (restore_result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Failed to restore previous Station configuration: %s",
                esp_err_to_name(restore_result)
            );
        }

        atomic_store(
            &s_reconnect_allowed,
            reconnect_was_allowed
        );

        if (s_started &&
            reconnect_was_allowed &&
            (restore_result == ESP_OK)) {

            const esp_err_t connect_result =
                esp_wifi_connect();

            if (connect_result != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "Failed to reconnect using previous Station "
                    "configuration: %s",
                    esp_err_to_name(connect_result)
                );
            }
        }

        wifi_service_unlock();

        return original_result;
    }
}

esp_err_t wifi_service_start_scan(void)
{
    esp_err_t result =
        wifi_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!s_initialized ||
        !s_started) {

        wifi_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    result =
        wifi_service_scan_lock();

    if (result != ESP_OK) {
        wifi_service_unlock();

        return result;
    }

    if (s_scan_info.state ==
        WIFI_SERVICE_SCAN_STATE_RUNNING) {

        wifi_service_scan_unlock();
        wifi_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    memset(
        s_scan_results,
        0,
        sizeof(s_scan_results)
    );

    s_scan_info.state =
        WIFI_SERVICE_SCAN_STATE_RUNNING;

    s_scan_info.result_count = 0U;
    s_scan_info.truncated = false;
    s_scan_info.last_error = ESP_OK;

    s_scan_temporary_sta = false;

    const wifi_mode_t requested_mode =
        wifi_service_get_mode();

    if (requested_mode == WIFI_MODE_AP) {
        result =
            esp_wifi_set_mode(
                WIFI_MODE_APSTA
            );

        if (result != ESP_OK) {
            s_scan_info.state =
                WIFI_SERVICE_SCAN_STATE_ERROR;

            s_scan_info.last_error = result;

            wifi_service_scan_unlock();
            wifi_service_unlock();

            return result;
        }

        s_scan_temporary_sta = true;
    }

    const wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0U,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };

    result =
        esp_wifi_scan_start(
            &scan_config,
            false
        );

    if (result != ESP_OK) {
        if (s_scan_temporary_sta) {
            const esp_err_t restore_result =
                esp_wifi_set_mode(
                    WIFI_MODE_AP
                );

            if (restore_result != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "Failed to restore AP mode: %s",
                    esp_err_to_name(restore_result)
                );
            }

            s_scan_temporary_sta = false;
        }

        s_scan_info.state =
            WIFI_SERVICE_SCAN_STATE_ERROR;

        s_scan_info.last_error = result;

        wifi_service_scan_unlock();
        wifi_service_unlock();

        return result;
    }

    wifi_service_scan_unlock();
    wifi_service_unlock();

    ESP_LOGI(
        TAG,
        "Asynchronous Wi-Fi scan started"
    );

    return ESP_OK;
}

esp_err_t wifi_service_get_scan_info(
    wifi_service_scan_info_t *info
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

    esp_err_t result =
        wifi_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!s_initialized) {
        wifi_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    result =
        wifi_service_scan_lock();

    if (result == ESP_OK) {
        *info = s_scan_info;

        wifi_service_scan_unlock();
    }

    wifi_service_unlock();

    return result;
}

esp_err_t wifi_service_get_scan_results(
    wifi_service_scan_result_t *results,
    size_t capacity,
    size_t *out_count
)
{
    if ((results == NULL) ||
        (out_count == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *out_count = 0U;

    if (capacity == 0U) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t result =
        wifi_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!s_initialized) {
        wifi_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    result =
        wifi_service_scan_lock();

    if (result != ESP_OK) {
        wifi_service_unlock();

        return result;
    }

    if (s_scan_info.state !=
        WIFI_SERVICE_SCAN_STATE_COMPLETE) {

        wifi_service_scan_unlock();
        wifi_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    size_t count =
        s_scan_info.result_count;

    if (count > capacity) {
        count = capacity;
    }

    memcpy(
        results,
        s_scan_results,
        count * sizeof(*results)
    );

    *out_count = count;

    wifi_service_scan_unlock();
    wifi_service_unlock();

    return ESP_OK;
}

esp_err_t wifi_service_clear_scan_results(void)
{
    esp_err_t result =
        wifi_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!s_initialized) {
        wifi_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    result =
        wifi_service_scan_lock();

    if (result != ESP_OK) {
        wifi_service_unlock();

        return result;
    }

    memset(
        s_scan_results,
        0,
        sizeof(s_scan_results)
    );

    s_scan_info.result_count = 0U;
    s_scan_info.truncated = false;

    if (s_scan_info.state !=
        WIFI_SERVICE_SCAN_STATE_RUNNING) {

        s_scan_info.state =
            WIFI_SERVICE_SCAN_STATE_IDLE;

        s_scan_info.last_error = ESP_OK;
    }

    wifi_service_scan_unlock();
    wifi_service_unlock();

    return ESP_OK;
}
