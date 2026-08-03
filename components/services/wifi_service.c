#include "wifi_service.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"

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
#define WIFI_SERVICE_AP_MAX_CLIENTS  (2U)

#define WIFI_SERVICE_AP_PASSWORD     "spectra123"

#define WIFI_SERVICE_AP_DHCP_START_D  (2U)
#define WIFI_SERVICE_AP_DHCP_END_D    (10U)

static const char *TAG = "wifi_service";

static SemaphoreHandle_t
    s_state_mutex = NULL;

static SemaphoreHandle_t
    s_ap_started_semaphore = NULL;

static esp_netif_t *s_ap_netif = NULL;

static esp_event_handler_instance_t
    s_wifi_event_instance = NULL;

static bool s_initialized = false;
static bool s_started = false;

static char s_ap_ssid[
    WIFI_SERVICE_AP_SSID_MAX_LENGTH
];

static esp_err_t wifi_service_configure_dns(
    esp_netif_t *netif,
    const esp_netif_ip_info_t *ip_info
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

static void wifi_service_event_handler(
    void *argument,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
)
{
    (void)argument;
    (void)event_base;

    if (event_id == WIFI_EVENT_AP_START) {
        if (s_ap_started_semaphore != NULL) {
            (void)xSemaphoreGive(
                s_ap_started_semaphore
            );
        }

    } else if (event_id ==
            WIFI_EVENT_AP_STACONNECTED) {

        const wifi_event_ap_staconnected_t *event =
            event_data;

        if (event != NULL) {
            ESP_LOGI(
                TAG,
                "Station connected, AID=%d",
                event->aid
            );
        }

    } else if (event_id ==
               WIFI_EVENT_AP_STADISCONNECTED) {

        const wifi_event_ap_stadisconnected_t *event =
            event_data;

        if (event != NULL) {
            ESP_LOGI(
                TAG,
                "Station disconnected, AID=%d",
                event->aid
            );
        }
    }
}

static esp_err_t wifi_service_build_ssid(void)
{
    uint8_t mac[6];

    const esp_err_t result =
        esp_read_mac(
            mac,
            ESP_MAC_WIFI_SOFTAP
        );

    if (result != ESP_OK) {
        return result;
    }

    const int written =
        snprintf(
            s_ap_ssid,
            sizeof(s_ap_ssid),
            "Spectra-%02X%02X%02X",
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

    return esp_netif_dhcps_start(
        s_ap_netif
    );
}

static esp_err_t wifi_service_configure_ap(void)
{
    wifi_config_t config = {0};

    const size_t ssid_length =
        strlen(s_ap_ssid);

    const size_t password_length =
        strlen(
            WIFI_SERVICE_AP_PASSWORD
        );

    if ((ssid_length == 0U) ||
        (ssid_length >
         sizeof(config.ap.ssid)) ||
        (password_length < 8U) ||
        (password_length >=
         sizeof(config.ap.password))) {

        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(
        config.ap.ssid,
        s_ap_ssid,
        ssid_length
    );

    config.ap.ssid_len =
        (uint8_t)ssid_length;

    memcpy(
        config.ap.password,
        WIFI_SERVICE_AP_PASSWORD,
        password_length
    );

    config.ap.channel =
        WIFI_SERVICE_AP_CHANNEL;

    config.ap.max_connection =
        WIFI_SERVICE_AP_MAX_CLIENTS;

    config.ap.authmode =
        WIFI_AUTH_WPA2_PSK;

    config.ap.pmf_cfg.capable = true;
    config.ap.pmf_cfg.required = false;

    esp_err_t result =
        esp_wifi_set_mode(
            WIFI_MODE_AP
        );

    if (result != ESP_OK) {
        return result;
    }

    return esp_wifi_set_config(
        WIFI_IF_AP,
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
    if (s_wifi_event_instance != NULL) {
        (void)esp_event_handler_instance_unregister(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            s_wifi_event_instance
        );

        s_wifi_event_instance = NULL;
    }

    if (s_ap_started_semaphore != NULL) {
        vSemaphoreDelete(
            s_ap_started_semaphore
        );

        s_ap_started_semaphore = NULL;
    }

    if (s_ap_netif != NULL) {
        esp_netif_destroy_default_wifi(
            s_ap_netif
        );

        s_ap_netif = NULL;
    }

    s_initialized = false;
    s_started = false;
    s_ap_ssid[0] = '\0';
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

    esp_err_t result =
        wifi_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (s_initialized) {
        wifi_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

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

    s_ap_started_semaphore =
        xSemaphoreCreateBinary();

    if (s_ap_started_semaphore == NULL) {
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

    result = wifi_service_configure_ip();

    if (result != ESP_OK) {
        (void)esp_wifi_deinit();
        wifi_service_cleanup();
        wifi_service_unlock();

        return result;
    }

    result = wifi_service_configure_ap();

    if (result != ESP_OK) {
        (void)esp_wifi_deinit();
        wifi_service_cleanup();
        wifi_service_unlock();

        return result;
    }

    s_initialized = true;

    ESP_LOGI(
        TAG,
        "Wi-Fi SoftAP initialized"
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
        (s_ap_started_semaphore == NULL)) {

        wifi_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Remove a stale notification left by an earlier start attempt.
     */
    while (xSemaphoreTake(
               s_ap_started_semaphore,
               0U
           ) == pdTRUE) {
    }

    result = esp_wifi_start();

    if (result != ESP_OK) {
        wifi_service_unlock();

        return result;
    }

    if (xSemaphoreTake(
            s_ap_started_semaphore,
            pdMS_TO_TICKS(
                WIFI_SERVICE_START_TIMEOUT_MS
            )
        ) != pdTRUE) {

        ESP_LOGE(
            TAG,
            "Timed out waiting for Wi-Fi SoftAP startup"
        );

        (void)esp_wifi_stop();

        wifi_service_unlock();

        return ESP_ERR_TIMEOUT;
    }

    esp_netif_dhcp_status_t dhcp_status =
        ESP_NETIF_DHCP_INIT;

    result = esp_netif_dhcps_get_status(
        s_ap_netif,
        &dhcp_status
    );

    if (result != ESP_OK) {
        (void)esp_wifi_stop();

        wifi_service_unlock();

        return result;
    }

    if (dhcp_status !=
        ESP_NETIF_DHCP_STARTED) {

        ESP_LOGE(
            TAG,
            "Wi-Fi DHCP server did not start"
        );

        (void)esp_wifi_stop();

        wifi_service_unlock();

        return ESP_FAIL;
    }

    s_started = true;

    ESP_LOGI(
        TAG,
        "SoftAP started: SSID=%s, IP=%s",
        s_ap_ssid,
        WIFI_SERVICE_AP_IP_ADDRESS
    );

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

    result = esp_wifi_stop();

    if (result != ESP_OK) {
        wifi_service_unlock();

        return result;
    }

    s_started = false;

    ESP_LOGI(
        TAG,
        "Wi-Fi SoftAP stopped"
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

    if (s_started) {
        result = esp_wifi_stop();

        if (result != ESP_OK) {
            wifi_service_unlock();

            return result;
        }

        s_started = false;
    }

    result = esp_wifi_deinit();

    if (result != ESP_OK) {
        wifi_service_unlock();

        return result;
    }

    wifi_service_cleanup();

    ESP_LOGI(
        TAG,
        "Wi-Fi SoftAP deinitialized"
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
