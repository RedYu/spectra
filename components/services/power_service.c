/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "power_service.h"

#include <string.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define POWER_SERVICE_LOCK_TIMEOUT_MS  (250U)

static const char *TAG =
    "power_service";

static SemaphoreHandle_t s_mutex = NULL;
static bool s_initialized = false;

static esp_err_t power_service_lock(void)
{
    if (!s_initialized ||
        (s_mutex == NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_mutex,
            pdMS_TO_TICKS(
                POWER_SERVICE_LOCK_TIMEOUT_MS
            )
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void power_service_unlock(void)
{
    if (s_mutex != NULL) {
        (void)xSemaphoreGive(
            s_mutex
        );
    }
}

esp_err_t power_service_init(
    i2c_master_bus_handle_t bus
)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_initialized ||
        (s_mutex != NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    s_mutex =
        xSemaphoreCreateMutex();

    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t result =
        axp313a_driver_init(
            bus
        );

    if (result != ESP_OK) {
        vSemaphoreDelete(
            s_mutex
        );

        s_mutex = NULL;

        ESP_LOGE(
            TAG,
            "Failed to initialize AXP313A: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    s_initialized = true;

    ESP_LOGI(
        TAG,
        "Power-management service initialized"
    );

    return ESP_OK;
}

esp_err_t power_service_deinit(void)
{
    const esp_err_t lock_result =
        power_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    const esp_err_t result =
        axp313a_driver_deinit();

    if (result != ESP_OK) {
        power_service_unlock();

        ESP_LOGE(
            TAG,
            "Failed to deinitialize AXP313A: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    s_initialized = false;

    power_service_unlock();

    vSemaphoreDelete(
        s_mutex
    );

    s_mutex = NULL;

    ESP_LOGI(
        TAG,
        "Power-management service deinitialized"
    );

    return ESP_OK;
}

esp_err_t power_service_get_initialized(
    bool *initialized
)
{
    if (initialized == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *initialized =
        s_initialized;

    return ESP_OK;
}

esp_err_t power_service_get_snapshot(
    power_service_snapshot_t *snapshot
)
{
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(
        snapshot,
        0,
        sizeof(*snapshot)
    );

    esp_err_t result =
        power_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    result =
        axp313a_driver_get_status(
            &snapshot->status
        );

    if (result == ESP_OK) {
        result =
            axp313a_driver_get_configuration(
                &snapshot->configuration
            );
    }

    power_service_unlock();

    return result;
}

esp_err_t power_service_set_aldo1_enabled(
    bool enabled
)
{
    esp_err_t result =
        power_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    result =
        axp313a_driver_set_aldo1_enabled(
            enabled
        );

    power_service_unlock();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to %s ALDO1: %s",
            enabled
                ? "enable"
                : "disable",
            esp_err_to_name(result)
        );
    }

    return result;
}

esp_err_t power_service_set_dldo1_enabled(
    bool enabled
)
{
    esp_err_t result =
        power_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    result =
        axp313a_driver_set_dldo1_enabled(
            enabled
        );

    power_service_unlock();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to %s DLDO1: %s",
            enabled
                ? "enable"
                : "disable",
            esp_err_to_name(result)
        );
    }

    return result;
}
