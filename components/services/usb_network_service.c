#include "usb_network_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"

#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_net.h"

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

static esp_err_t usb_network_receive_callback(
    void *buffer,
    uint16_t length,
    void *context
)
{
    /*
     * At the enumeration stage Ethernet frames are intentionally
     * discarded. Later this callback will pass them to esp_netif/lwIP.
     */
    (void)buffer;
    (void)length;
    (void)context;

    return ESP_OK;
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

esp_err_t usb_network_service_init(void)
{
    if (s_initialized) {
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

    const esp_err_t result =
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

    s_initialized = true;

    ESP_LOGI(
        TAG,
        "USB RNDIS initialized"
    );

    return ESP_OK;
}
