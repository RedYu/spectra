/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "time_service.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"

#define TIME_SERVICE_DEFAULT_TIMEZONE \
    ("CET-1CEST,M3.5.0,M10.5.0/3")
#define TIME_SERVICE_PRIMARY_SERVER   ("pool.ntp.org")
#define TIME_SERVICE_SECONDARY_SERVER ("time.cloudflare.com")

#define TIME_SERVICE_MINIMUM_VALID_EPOCH ((time_t)1704067200)

static const char *TAG = "time_service";

static atomic_bool s_running = false;
static atomic_bool s_synchronized = false;
static atomic_uint s_synchronization_count = 0U;

static esp_event_handler_instance_t s_sync_event_instance = NULL;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static time_t s_last_synchronization_time = 0;
static char s_timezone[TIME_SERVICE_TIMEZONE_MAX_LENGTH] =
    TIME_SERVICE_DEFAULT_TIMEZONE;

static bool time_service_text_valid(
    const char *text,
    size_t capacity
)
{
    return
        (text != NULL) &&
        (memchr(text, '\0', capacity) != NULL) &&
        (text[0] != '\0');
}

static void time_service_sync_event_handler(
    void *argument,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
)
{
    (void)argument;
    (void)event_data;

    if (!atomic_load_explicit(
            &s_running,
            memory_order_acquire
        )) {

        return;
    }

    if ((event_base != NETIF_SNTP_EVENT) ||
        (event_id != NETIF_SNTP_TIME_SYNC)) {

        return;
    }

    const time_t synchronized_at = time(NULL);

    portENTER_CRITICAL(&s_lock);
    s_last_synchronization_time = synchronized_at;
    portEXIT_CRITICAL(&s_lock);

    atomic_store_explicit(
        &s_synchronized,
        true,
        memory_order_release
    );

    const unsigned int count =
        atomic_fetch_add_explicit(
            &s_synchronization_count,
            1U,
            memory_order_relaxed
        ) + 1U;

    struct tm utc_time = {0};
    char timestamp[32] = {0};

    if ((gmtime_r(&synchronized_at, &utc_time) != NULL) &&
        (strftime(
            timestamp,
            sizeof(timestamp),
            "%Y-%m-%dT%H:%M:%SZ",
            &utc_time
        ) > 0U)) {

        ESP_LOGI(
            TAG,
            "System time synchronized: %s, count=%u",
            timestamp,
            count
        );
    } else {
        ESP_LOGI(
            TAG,
            "System time synchronized, count=%u",
            count
        );
    }
}

esp_err_t time_service_start(void)
{
    bool expected = false;

    if (!atomic_compare_exchange_strong(
            &s_running,
            &expected,
            true
        )) {

        return ESP_ERR_INVALID_STATE;
    }

    atomic_store(&s_synchronized, false);
    atomic_store(&s_synchronization_count, 0U);

    portENTER_CRITICAL(&s_lock);
    s_last_synchronization_time = 0;
    portEXIT_CRITICAL(&s_lock);

    esp_err_t result =
        time_service_set_timezone(
            TIME_SERVICE_DEFAULT_TIMEZONE
        );

    if (result != ESP_OK) {
        atomic_store(&s_running, false);
        return result;
    }

    result = esp_event_handler_instance_register(
        NETIF_SNTP_EVENT,
        NETIF_SNTP_TIME_SYNC,
        time_service_sync_event_handler,
        NULL,
        &s_sync_event_instance
    );

    if (result != ESP_OK) {
        atomic_store(&s_running, false);
        return result;
    }

    const esp_sntp_config_t config =
#if CONFIG_LWIP_SNTP_MAX_SERVERS >= 2
        ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
            2U,
            ESP_SNTP_SERVER_LIST(
                TIME_SERVICE_PRIMARY_SERVER,
                TIME_SERVICE_SECONDARY_SERVER
            )
        );
#else
        ESP_NETIF_SNTP_DEFAULT_CONFIG(
            TIME_SERVICE_PRIMARY_SERVER
        );
#endif

    result = esp_netif_sntp_init(&config);

    if (result != ESP_OK) {
        (void)esp_event_handler_instance_unregister(
            NETIF_SNTP_EVENT,
            NETIF_SNTP_TIME_SYNC,
            s_sync_event_instance
        );

        s_sync_event_instance = NULL;
        atomic_store(&s_running, false);
        return result;
    }
    
