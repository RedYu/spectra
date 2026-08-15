#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USB_NETWORK_MAC_ADDRESS_LENGTH       (6U)
#define USB_NETWORK_IPV4_ADDRESS_MAX_LENGTH  (16U)

/**
 * @brief USB RNDIS runtime information.
 */
typedef struct
{
    /**
     * True when TinyUSB, RNDIS and ESP-NETIF resources are initialized.
     */
    bool initialized;

    /**
     * True when the USB network interface is active.
     */
    bool started;

    /**
     * True when a USB host is attached to the device.
     */
    bool host_connected;

    uint8_t mac[
        USB_NETWORK_MAC_ADDRESS_LENGTH
    ];

    char ip_address[
        USB_NETWORK_IPV4_ADDRESS_MAX_LENGTH
    ];

    char netmask[
        USB_NETWORK_IPV4_ADDRESS_MAX_LENGTH
    ];

    char dhcp_start[
        USB_NETWORK_IPV4_ADDRESS_MAX_LENGTH
    ];

    char dhcp_end[
        USB_NETWORK_IPV4_ADDRESS_MAX_LENGTH
    ];

    char dns_address[
        USB_NETWORK_IPV4_ADDRESS_MAX_LENGTH
    ];

} usb_network_service_info_t;

/**
 * @brief Initialize and start the USB RNDIS network interface.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * already initialized or the global network service is unavailable,
 * ESP_ERR_NO_MEM if required resources cannot be allocated, otherwise
 * an ESP-IDF error code.
 */
esp_err_t usb_network_service_init(void);

/**
 * @brief Stop the USB RNDIS service.
 *
 * Stops the USB network interface and DHCP server, releases the
 * ESP-NETIF instance and uninstalls the service-owned TinyUSB driver.
 *
 * This operation is intended for the final coordinated device-shutdown
 * sequence. Starting the service again after it has been stopped is not
 * currently supported without restarting the device.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not initialized, otherwise an ESP-IDF error code.
 */
esp_err_t usb_network_service_stop(void);

/**
 * @brief Get current USB RNDIS interface information.
 *
 * @param[out] info Destination information structure.
 *
 * @return ESP_OK on success or ESP_ERR_INVALID_ARG if info is NULL.
 */
esp_err_t usb_network_service_get_info(
    usb_network_service_info_t *info
);

#ifdef __cplusplus
}
#endif
