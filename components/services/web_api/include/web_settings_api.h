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
 * @brief Register application-settings API handlers.
 *
 * Registers:
 * - GET /api/settings
 * - PUT /api/settings
 * - POST /api/settings/save
 *
 * @param[in] server HTTP server handle.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if server is NULL,
 * otherwise an ESP HTTP server error code.
 */
esp_err_t web_settings_api_register(
    httpd_handle_t server
);

#ifdef __cplusplus
}
#endif
