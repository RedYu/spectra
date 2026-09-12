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
 * @brief Register ISO-TP diagnostics HTTP handlers.
 */
esp_err_t web_isotp_api_register(
    httpd_handle_t server
);

#ifdef __cplusplus
}
#endif
