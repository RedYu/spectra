#include "usb_network_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_netif_defaults.h"
#include "freertos/FreeRTOS.h"

#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "dhcpserver/dhcpserver.h"
#include "tinyusb_net.h"
#include "network_service.h"
#include "dns_server.h"

#define USB_RNDIS_VENDOR_ID            (TINYUSB_ESPRESSIF_VID)
#define USB_RNDIS_PRODUCT_ID           (0x4000U)
#define USB_RNDIS_DEVICE_VERSION       (0x0100U)

#define USB_RNDIS_INTERFACE_COUNT      (2U)
#define USB_RNDIS_INTERFACE_NUMBER     (0U)

#define USB_RNDIS_STRING_INTERFACE     (4U)
#define USB_RNDIS_STRING_MAC           (5U)

#define USB_RNDIS_ENDPOINT_NOTIFY      (0x81U)
#define USB_RNDIS_ENDPOINT_OUT         (0x02U)
#define USB_RNDIS_ENDPOINT_IN          (0x82U)

#define USB_RNDIS_ENDPOINT_SIZE        (64U)
#define USB_RNDIS_NOTIFY_SIZE          (8U)

#define USB_RNDIS_CONFIG_TOTAL_LENGTH  \
    (TUD_CONFIG_DESC_LEN + TUD_RNDIS_DESC_LEN)

#define USB_MS_OS_STRING_INDEX           (0xEEU)
#define USB_MS_OS_VENDOR_CODE            (0x20U)
#define USB_MS_OS_EXT_COMPAT_ID_INDEX    (0x0004U)

#define USB_RNDIS_CONTROL_INTERFACE      (0U)

#define USB_NETWORK_IP_A                  (172U)
#define USB_NETWORK_IP_B                  (16U)
#define USB_NETWORK_IP_C                  (10U)
#define USB_NETWORK_IP_D                  (1U)

#define USB_NETWORK_DHCP_START_D          (2U)
#define USB_NETWORK_DHCP_END_D            (10U)

#define USB_NETWORK_TX_TIMEOUT_MS         (1000U)

static esp_netif_t *s_usb_netif = NULL;
static dns_server_handle_t s_dns_server = NULL;

/*
 * esp_netif requires a non-null driver handle.
 * The current USB driver callbacks do not need additional context.
 */
static uint8_t s_netif_driver_context = 0U;

static const char *TAG = "usb_network_service";

static bool s_initialized = false;

/*
 * Microsoft OS 1.0 Extended Compatible ID descriptor.
 *
 * This descriptor binds RNDIS interface 0 to the Windows
 * in-box RNDIS driver.
 */
static const uint8_t s_ms_os_compatible_id_descriptor[] = {
    /*
     * Header:
     * dwLength = 40 bytes.
     */
    0x28U, 0x00U, 0x00U, 0x00U,

    /*
     * bcdVersion = 1.00.
     */
    0x00U, 0x01U,

    /*
     * wIndex = Extended Compatible ID descriptor.
     */
    0x04U, 0x00U,

    /*
     * bCount = one function section.
     */
    0x01U,

    /*
     * Reserved.
     */
    0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U,

    /*
     * Function section:
     * bFirstInterfaceNumber.
     */
    USB_RNDIS_CONTROL_INTERFACE,

    /*
     * Reserved.
     */
    0x01U,

    /*
     * Compatible ID: "RNDIS" followed by zero padding.
     */
    'R', 'N', 'D', 'I', 'S',
    0x00U, 0x00U, 0x00U,

    /*
     * Sub-compatible ID: empty.
     */
    0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U,

    /*
     * Reserved.
     */
    0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U,
};

_Static_assert(
    sizeof(s_ms_os_compatible_id_descriptor) == 40U,
    "Invalid Microsoft OS 1.0 compatible ID descriptor size"
);

/*
 * Required by the TinyUSB ECM/RNDIS implementation.
 *
 * It must have external linkage and must not be static or const.
 */
uint8_t tud_network_mac_address[6] = {0};

static const tusb_desc_device_t s_device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200U,

    /*
     * RNDIS contains an Interface Association Descriptor.
     */
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,

    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = USB_RNDIS_VENDOR_ID,
    .idProduct = USB_RNDIS_PRODUCT_ID,
    .bcdDevice = USB_RNDIS_DEVICE_VERSION,

    .iManufacturer = 1U,
    .iProduct = 2U,
    .iSerialNumber = 3U,

    .bNumConfigurations = 1U,
};

