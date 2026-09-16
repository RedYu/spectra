/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "io_expander_service.h"

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_log.h"

#include "board_config.h"
#include "mcp23017_driver.h"

#define IO_EXPANDER_LOCK_TIMEOUT_MS  (100U)

typedef struct
{
    uint8_t pin;
    bool active_high;
    const char *name;

} io_expander_output_descriptor_t;

static const char *TAG =
    "io_expander";

static const io_expander_output_descriptor_t
    s_output_descriptors[IO_EXPANDER_OUTPUT_COUNT] = {
        [IO_EXPANDER_OUTPUT_CAN_PRIMARY_TERMINATION] = {
            .pin = CAN_PRIMARY_TERMINATION_EXPANDER_PIN,
            .active_high =
                CAN_PRIMARY_TERMINATION_ACTIVE_LEVEL != 0,
            .name = "can_primary_termination",
        },
        [IO_EXPANDER_OUTPUT_CAN_SECONDARY_TERMINATION] = {
            .pin = CAN_SECONDARY_TERMINATION_EXPANDER_PIN,
            .active_high =
                CAN_SECONDARY_TERMINATION_ACTIVE_LEVEL != 0,
            .name = "can_secondary_termination",
        },
        [IO_EXPANDER_OUTPUT_CAN_PRIMARY_STANDBY] = {
            .pin = CAN_PRIMARY_STANDBY_EXPANDER_PIN,
            .active_high =
                CAN_PRIMARY_STANDBY_ACTIVE_LEVEL != 0,
            .name = "can_primary_standby",
        },
        [IO_EXPANDER_OUTPUT_CAN_SECONDARY_STANDBY] = {
            .pin = CAN_SECONDARY_STANDBY_EXPANDER_PIN,
            .active_high =
                CAN_SECONDARY_STANDBY_ACTIVE_LEVEL != 0,
            .name = "can_secondary_standby",
        },
        [IO_EXPANDER_OUTPUT_CAN_ROUTE_SELECT_0] = {
            .pin = CAN_ROUTE_SELECT_0_EXPANDER_PIN,
            .active_high =
                CAN_ROUTE_SELECT_0_ACTIVE_LEVEL != 0,
            .name = "can_route_select_0",
        },
        [IO_EXPANDER_OUTPUT_CAN_ROUTE_SELECT_1] = {
            .pin = CAN_ROUTE_SELECT_1_EXPANDER_PIN,
            .active_high =
                CAN_ROUTE_SELECT_1_ACTIVE_LEVEL != 0,
            .name = "can_route_select_1",
        },
        [IO_EXPANDER_OUTPUT_CAN_ROUTE_ENABLE] = {
            .pin = CAN_ROUTE_ENABLE_EXPANDER_PIN,
            .active_high =
                CAN_ROUTE_ENABLE_ACTIVE_LEVEL != 0,
            .name = "can_route_enable",
        },
    };

static SemaphoreHandle_t s_mutex = NULL;
static io_expander_service_info_t s_info = {0};

static bool io_expander_service_output_valid(
    io_expander_output_t output
)
{
    return
        ((unsigned int)output <
         (unsigned int)IO_EXPANDER_OUTPUT_COUNT);
}

static uint16_t io_expander_service_output_mask(
    io_expander_output_t output
)
{
    return (uint16_t)(1U << (unsigned int)output);
}

static uint16_t io_expander_service_pin_mask(
    const io_expander_output_descriptor_t *descriptor
)
{
    return (uint16_t)(1U << descriptor->pin);
}

static uint16_t io_expander_service_physical_level(
    const io_expander_output_descriptor_t *descriptor,
    bool active
)
{
    const bool level =
        active
            ? descriptor->active_high
            : !descriptor->active_high;

    return level
        ? io_expander_service_pin_mask(descriptor)
        : 0U;
}

static esp_err_t io_expander_service_lock(void)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return xSemaphoreTake(
        s_mutex,
        pdMS_TO_TICKS(IO_EXPANDER_LOCK_TIMEOUT_MS)
    ) == pdTRUE
        ? ESP_OK
        : ESP_ERR_TIMEOUT;
}

static void io_expander_service_unlock(void)
{
    (void)xSemaphoreGive(s_mutex);
}

static esp_err_t io_expander_service_validate_assignments(void)
{
    uint16_t assigned_pins = 0U;

    for (unsigned int i = 0U;
         i < (unsigned int)IO_EXPANDER_OUTPUT_COUNT;
         ++i) {

        const io_expander_output_descriptor_t *descriptor =
            &s_output_descriptors[i];

        if (descriptor->pin >= MCP23017_PIN_COUNT) {
            return ESP_ERR_INVALID_ARG;
        }

        const uint16_t pin_mask =
            io_expander_service_pin_mask(descriptor);

        if ((assigned_pins & pin_mask) != 0U) {
            return ESP_ERR_INVALID_STATE;
        }

        assigned_pins |= pin_mask;
    }

    return ESP_OK;
}

