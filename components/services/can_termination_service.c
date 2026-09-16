/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "can_termination_service.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_log.h"

#include "io_expander_service.h"

#define CAN_TERMINATION_LOCK_TIMEOUT_MS  (100U)

static const char *TAG =
    "can_termination";

static SemaphoreHandle_t s_mutex = NULL;
static can_termination_service_info_t s_info = {0};

static esp_err_t can_termination_service_lock(void)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return xSemaphoreTake(
        s_mutex,
        pdMS_TO_TICKS(CAN_TERMINATION_LOCK_TIMEOUT_MS)
    ) == pdTRUE
        ? ESP_OK
        : ESP_ERR_TIMEOUT;
}

static void can_termination_service_unlock(void)
{
    (void)xSemaphoreGive(s_mutex);
}

esp_err_t can_termination_service_init(void)
{
    if (s_mutex != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_mutex = xSemaphoreCreateMutex();

    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result =
        io_expander_service_configure_output(
            IO_EXPANDER_OUTPUT_CAN_PRIMARY_TERMINATION,
            false
        );

    if (result == ESP_OK) {
        result =
            io_expander_service_configure_output(
                IO_EXPANDER_OUTPUT_CAN_SECONDARY_TERMINATION,
                false
            );
    }

    if (result != ESP_OK) {
        (void)io_expander_service_release_output(
            IO_EXPANDER_OUTPUT_CAN_PRIMARY_TERMINATION
        );

        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;

        return result;
    }

    s_info =
        (can_termination_service_info_t){
            .initialized = true,
        };

    ESP_LOGI(
        TAG,
        "CAN termination control initialized"
    );

    return ESP_OK;
}

esp_err_t can_termination_service_deinit(void)
{
    esp_err_t result =
        can_termination_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    result =
        io_expander_service_release_output(
            IO_EXPANDER_OUTPUT_CAN_PRIMARY_TERMINATION
        );

    if (result == ESP_OK) {
        result =
            io_expander_service_release_output(
                IO_EXPANDER_OUTPUT_CAN_SECONDARY_TERMINATION
            );
    }

    if (result == ESP_OK) {
        s_info =
            (can_termination_service_info_t){0};
    }

    can_termination_service_unlock();

    if (result == ESP_OK) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }

    return result;
}

esp_err_t can_termination_service_set_enabled(
    can_bus_id_t bus,
    bool enabled
)
{
    io_expander_output_t output;

    switch (bus) {
        case CAN_BUS_PRIMARY:
            output =
                IO_EXPANDER_OUTPUT_CAN_PRIMARY_TERMINATION;
            break;

        case CAN_BUS_SECONDARY:
            output =
                IO_EXPANDER_OUTPUT_CAN_SECONDARY_TERMINATION;
            break;

        default:
            return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        can_termination_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!s_info.initialized) {
        can_termination_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    result =
        io_expander_service_set_output(
            output,
            enabled
        );

    if (result == ESP_OK) {
        if (bus == CAN_BUS_PRIMARY) {
            s_info.primary_enabled = enabled;
        } else {
            s_info.secondary_enabled = enabled;
        }

        ESP_LOGI(
            TAG,
            "%s CAN termination %s",
            bus == CAN_BUS_PRIMARY
                ? "Primary"
                : "Secondary",
            enabled
                ? "enabled"
                : "disabled"
        );
    }

    can_termination_service_unlock();

    return result;
}

esp_err_t can_termination_service_get_info(
    can_termination_service_info_t *info
)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *info =
        (can_termination_service_info_t){0};

    esp_err_t result =
        can_termination_service_lock();

    if (result == ESP_OK) {
        if (!s_info.initialized) {
            result = ESP_ERR_INVALID_STATE;
        } else {
            *info = s_info;
        }

        can_termination_service_unlock();
    }

    return result;
}
