/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TIME_SERVICE_TIMEZONE_MAX_LENGTH (64U)

/**
 * @brief Current system-time synchronization information.
 */
typedef struct
{
    bool running;
    bool time_valid;
    bool synchronized;

    uint32_t synchronization_count;
    time_t last_synchronization_time;

    char timezone[TIME_SERVICE_TIMEZONE_MAX_LENGTH];

} time_service_info_t;

/**
 * @brief Start system-clock synchronization through SNTP.
 *
 * The network stack must be initialized before this function is called.
 * An active network connection is not required at startup; SNTP retries
 * automatically after connectivity becomes available.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already running,
 * otherwise an ESP-IDF error code.
 */
esp_err_t time_service_start(void);

/**
 * @brief Stop SNTP synchronization.
 *
 * The current system time remains available and continues advancing after
 * SNTP is stopped.
 */
void time_service_stop(void);

/**
 * @brief Check whether the time service is running.
 */
bool time_service_is_running(void);

/**
 * @brief Check whether the system clock contains a plausible date.
 *
 * This can be true before the first synchronization when time was restored
 * by another trusted source.
 */
bool time_service_time_valid(void);

/**
 * @brief Check whether SNTP has synchronized time during this boot.
 */
bool time_service_is_synchronized(void);

/**
 * @brief Set the process-wide POSIX timezone.
 *
 * Example for Poland: "CET-1CEST,M3.5.0,M10.5.0/3".
 * The timezone only affects local-time conversion; Unix timestamps remain
 * in UTC.
 *
 * @param[in] timezone POSIX timezone string.
 * @return ESP_OK on success or ESP_ERR_INVALID_ARG for an invalid value.
 */
esp_err_t time_service_set_timezone(
    const char *timezone
);

/**
 * @brief Convert the current system time to local calendar time.
 *
 * @param[out] time_info Destination calendar-time structure.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if time_info is NULL, or
 * ESP_ERR_INVALID_STATE if the system time is not valid.
 */
esp_err_t time_service_get_local_time(
    struct tm *time_info
);

/**
 * @brief Get current time-service information.
 */
esp_err_t time_service_get_info(
    time_service_info_t *info
);

/**
 * @brief Create a timestamped filename component using local time.
 *
 * The generated value has the form "prefix-YYYYMMDD-HHMMSS.extension".
 * Prefix and extension must not contain path separators. No file is created.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid arguments,
 * ESP_ERR_INVALID_STATE if time is invalid, or ESP_ERR_INVALID_SIZE when the
 * destination buffer is too small.
 */
esp_err_t time_service_format_filename(
    const char *prefix,
    const char *extension,
    char *buffer,
    size_t buffer_size
);

#ifdef __cplusplus
}
#endif
