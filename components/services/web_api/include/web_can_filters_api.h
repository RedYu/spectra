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
 * @brief Register CAN hardware receive-filter HTTP handlers.
 *
 * Registers handlers for reading the hardware filter capabilities and
 * current configuration of both CAN interfaces, and for applying a new
 * runtime filter configuration.
 *
 * @param[in] server HTTP server handle.
 *
 * @return ESP_OK on success, otherwise an ESP HTTP server error code.
 */
esp_err_t web_can_filters_api_register(
    httpd_handle_t server
);

#ifdef __cplusplus
}
#endif
