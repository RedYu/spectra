/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once
/**
 * @file wifi_credentials_dialog.h
 * @brief Display a Wi-Fi credentials input dialog.
 *
 * All functions must be called from the GUI task because they access
 * LVGL objects directly.
 */

#include <stdbool.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wi-Fi credentials submission callback.
 *
 * The SSID and password pointers are valid only during the callback.
 * The callback must copy them if they are needed later.
 *
 * Returning ESP_OK closes the dialog. Returning an error keeps the
 * dialog open and displays the error to the user.
 */
typedef esp_err_t (*wifi_credentials_dialog_submit_cb_t)(
    const char *ssid,
    const char *password,
    void *context
);

/**
 * @brief Open the Wi-Fi credentials dialog.
 *
 * Only one Wi-Fi credentials dialog may be open at a time.
 *
 * For an open network, the password field and keyboard are not shown.
 *
 * @param[in] parent Parent LVGL object, normally the active screen.
 * @param[in] ssid Selected Wi-Fi network SSID.
 * @param[in] password_required True when the network requires a
 * password.
 * @param[in] submit_callback Callback invoked when the user confirms.
 * @param[in] context Optional callback context.
 *
 * @return true when the dialog was created; otherwise false.
 */
bool wifi_credentials_dialog_open(
    lv_obj_t *parent,
    const char *ssid,
    bool password_required,
    wifi_credentials_dialog_submit_cb_t submit_callback,
    void *context
);

/**
 * @brief Close the Wi-Fi credentials dialog.
 *
 * Calling this function when the dialog is not open has no effect.
 */
void wifi_credentials_dialog_close(void);

/**
 * @brief Check whether the Wi-Fi credentials dialog is open.
 *
 * @return true when the dialog is currently open; otherwise false.
 */
bool wifi_credentials_dialog_is_open(void);

#ifdef __cplusplus
}
#endif