#if CONFIG_LWIP_SNTP_MAX_SERVERS >= 2
    ESP_LOGI(
        TAG,
        "SNTP started: servers=%s,%s, timezone=%s",
        TIME_SERVICE_PRIMARY_SERVER,
        TIME_SERVICE_SECONDARY_SERVER,
        s_timezone
    );
#else
    ESP_LOGI(
        TAG,
        "SNTP started: server=%s, timezone=%s",
        TIME_SERVICE_PRIMARY_SERVER,
        s_timezone
    );
#endif

    return ESP_OK;
}

void time_service_stop(void)
{
    if (!atomic_exchange(&s_running, false)) {
        return;
    }

    if (s_sync_event_instance != NULL) {
        (void)esp_event_handler_instance_unregister(
            NETIF_SNTP_EVENT,
            NETIF_SNTP_TIME_SYNC,
            s_sync_event_instance
        );

        s_sync_event_instance = NULL;
    }

    esp_netif_sntp_deinit();

    ESP_LOGI(TAG, "Time service stopped");
}

bool time_service_is_running(void)
{
    return atomic_load_explicit(
        &s_running,
        memory_order_acquire
    );
}

bool time_service_time_valid(void)
{
    return time(NULL) >= TIME_SERVICE_MINIMUM_VALID_EPOCH;
}

bool time_service_is_synchronized(void)
{
    return atomic_load_explicit(
        &s_synchronized,
        memory_order_acquire
    );
}

esp_err_t time_service_set_timezone(
    const char *timezone
)
{
    if (!time_service_text_valid(
            timezone,
            TIME_SERVICE_TIMEZONE_MAX_LENGTH
        )) {

        return ESP_ERR_INVALID_ARG;
    }

    if (setenv("TZ", timezone, 1) != 0) {
        return ESP_FAIL;
    }

    tzset();

    portENTER_CRITICAL(&s_lock);
    (void)strlcpy(
        s_timezone,
        timezone,
        sizeof(s_timezone)
    );
    portEXIT_CRITICAL(&s_lock);

    return ESP_OK;
}

esp_err_t time_service_get_local_time(
    struct tm *time_info
)
{
    if (time_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const time_t now = time(NULL);

    if (now < TIME_SERVICE_MINIMUM_VALID_EPOCH) {
        memset(time_info, 0, sizeof(*time_info));
        return ESP_ERR_INVALID_STATE;
    }

    if (localtime_r(&now, time_info) == NULL) {
        memset(time_info, 0, sizeof(*time_info));
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t time_service_get_info(
    time_service_info_t *info
)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(info, 0, sizeof(*info));

    info->running = time_service_is_running();
    info->time_valid = time_service_time_valid();
    info->synchronized = time_service_is_synchronized();
    info->synchronization_count =
        atomic_load(&s_synchronization_count);

    portENTER_CRITICAL(&s_lock);
    info->last_synchronization_time =
        s_last_synchronization_time;

    (void)strlcpy(
        info->timezone,
        s_timezone,
        sizeof(info->timezone)
    );
    portEXIT_CRITICAL(&s_lock);

    return ESP_OK;
}

esp_err_t time_service_format_filename(
    const char *prefix,
    const char *extension,
    char *buffer,
    size_t buffer_size
)
{
    if ((prefix == NULL) ||
        (extension == NULL) ||
        (buffer == NULL) ||
        (buffer_size == 0U) ||
        (prefix[0] == '\0') ||
        (extension[0] == '\0') ||
        (strchr(prefix, '/') != NULL) ||
        (strchr(prefix, '\\') != NULL) ||
        (strchr(extension, '/') != NULL) ||
        (strchr(extension, '\\') != NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    buffer[0] = '\0';

    struct tm local_time = {0};
    const esp_err_t result =
        time_service_get_local_time(&local_time);

    if (result != ESP_OK) {
        return result;
    }

    const int written = snprintf(
        buffer,
        buffer_size,
        "%s-%04d%02d%02d-%02d%02d%02d.%s",
        prefix,
        local_time.tm_year + 1900,
        local_time.tm_mon + 1,
        local_time.tm_mday,
        local_time.tm_hour,
        local_time.tm_min,
        local_time.tm_sec,
        extension
    );

    if ((written < 0) ||
        ((size_t)written >= buffer_size)) {

        buffer[0] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}
