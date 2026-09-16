/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Current CAN termination-control state.
 */
typedef struct
{
    bool initialized;
    bool primary_enabled;
    bool secondary_enabled;

} can_termination_service_info_t;

/**
 * @brief Initialize both MCP23017 CAN termination-control outputs.
 *
 * Both termination resistors are forced off before their expander pins are
 * changed from reset-default inputs to outputs. Unassigned MCP23017 pins are
 * not modified.
 *
 * The MCP23017 driver must already be initialized.
 */
esp_err_t can_termination_service_init(void);

/**
 * @brief Disable both termination resistors and return their pins to inputs.
 */
esp_err_t can_termination_service_deinit(void);

/**
 * @brief Enable or disable one CAN termination resistor.
 *
 * @param[in] bus Primary or secondary CAN interface.
 * @param[in] enabled True to connect the external 120-ohm resistor.
 */
esp_err_t can_termination_service_set_enabled(
    can_bus_t bus,
    bool enabled
);

/**
 * @brief Copy current CAN termination-control state.
 */
esp_err_t can_termination_service_get_info(
    can_termination_service_info_t *info
);

#ifdef __cplusplus
}
#endif
