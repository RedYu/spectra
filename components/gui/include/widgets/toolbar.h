#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file toolbar.h
 * @brief Create and manage application toolbars.
 *
 * All functions must be called from the GUI task because they access
 * LVGL objects directly.
 */

/**
 * @brief Toolbar action callback.
 */
typedef void (*toolbar_action_cb_t)(void);

/**
 * @brief Toolbar configuration.
 *
 * Text is copied by the corresponding LVGL label. The configuration
 * structure only needs to remain valid during toolbar_create().
 */
typedef struct
{
    const char *title;

    const lv_image_dsc_t *left_icon;
    toolbar_action_cb_t left_action;

    const lv_image_dsc_t *right_icon;
    toolbar_action_cb_t right_action;

    bool show_usb_status;
    toolbar_action_cb_t usb_action;

    bool show_wifi_status;
    toolbar_action_cb_t wifi_action;

    bool show_cpu_status;
    toolbar_action_cb_t cpu_action;

    bool show_sd_status;
    toolbar_action_cb_t sd_action;

    bool show_ota_status;
    toolbar_action_cb_t ota_action;

} toolbar_config_t;

/**
 * @brief Toolbar instance.
 *
 * The instance contains non-owning pointers to LVGL objects. These
 * pointers become invalid when the parent screen is deleted.
 */
typedef struct
{
    lv_obj_t *root;

    lv_obj_t *usb_button;
    lv_obj_t *usb_label;

    lv_obj_t *wifi_button;
    lv_obj_t *wifi_label;

    lv_obj_t *cpu_button;
    lv_obj_t *cpu_label;

    lv_obj_t *sd_button;
    lv_obj_t *ota_button;

} toolbar_t;

/**
 * @brief Create a toolbar.
 *
 * @param[in] parent Parent LVGL object.
 * @param[in] config Toolbar configuration.
 *
 * @return Created toolbar instance. The root field is NULL if creation
 * fails or an argument is invalid.
 */
toolbar_t toolbar_create(
    lv_obj_t *parent,
    const toolbar_config_t *config
);

/**
 * @brief Update the SD-card status indicator.
 *
 * @param[in,out] toolbar Toolbar instance.
 * @param[in] mounted true when the SD card is mounted.
 */
void toolbar_set_sd_mounted(
    toolbar_t *toolbar,
    bool mounted
);

/**
 * @brief Update the OTA-update status indicator.
 *
 * @param[in,out] toolbar Toolbar instance.
 * @param[in] available true when an OTA update is available.
 */
void toolbar_set_ota_available(
    toolbar_t *toolbar,
    bool available
);

/**
 * @brief Update the USB-network status indicator.
 *
 * @param[in,out] toolbar Toolbar instance.
 * @param[in] started true when the USB network interface is running.
 * @param[in] host_connected true when a USB host is connected.
 */
void toolbar_set_usb_status(
    toolbar_t *toolbar,
    bool started,
    bool host_connected
);

/**
 * @brief Update the Wi-Fi status indicator.
 *
 * @param[in,out] toolbar Toolbar instance.
 * @param[in] started true when Wi-Fi SoftAP is running.
 * @param[in] client_count Number of connected Wi-Fi clients.
 */
void toolbar_set_wifi_status(
    toolbar_t *toolbar,
    bool started,
    size_t client_count
);

/**
 * @brief Update the CPU-load status indicator.
 *
 * Values below 70 percent are displayed as normal, values from 70 to
 * 89 percent as a warning, and values from 90 percent as critical.
 *
 * @param[in,out] toolbar Toolbar instance.
 * @param[in] cpu_usage CPU usage percentage from 0 to 100.
 */
void toolbar_set_cpu_usage(
    toolbar_t *toolbar,
    uint8_t cpu_usage
);

#ifdef __cplusplus
}
#endif
