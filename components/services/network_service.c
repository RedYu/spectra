#include "network_service.h"

#include <stdatomic.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "dns_server.h"
#include "mdns_service.h"

#define NETWORK_SERVICE_DNS_NAME  ("spectra.device")

#define NETWORK_SERVICE_COMMAND_QUEUE_LENGTH  (1U)

#define NETWORK_SERVICE_TASK_STACK_SIZE       (3072U)
#define NETWORK_SERVICE_TASK_PRIORITY         (4U)

#define NETWORK_SERVICE_MDNS_REFRESH_DELAY_MS (250U)

static const char *TAG = "network_service";

typedef enum
{
    NETWORK_SERVICE_COMMAND_REFRESH_MDNS = 0,

} network_service_command_t;

static QueueHandle_t s_command_queue = NULL;
static TaskHandle_t s_maintenance_task = NULL;

static atomic_bool s_maintenance_enabled =
    ATOMIC_VAR_INIT(false);

static SemaphoreHandle_t s_maintenance_mutex = NULL;

static bool s_netif_initialized = false;
static bool s_event_loop_initialized = false;

static dns_server_handle_t s_dns_server = NULL;

static esp_err_t network_service_start_dns(void);

static esp_err_t network_service_start_maintenance(void);

static void network_service_maintenance_task(
    void *argument
);

esp_err_t network_service_init(void)
{
    if (s_netif_initialized &&
        s_event_loop_initialized) {

        esp_err_t result =
            network_service_start_dns();

        if (result != ESP_OK) {
            return result;
        }

        return network_service_start_maintenance();
    }

    if (!s_netif_initialized) {
        const esp_err_t result =
            esp_netif_init();

        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Failed to initialize ESP-NETIF: %s",
                esp_err_to_name(result)
            );

            return result;
        }

        s_netif_initialized = true;
    }

    if (!s_event_loop_initialized) {
        const esp_err_t result =
            esp_event_loop_create_default();

        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Failed to create default event loop: %s",
                esp_err_to_name(result)
            );

            /*
             * ESP-NETIF deinitialization is not currently supported.
             * Keep its initialized state and allow a later call to
             * retry default event-loop creation.
             */
            return result;
        }

        s_event_loop_initialized = true;
    }

    const esp_err_t dns_result =
        network_service_start_dns();

    if (dns_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to start DNS server: %s",
            esp_err_to_name(dns_result)
        );

        return dns_result;
    }

    const esp_err_t maintenance_result =
        network_service_start_maintenance();

    if (maintenance_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to start network maintenance: %s",
            esp_err_to_name(maintenance_result)
        );

        return maintenance_result;
    }

    ESP_LOGI(
        TAG,
        "Global network stack and DNS server initialized"
    );

    return ESP_OK;
}

