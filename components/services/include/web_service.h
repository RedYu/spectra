/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the local device web server.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the server is
 * already running, otherwise an ESP-IDF error code.
 */
esp_err_t web_service_start(void);

/**
 * @brief Stop the local device web server.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the server is
 * not running, otherwise an ESP-IDF error code.
 */
esp_err_t web_service_stop(void);

#ifdef __cplusplus
}
#endif