esp_err_t io_expander_service_init(void)
{
    if (s_mutex != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t validation_result =
        io_expander_service_validate_assignments();

    if (validation_result != ESP_OK) {
        return validation_result;
    }

    s_mutex = xSemaphoreCreateMutex();

    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_info =
        (io_expander_service_info_t){
            .initialized = true,
        };

    ESP_LOGI(
        TAG,
        "MCP23017 ownership initialized: signals=%u, reserved pins=7",
        (unsigned int)IO_EXPANDER_OUTPUT_COUNT
    );

    return ESP_OK;
}

esp_err_t io_expander_service_deinit(void)
{
    esp_err_t result =
        io_expander_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    uint16_t pin_mask = 0U;
    uint16_t inactive_levels = 0U;

    for (unsigned int i = 0U;
         i < (unsigned int)IO_EXPANDER_OUTPUT_COUNT;
         ++i) {

        const uint16_t output_mask =
            (uint16_t)(1U << i);

        if ((s_info.configured_outputs &
             output_mask) == 0U) {

            continue;
        }

        const io_expander_output_descriptor_t *descriptor =
            &s_output_descriptors[i];

        pin_mask |=
            io_expander_service_pin_mask(
                descriptor
            );

        inactive_levels |=
            io_expander_service_physical_level(
                descriptor,
                false
            );
    }

    if (pin_mask != 0U) {
        result =
            mcp23017_driver_update_gpio(
                pin_mask,
                inactive_levels
            );

        if (result == ESP_OK) {
            result =
                mcp23017_driver_set_direction(
                    pin_mask,
                    pin_mask
                );
        }
    }

    if (result == ESP_OK) {
        s_info =
            (io_expander_service_info_t){0};
    }

    io_expander_service_unlock();

    if (result == ESP_OK) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }

    return result;
}

esp_err_t io_expander_service_configure_output(
    io_expander_output_t output,
    bool active
)
{
    if (!io_expander_service_output_valid(output)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        io_expander_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    const uint16_t output_mask =
        io_expander_service_output_mask(output);

    if ((s_info.configured_outputs &
         output_mask) != 0U) {

        io_expander_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    const io_expander_output_descriptor_t *descriptor =
        &s_output_descriptors[output];

    const uint16_t pin_mask =
        io_expander_service_pin_mask(
            descriptor
        );

    result =
        mcp23017_driver_update_gpio(
            pin_mask,
            io_expander_service_physical_level(
                descriptor,
                active
            )
        );

    if (result == ESP_OK) {
        result =
            mcp23017_driver_set_pull_up(
                pin_mask,
                0U
            );
    }

    if (result == ESP_OK) {
        result =
            mcp23017_driver_set_input_polarity(
                pin_mask,
                0U
            );
    }

    if (result == ESP_OK) {
        result =
            mcp23017_driver_set_direction(
                pin_mask,
                0U
            );
    }

    if (result == ESP_OK) {
        s_info.configured_outputs |=
            output_mask;

        if (active) {
            s_info.active_outputs |=
                output_mask;
        }

        ESP_LOGI(
            TAG,
            "Output configured: %s, pin=%u, active=%u",
            descriptor->name,
            (unsigned int)descriptor->pin,
            (unsigned int)active
        );
    } else {
        (void)mcp23017_driver_set_direction(
            pin_mask,
            pin_mask
        );
    }

    io_expander_service_unlock();

    return result;
}

esp_err_t io_expander_service_release_output(
    io_expander_output_t output
)
{
    if (!io_expander_service_output_valid(output)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        io_expander_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    const uint16_t output_mask =
        io_expander_service_output_mask(output);

    if ((s_info.configured_outputs &
         output_mask) == 0U) {

        io_expander_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    const io_expander_output_descriptor_t *descriptor =
        &s_output_descriptors[output];

    const uint16_t pin_mask =
        io_expander_service_pin_mask(
            descriptor
        );

    result =
        mcp23017_driver_update_gpio(
            pin_mask,
            io_expander_service_physical_level(
                descriptor,
                false
            )
        );

    if (result == ESP_OK) {
        result =
            mcp23017_driver_set_direction(
                pin_mask,
                pin_mask
            );
    }

    if (result == ESP_OK) {
        s_info.configured_outputs &=
            (uint16_t)~output_mask;

        s_info.active_outputs &=
            (uint16_t)~output_mask;
    }

    io_expander_service_unlock();

    return result;
}

esp_err_t io_expander_service_set_output(
    io_expander_output_t output,
    bool active
)
{
    if (!io_expander_service_output_valid(output)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        io_expander_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    const uint16_t output_mask =
        io_expander_service_output_mask(output);

    if ((s_info.configured_outputs &
         output_mask) == 0U) {

        io_expander_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    const io_expander_output_descriptor_t *descriptor =
        &s_output_descriptors[output];

    result =
        mcp23017_driver_update_gpio(
            io_expander_service_pin_mask(
                descriptor
            ),
            io_expander_service_physical_level(
                descriptor,
                active
            )
        );

    if (result == ESP_OK) {
        if (active) {
            s_info.active_outputs |=
                output_mask;
        } else {
            s_info.active_outputs &=
                (uint16_t)~output_mask;
        }
    }

    io_expander_service_unlock();

    return result;
}

esp_err_t io_expander_service_get_output(
    io_expander_output_t output,
    bool *active
)
{
    if (!io_expander_service_output_valid(output) ||
        (active == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *active = false;

    esp_err_t result =
        io_expander_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    const uint16_t output_mask =
        io_expander_service_output_mask(output);

    if ((s_info.configured_outputs &
         output_mask) == 0U) {

        result = ESP_ERR_INVALID_STATE;
    } else {
        *active =
            (s_info.active_outputs &
             output_mask) != 0U;
    }

    io_expander_service_unlock();

    return result;
}

esp_err_t io_expander_service_get_info(
    io_expander_service_info_t *info
)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *info =
        (io_expander_service_info_t){0};

    esp_err_t result =
        io_expander_service_lock();

    if (result == ESP_OK) {
        *info = s_info;
        io_expander_service_unlock();
    }

    return result;
}
