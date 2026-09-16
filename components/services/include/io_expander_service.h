/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Named MCP23017 output assignments.
 *
 * Reserved signals remain inputs until their owning service explicitly
 * configures them as outputs.
 */
typedef enum
{
    IO_EXPANDER_OUTPUT_CAN_PRIMARY_TERMINATION = 0,
    IO_EXPANDER_OUTPUT_CAN_SECONDARY_TERMINATION,
    IO_EXPANDER_OUTPUT_CAN_PRIMARY_STANDBY,
    IO_EXPANDER_OUTPUT_CAN_SECONDARY_STANDBY,
    IO_EXPANDER_OUTPUT_CAN_ROUTE_SELECT_0,
    IO_EXPANDER_OUTPUT_CAN_ROUTE_SELECT_1,
    IO_EXPANDER_OUTPUT_CAN_ROUTE_ENABLE,

    IO_EXPANDER_OUTPUT_COUNT,

} io_expander_output_t;

typedef struct
{
    bool initialized;
    uint16_t configured_outputs;
    uint16_t active_outputs;

} io_expander_service_info_t;

esp_err_t io_expander_service_init(void);
esp_err_t io_expander_service_deinit(void);

esp_err_t io_expander_service_configure_output(
    io_expander_output_t output,
    bool active
);

esp_err_t io_expander_service_release_output(
    io_expander_output_t output
);

esp_err_t io_expander_service_set_output(
    io_expander_output_t output,
    bool active
);

esp_err_t io_expander_service_get_output(
    io_expander_output_t output,
    bool *active
);

esp_err_t io_expander_service_get_info(
    io_expander_service_info_t *info
);

#ifdef __cplusplus
}
#endif
