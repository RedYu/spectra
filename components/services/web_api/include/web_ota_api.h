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
 * @brief Register OTA status and command HTTP handlers.
 *
 * Registers GET and POST handlers for /api/ota. A POST without an action
 * query streams a firmware image. The cancel and restart commands use the
 * action query parameter.
 *
 * @param[in] server HTTP server handle.
 *
 * @return ESP_OK on success, otherwise an ESP HTTP server error code.
 */
esp_err_t web_ota_api_register(
    httpd_handle_t server
);

#ifdef __cplusplus
}
#endif
