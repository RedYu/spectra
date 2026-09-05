/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include "esp_err.h"

#include "can_fd_mcp2518fd_driver.h"
#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file can_mcp2518fd_frame_adapter.h
 * @brief Conversion between MCP2518FD and shared CAN frames.
 */

/**
 * @brief Convert an MCP2518FD frame to the shared frame format.
 *
 * The resulting frame belongs to CAN_BUS_SECONDARY. Valid MCP2518FD
 * timestamps are marked as hardware timestamps.
 *
 * @param[in] source MCP2518FD driver frame.
 * @param[out] destination Shared CAN frame.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid pointers
 * or frame fields, or ESP_ERR_INVALID_SIZE for an unsupported payload
 * length.
 */
esp_err_t can_mcp2518fd_frame_to_common(
    const can_fd_mcp2518fd_frame_t *source,
    can_frame_t *destination
);

/**
 * @brief Convert a shared frame to an MCP2518FD transmission frame.
 *
 * Only frames assigned to CAN_BUS_SECONDARY are accepted. Classical
 * CAN and CAN FD frames are supported.
 *
 * The MCP2518FD timestamp is cleared because hardware timestamps are
 * assigned by the controller.
 *
 * @param[in] source Shared CAN frame.
 * @param[out] destination MCP2518FD driver frame.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid pointers,
 * fields or an incompatible bus, or ESP_ERR_INVALID_SIZE for an
 * unsupported DLC or payload length.
 */
esp_err_t can_mcp2518fd_frame_from_common(
    const can_frame_t *source,
    can_fd_mcp2518fd_frame_t *destination
);

#ifdef __cplusplus
}
#endif
