/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register CAN transmission HTTP handlers.
 *
 * Registers handlers for reading transmission jobs and processing
 * explicit start and stop commands.
 *
 * @param[in] server HTTP server handle.
 *
 * @return ESP_OK on success, otherwise an ESP HTTP server error code.
 */
esp_err_t web_can_transmit_api_register(
    httpd_handle_t server
);

#ifdef __cplusplus
}
#endif