static const uint8_t s_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(
        1,
        USB_RNDIS_INTERFACE_COUNT,
        0,
        USB_RNDIS_CONFIG_TOTAL_LENGTH,
        0,
        100
    ),

    TUD_RNDIS_DESCRIPTOR(
        USB_RNDIS_INTERFACE_NUMBER,
        USB_RNDIS_STRING_INTERFACE,
        USB_RNDIS_ENDPOINT_NOTIFY,
        USB_RNDIS_NOTIFY_SIZE,
        USB_RNDIS_ENDPOINT_OUT,
        USB_RNDIS_ENDPOINT_IN,
        USB_RNDIS_ENDPOINT_SIZE
    ),
};

static const char s_language_descriptor[] = {
    0x09,
    0x04
};

static const char *s_string_descriptors[] = {
    s_language_descriptor,
    "Spectra",
    "Spectra USB RNDIS",
    "SPC-00000001",
    "Spectra RNDIS Network",
    "",
};

static esp_err_t usb_network_dns_start(void)
{
    if (s_dns_server != NULL) {
        return ESP_OK;
    }

    /*
     * Resolve spectra.device directly to the USB network address.
     * A static address avoids dependency on the esp_netif interface key.
     */
    dns_server_config_t config = {
        .num_of_entries = 1,

        .item = {
            {
                .name = "spectra.device",
                .if_key = NULL,

                .ip = {
                    .addr = ESP_IP4TOADDR(
                        USB_NETWORK_IP_A,
                        USB_NETWORK_IP_B,
                        USB_NETWORK_IP_C,
                        USB_NETWORK_IP_D
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
            "Failed to start USB DNS server"
        );

        return ESP_FAIL;
    }

    ESP_LOGI(
        TAG,
        "DNS server started: spectra.device -> 172.16.10.1"
    );

    return ESP_OK;
}

static esp_err_t usb_network_initialize_mac(void)
{
    uint8_t base_mac[6] = {0};

    ESP_RETURN_ON_ERROR(
        esp_read_mac(
            base_mac,
            ESP_MAC_WIFI_STA
        ),
        TAG,
        "Failed to read base MAC address"
    );

    ESP_RETURN_ON_ERROR(
        esp_derive_local_mac(
            tud_network_mac_address,
            base_mac
        ),
        TAG,
        "Failed to derive USB network MAC address"
    );

    ESP_LOGI(
        TAG,
        "USB network MAC: "
        "%02X:%02X:%02X:%02X:%02X:%02X",
        tud_network_mac_address[0],
        tud_network_mac_address[1],
        tud_network_mac_address[2],
        tud_network_mac_address[3],
        tud_network_mac_address[4],
        tud_network_mac_address[5]
    );

    return ESP_OK;
}

static esp_err_t usb_network_transmit(
    void *handle,
    void *buffer,
    size_t length
)
{
    (void)handle;

    if ((buffer == NULL) ||
        (length == 0U) ||
        (length > UINT16_MAX)) {

        return ESP_ERR_INVALID_ARG;
    }

    return tinyusb_net_send_sync(
        buffer,
        (uint16_t)length,
        NULL,
        pdMS_TO_TICKS(
            USB_NETWORK_TX_TIMEOUT_MS
        )
    );
}

static void usb_network_free_rx_buffer(
    void *handle,
    void *buffer
)
{
    (void)handle;

    free(buffer);
}

static esp_err_t usb_network_receive_callback(
    void *buffer,
    uint16_t length,
    void *context
)
{
    (void)context;

    if ((buffer == NULL) ||
        (length == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (s_usb_netif == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * TinyUSB reuses its receive buffer after this callback returns.
     * Copy the Ethernet frame into internal memory before passing it
     * to esp_netif and lwIP.
     */
    void *packet =
        heap_caps_malloc(
            length,
            MALLOC_CAP_INTERNAL |
            MALLOC_CAP_8BIT
        );

    if (packet == NULL) {
        ESP_LOGW(
            TAG,
            "Failed to allocate %u bytes for USB RX packet",
            (unsigned int)length
        );

        return ESP_ERR_NO_MEM;
    }

    memcpy(
        packet,
        buffer,
        length
    );

    /*
     * esp_netif takes ownership of the packet and releases it through
     * usb_network_free_rx_buffer(), including receive error paths.
     */
    return esp_netif_receive(
        s_usb_netif,
        packet,
        length,
        NULL
    );
}

static esp_err_t usb_network_netif_init(void)
{
    if (s_usb_netif != NULL) {
        return ESP_OK;
    }

    bool netif_started = false;

    static const esp_netif_ip_info_t ip_info = {
        .ip = {
            .addr = ESP_IP4TOADDR(
                USB_NETWORK_IP_A,
                USB_NETWORK_IP_B,
                USB_NETWORK_IP_C,
                USB_NETWORK_IP_D
            ),
        },

        .netmask = {
            .addr = ESP_IP4TOADDR(
                255U,
                255U,
                255U,
                0U
            ),
        },

        .gw = {
            .addr = ESP_IP4TOADDR(
                USB_NETWORK_IP_A,
                USB_NETWORK_IP_B,
                USB_NETWORK_IP_C,
                USB_NETWORK_IP_D
            ),
        },
    };

    static const esp_netif_inherent_config_t base_config = {
        .flags =
            ESP_NETIF_DHCP_SERVER |
            ESP_NETIF_FLAG_AUTOUP,

        .mac = {0},
        .ip_info = &ip_info,

        .get_ip_event = 0,
        .lost_ip_event = 0,

        .if_key = "USB_RNDIS",
        .if_desc = "usb_rndis",

        .route_prio = 5,
        .bridge_info = NULL,
        .mtu = 1500U,
    };

    static const esp_netif_driver_ifconfig_t driver_config = {
        .handle = &s_netif_driver_context,
        .transmit = usb_network_transmit,
        .transmit_wrap = NULL,
        .driver_free_rx_buffer =
            usb_network_free_rx_buffer,
        .driver_set_mac_filter = NULL,
    };

    const esp_netif_config_t netif_config = {
        .base = &base_config,
        .driver = &driver_config,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
    };

    s_usb_netif =
        esp_netif_new(
            &netif_config
        );

    if (s_usb_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result =
        esp_netif_set_mac(
            s_usb_netif,
            tud_network_mac_address
        );

    if (result != ESP_OK) {
        goto fail;
    }

    const dhcps_lease_t lease = {
        .enable = true,

        .start_ip = {
            .addr = ESP_IP4TOADDR(
                USB_NETWORK_IP_A,
                USB_NETWORK_IP_B,
                USB_NETWORK_IP_C,
                USB_NETWORK_DHCP_START_D
            ),
        },

        .end_ip = {
            .addr = ESP_IP4TOADDR(
                USB_NETWORK_IP_A,
                USB_NETWORK_IP_B,
                USB_NETWORK_IP_C,
                USB_NETWORK_DHCP_END_D
            ),
        },
    };

    /*
     * Configure the DHCP address pool before starting the interface.
     */
    result = esp_netif_dhcps_option(
        s_usb_netif,
        ESP_NETIF_OP_SET,
        ESP_NETIF_REQUESTED_IP_ADDRESS,
        (void *)&lease,
        sizeof(lease)
    );

    if (result != ESP_OK) {
        goto fail;
    }

    esp_netif_dns_info_t dns_info = {
        .ip = ESP_IP4ADDR_INIT(
            USB_NETWORK_IP_A,
            USB_NETWORK_IP_B,
            USB_NETWORK_IP_C,
            USB_NETWORK_IP_D
        ),
    };

    /*
     * Advertise the device address as the DNS server.
     */
    result = esp_netif_set_dns_info(
        s_usb_netif,
        ESP_NETIF_DNS_MAIN,
        &dns_info
    );

    if (result != ESP_OK) {
        goto fail;
    }

    uint8_t offer_dns = 1U;

    /*
     * Include the configured DNS server in DHCP responses.
     */
    result = esp_netif_dhcps_option(
        s_usb_netif,
        ESP_NETIF_OP_SET,
        ESP_NETIF_DOMAIN_NAME_SERVER,
        &offer_dns,
        sizeof(offer_dns)
    );

    if (result != ESP_OK) {
        goto fail;
    }

    /*
     * Bring the network interface up and start its DHCP server.
     * esp_netif_action_start() is the public interface provided by ESP-IDF.
     */
    esp_netif_action_start(
        s_usb_netif,
        NULL,
        0,
        NULL
    );

    netif_started = true;

    esp_netif_dhcp_status_t dhcp_status =
        ESP_NETIF_DHCP_INIT;

    result = esp_netif_dhcps_get_status(
        s_usb_netif,
        &dhcp_status
    );

    if (result != ESP_OK) {
        goto fail;
    }

    if (dhcp_status != ESP_NETIF_DHCP_STARTED) {
        ESP_LOGE(
            TAG,
            "USB DHCP server did not start"
        );

        result = ESP_FAIL;
        goto fail;
    }

    ESP_LOGI(
        TAG,
        "USB network ready: "
        "IP 172.16.10.1, DHCP 172.16.10.2-172.16.10.10"
    );

    return ESP_OK;

fail:
    if (netif_started) {
        esp_netif_action_stop(
            s_usb_netif,
            NULL,
            0,
            NULL
        );
    }

    esp_netif_destroy(
        s_usb_netif
    );

    s_usb_netif = NULL;

    return result;
}

static void usb_network_event_callback(
    tinyusb_event_t *event,
    void *argument
)
{
    (void)argument;

    if (event == NULL) {
        return;
    }

    switch (event->id) {
        case TINYUSB_EVENT_ATTACHED:
            ESP_LOGI(
                TAG,
                "USB host attached"
            );
            break;

        case TINYUSB_EVENT_DETACHED:
            ESP_LOGI(
                TAG,
                "USB host detached"
            );
            break;

        default:
            break;
    }
}

bool tud_vendor_control_xfer_cb(
    uint8_t rhport,
    uint8_t stage,
    const tusb_control_request_t *request
)
{
    if (request == NULL) {
        return false;
    }

    /*
     * Data and ACK stages require no additional processing.
     */
    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }

    if (request->bmRequestType_bit.type !=
        TUSB_REQ_TYPE_VENDOR) {

        return false;
    }

    if (request->bmRequestType_bit.recipient !=
        TUSB_REQ_RCPT_DEVICE) {

        return false;
    }

    if (request->bmRequestType_bit.direction !=
        TUSB_DIR_IN) {

        return false;
    }

    if (request->bRequest != USB_MS_OS_VENDOR_CODE) {
        return false;
    }

    if (request->wIndex !=
        USB_MS_OS_EXT_COMPAT_ID_INDEX) {

        return false;
    }

    return tud_control_xfer(
        rhport,
        request,
        (void *)(uintptr_t)
            s_ms_os_compatible_id_descriptor,
        sizeof(s_ms_os_compatible_id_descriptor)
    );
}

esp_err_t usb_network_service_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    bool network_initialized = false;

    esp_err_t result =
        network_service_get_initialized(
            &network_initialized
        );

    if (result != ESP_OK) {
        return result;
    }

    if (!network_initialized) {
        ESP_LOGE(
            TAG,
            "Global network stack is not initialized"
        );

        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(
        usb_network_initialize_mac(),
        TAG,
        "Failed to initialize USB network MAC"
    );

    tinyusb_config_t usb_config =
        TINYUSB_DEFAULT_CONFIG(
            usb_network_event_callback
        );

    usb_config.descriptor.device =
        &s_device_descriptor;

    usb_config.descriptor.string =
        s_string_descriptors;

    usb_config.descriptor.string_count =
        sizeof(s_string_descriptors) /
        sizeof(s_string_descriptors[0]);

    usb_config.descriptor.full_speed_config =
        s_configuration_descriptor;

    ESP_RETURN_ON_ERROR(
        tinyusb_driver_install(
            &usb_config
        ),
        TAG,
        "Failed to install TinyUSB driver"
    );

    tinyusb_net_config_t network_config = {
        .on_recv_callback =
            usb_network_receive_callback,

        .free_tx_buffer = NULL,
        .on_init_callback = NULL,
        .user_context = NULL,
    };

    memcpy(
        network_config.mac_addr,
        tud_network_mac_address,
        sizeof(network_config.mac_addr)
    );

    result =
        tinyusb_net_init(
            &network_config
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize TinyUSB network: %s",
            esp_err_to_name(result)
        );

        (void)tinyusb_driver_uninstall();

        return result;
    }

    result = usb_network_netif_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize USB network interface: %s",
            esp_err_to_name(result)
        );

        tinyusb_net_deinit();

        (void)tinyusb_driver_uninstall();

        return result;
    }

    result = usb_network_dns_start();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to start USB DNS server: %s",
            esp_err_to_name(result)
        );

        esp_netif_action_stop(
            s_usb_netif,
            NULL,
            0,
            NULL
        );

        esp_netif_destroy(
            s_usb_netif
        );

        s_usb_netif = NULL;

        tinyusb_net_deinit();

        (void)tinyusb_driver_uninstall();

        return result;
    }

    s_initialized = true;

    ESP_LOGI(
        TAG,
        "USB RNDIS service initialized"
    );

    return ESP_OK;
}
