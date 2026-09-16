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
 * @brief Start Spectra backend availability monitoring.
 *
 * The service checks the configured Spectra health endpoint after the
 * Station obtains an IP address and on an explicit request. It does not
 * perform periodic HTTPS requests. Firmware availability is checked once
 * after Internet first becomes available during startup.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * already running, ESP_ERR_NO_MEM if the service task cannot be
 * created, otherwise an ESP-IDF error code.
 */
esp_err_t internet_service_start(void);

/**
 * @brief Request an asynchronous backend and firmware availability check.
 *
 * The Internet service task performs the HTTPS requests. This function does
 * not block the caller.
 *
 * @return ESP_OK when the request is queued or ESP_ERR_INVALID_STATE when the
 * service is not running.
 */
esp_err_t internet_service_request_check(void);

/**
 * @brief Request cooperative termination of the Internet service.
 *
 * The function returns immediately. The service task is notified and
 * terminates after the current HTTPS request has completed. Calling
 * this function when the service is not running has no effect.
 */
void internet_service_stop(void);

#ifdef __cplusplus
}
#endif
