/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include "can_service.h"
#include "can_fd_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file can_router_service_adapter.h
 * @brief Integration callbacks between CAN services and CAN router.
 */

/**
 * @brief Forward a primary TWAI transmission confirmation.
 *
 * Configure this function as can_service_config_t::
 * tx_confirmation_callback.
 */
void can_router_twai_tx_confirmation_callback(
    const can_twai_tx_confirmation_t *confirmation,
    void *context
);

/**
 * @brief Forward a secondary MCP2518FD TEF confirmation.
 *
 * Configure this function as can_fd_service_config_t::
 * tx_confirmation_callback.
 */
void can_router_mcp2518fd_tx_confirmation_callback(
    const can_fd_mcp2518fd_tx_event_t *event,
    void *context
);

#ifdef __cplusplus
}
#endif
