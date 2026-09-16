/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "can_termination_service.h"

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_log.h"

#include "board_config.h"
#include "mcp23017_driver.h"

#define CAN_TERMINATION_LOCK_TIMEOUT_MS  (100U)

_Static_assert(
    CAN_PRIMARY_TERMINATION_EXPANDER_PIN <
    MCP23017_PIN_COUNT,
    "Primary termination pin must belong to MCP23017"
);

_Static_assert(
    CAN_SECONDARY_TERMINATION_EXPANDER_PIN <
    MCP23017_PIN_COUNT,
    "Secondary termination pin must belong to MCP23017"
);

_Static_assert(
    CAN_PRIMARY_TERMINATION_EXPANDER_PIN !=
    CAN_SECONDARY_TERMINATION_EXPANDER_PIN,
    "CAN termination controls require distinct MCP23017 pins"
);

static const char *TAG =
    "can_termination";

static SemaphoreHandle_t s_mutex = NULL;

static can_termination_service_info_t s_info = {0};

static uint16_t can_termination_service_pin_mask(
    uint8_t pin
)
{
    return (uint16_t)(1U << pin);
}

static uint16_t can_termination_service_level(
    uint8_t pin,
    bool active_high,
    bool enabled
)
{
    const bool level =
        enabled
            ? active_high
            : !active_high;

    return level
        ? can_termination_service_pin_mask(pin)
        : 0U;
}

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

    const uint16_t primary_mask =
        can_termination_service_pin_mask(
            CAN_PRIMARY_TERMINATION_EXPANDER_PIN
        );

    const uint16_t secondary_mask =
        can_termination_service_pin_mask(
            CAN_SECONDARY_TERMINATION_EXPANDER_PIN
        );

    const uint16_t mask =
        primary_mask |
        secondary_mask;

    const uint16_t inactive_levels =
        can_termination_service_level(
            CAN_PRIMARY_TERMINATION_EXPANDER_PIN,
            CAN_PRIMARY_TERMINATION_ACTIVE_LEVEL != 0,
            false
        ) |
        can_termination_service_level(
            CAN_SECONDARY_TERMINATION_EXPANDER_PIN,
            CAN_SECONDARY_TERMINATION_ACTIVE_LEVEL != 0,
            false
        );

    esp_err_t result =
        mcp23017_driver_update_gpio(
            mask,
            inactive_levels
        );

    if (result == ESP_OK) {
        result =
            mcp23017_driver_set_pull_up(
                mask,
                0U
            );
    }

    if (result == ESP_OK) {
        result =
            mcp23017_driver_set_input_polarity(
                mask,
                0U
            );
    }

    if (result == ESP_OK) {
        result =
            mcp23017_driver_set_direction(
                mask,
                0U
            );
    }

    if (result != ESP_OK) {
        (void)mcp23017_driver_set_direction(
            mask,
            mask
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
        "CAN termination control initialized: primary=GPA%u, "
        "secondary=GPA%u",
        (unsigned int)
            CAN_PRIMARY_TERMINATION_EXPANDER_PIN,
        (unsigned int)
            CAN_SECONDARY_TERMINATION_EXPANDER_PIN
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

    const uint16_t primary_mask =
        can_termination_service_pin_mask(
            CAN_PRIMARY_TERMINATION_EXPANDER_PIN
        );

    const uint16_t secondary_mask =
        can_termination_service_pin_mask(
            CAN_SECONDARY_TERMINATION_EXPANDER_PIN
        );

    const uint16_t mask =
        primary_mask |
        secondary_mask;

    const uint16_t inactive_levels =
        can_termination_service_level(
            CAN_PRIMARY_TERMINATION_EXPANDER_PIN,
            CAN_PRIMARY_TERMINATION_ACTIVE_LEVEL != 0,
            false
        ) |
        can_termination_service_level(
            CAN_SECONDARY_TERMINATION_EXPANDER_PIN,
            CAN_SECONDARY_TERMINATION_ACTIVE_LEVEL != 0,
            false
        );

    result =
        mcp23017_driver_update_gpio(
            mask,
            inactive_levels
        );

    if (result == ESP_OK) {
        result =
            mcp23017_driver_set_direction(
                mask,
                mask
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
    can_bus_t bus,
    bool enabled
)
{
    uint8_t pin = 0U;
    bool active_high = false;

    switch (bus) {
        case CAN_BUS_PRIMARY:
            pin =
                CAN_PRIMARY_TERMINATION_EXPANDER_PIN;

            active_high =
                CAN_PRIMARY_TERMINATION_ACTIVE_LEVEL != 0;
            break;

        case CAN_BUS_SECONDARY:
            pin =
                CAN_SECONDARY_TERMINATION_EXPANDER_PIN;

            active_high =
                CAN_SECONDARY_TERMINATION_ACTIVE_LEVEL != 0;
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

    const uint16_t mask =
        can_termination_service_pin_mask(pin);

    result =
        mcp23017_driver_update_gpio(
            mask,
            can_termination_service_level(
                pin,
                active_high,
                enabled
            )
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