esp_err_t network_service_get_initialized(
    bool *initialized
)
{
    if (initialized == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *initialized =
        s_netif_initialized &&
        s_event_loop_initialized &&
        (s_dns_server != NULL) &&
        (s_command_queue != NULL) &&
        (s_maintenance_task != NULL) &&
        (s_maintenance_mutex != NULL) &&
        atomic_load(
            &s_maintenance_enabled
        );

    return ESP_OK;
}

static void network_service_maintenance_task(
    void *argument
)
{
    (void)argument;

    ESP_LOGI(
        TAG,
        "Network maintenance task started"
    );

    for (;;) {
        network_service_command_t command;

        if (xQueueReceive(
                s_command_queue,
                &command,
                portMAX_DELAY
            ) != pdTRUE) {

            continue;
        }

        switch (command) {
            case NETWORK_SERVICE_COMMAND_REFRESH_MDNS: {
                /*
                 * Allow ESP-NETIF and the Wi-Fi interface to settle
                 * after a mode transition before recreating mDNS.
                 */
                vTaskDelay(
                    pdMS_TO_TICKS(
                        NETWORK_SERVICE_MDNS_REFRESH_DELAY_MS
                    )
                );

                if (xSemaphoreTake(
                        s_maintenance_mutex,
                        portMAX_DELAY
                    ) != pdTRUE) {

                    break;
                }

                if (atomic_load(
                        &s_maintenance_enabled
                    )) {

                    const esp_err_t result =
                        mdns_service_refresh();

                    if (result != ESP_OK) {
                        ESP_LOGW(
                            TAG,
                            "Failed to refresh mDNS: %s",
                            esp_err_to_name(result)
                        );
                    }
                }

                (void)xSemaphoreGive(
                    s_maintenance_mutex
                );

                break;
            }

            default:
                ESP_LOGW(
                    TAG,
                    "Unknown network maintenance command: %d",
                    (int)command
                );

                break;
        }
    }
}

static esp_err_t network_service_start_maintenance(void)
{
    if ((s_command_queue != NULL) &&
        (s_maintenance_task != NULL) &&
        (s_maintenance_mutex != NULL)) {

        atomic_store(
            &s_maintenance_enabled,
            true
        );

        return ESP_OK;
    }

    if ((s_command_queue != NULL) ||
        (s_maintenance_task != NULL) ||
        (s_maintenance_mutex != NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    s_maintenance_mutex =
        xSemaphoreCreateMutex();

    if (s_maintenance_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_command_queue =
        xQueueCreate(
            NETWORK_SERVICE_COMMAND_QUEUE_LENGTH,
            sizeof(network_service_command_t)
        );

    if (s_command_queue == NULL) {
        vSemaphoreDelete(
            s_maintenance_mutex
        );

        s_maintenance_mutex = NULL;

        return ESP_ERR_NO_MEM;
    }

    const BaseType_t task_result =
        xTaskCreate(
            network_service_maintenance_task,
            "network_maintenance",
            NETWORK_SERVICE_TASK_STACK_SIZE,
            NULL,
            NETWORK_SERVICE_TASK_PRIORITY,
            &s_maintenance_task
        );

    if (task_result != pdPASS) {
        vQueueDelete(
            s_command_queue
        );

        vSemaphoreDelete(
            s_maintenance_mutex
        );

        s_command_queue = NULL;
        s_maintenance_mutex = NULL;
        s_maintenance_task = NULL;

        return ESP_ERR_NO_MEM;
    }

    atomic_store(
        &s_maintenance_enabled,
        true
    );

    return ESP_OK;
}

static esp_err_t network_service_start_dns(void)
{
    if (s_dns_server != NULL) {
        return ESP_OK;
    }

    const dns_server_config_t config = {
        .num_of_entries = 2U,

        .item = {
            {
                .name = NETWORK_SERVICE_DNS_NAME,
                .if_key = USB_NETWORK_INTERFACE_KEY,

                .source_network = {
                    .addr = ESP_IP4TOADDR(
                        172U,
                        16U,
                        10U,
                        0U
                    ),
                },

                .source_netmask = {
                    .addr = ESP_IP4TOADDR(
                        255U,
                        255U,
                        255U,
                        0U
                    ),
                },
            },
            {
                .name = NETWORK_SERVICE_DNS_NAME,

                /*
                 * Use the actual if_key assigned to the SoftAP
                 * esp_netif instance.
                 */
                .if_key = WIFI_AP_INTERFACE_KEY,

                .source_network = {
                    .addr = ESP_IP4TOADDR(
                        172U,
                        16U,
                        20U,
                        0U
                    ),
                },

                .source_netmask = {
                    .addr = ESP_IP4TOADDR(
                        255U,
                        255U,
                        255U,
                        0U
                    ),
                },
            },
        },
    };

    s_dns_server =
        start_dns_server(
            &config
        );

    if (s_dns_server == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create DNS server"
        );

        return ESP_FAIL;
    }

    ESP_LOGI(
        TAG,
        "DNS server started for USB and Wi-Fi networks"
    );

    return ESP_OK;
}

esp_err_t network_service_get_dns_info(
    network_service_dns_info_t *info
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

    info->started =
        (s_dns_server != NULL) &&
        dns_server_get_started(
            s_dns_server
        );

    (void)strlcpy(
        info->local_name,
        NETWORK_SERVICE_DNS_NAME,
        sizeof(info->local_name)
    );

    return ESP_OK;
}

esp_err_t network_service_request_mdns_refresh(void)
{
    if (!s_netif_initialized ||
        !s_event_loop_initialized ||
        (s_dns_server == NULL) ||
        (s_command_queue == NULL) ||
        (s_maintenance_task == NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    if (!atomic_load(
            &s_maintenance_enabled
        )) {

        return ESP_ERR_INVALID_STATE;
    }

    const network_service_command_t command =
        NETWORK_SERVICE_COMMAND_REFRESH_MDNS;

    if (xQueueSend(
            s_command_queue,
            &command,
            0U
        ) != pdTRUE) {

        /*
         * A refresh is already pending. It will cover the latest
         * interface transition as well, so treat it as accepted.
         */
        ESP_LOGD(
            TAG,
            "mDNS refresh is already pending"
        );

        return ESP_OK;
    }

    return ESP_OK;
}

esp_err_t network_service_prepare_shutdown(void)
{
    if ((s_command_queue == NULL) ||
        (s_maintenance_task == NULL) ||
        (s_maintenance_mutex == NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Prevent a worker that has not entered the protected section
     * from starting another mDNS refresh.
     */
    atomic_store(
        &s_maintenance_enabled,
        false
    );

    (void)xQueueReset(
        s_command_queue
    );

    /*
     * Wait for an already running mDNS refresh to complete.
     */
    if (xSemaphoreTake(
            s_maintenance_mutex,
            portMAX_DELAY
        ) != pdTRUE) {

        return ESP_FAIL;
    }

    (void)xSemaphoreGive(
        s_maintenance_mutex
    );

    ESP_LOGI(
        TAG,
        "Network maintenance disabled"
    );

    return ESP_OK;
}
