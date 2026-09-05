/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "can_frame.h"
#include "can_fd_mcp2518fd_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file can_fd_service.h
 * @brief Secondary Classical CAN and CAN FD service.
 *
 * The service owns the MCP2518FD driver, processes received frames and
 * Transmit Event FIFO confirmations in a dedicated FreeRTOS task, and
 * forwards them to application callbacks.
 *
 * All MCP2518FD driver access should be performed through this service
 * while it is running. The underlying driver serializes SPI
 * transactions and runtime reconfiguration using its internal mutex.
 */

/**
 * @brief Callback invoked for every received Classical CAN or CAN FD
 * frame.
 *
 * The callback runs in the CAN FD service task context, not in an ISR.
 * The frame pointer remains valid only during the callback.
 *
 * The callback should complete quickly and must not call
 * can_fd_service_stop() or can_fd_service_reconfigure().
 */
typedef void (*can_fd_service_receive_cb_t)(
    const can_fd_mcp2518fd_frame_t *frame,
    void *context
);

/**
 * @brief Callback invoked for every received shared CAN frame.
 *
 * The callback runs in the CAN FD service task context. The frame
 * pointer remains valid only during the callback.
 *
 * The callback must complete quickly and must not call
 * can_fd_service_stop() or can_fd_service_reconfigure().
 */
typedef void (*can_fd_service_common_receive_cb_t)(
    const can_frame_t *frame,
    void *context
);

/**
 * @brief Callback invoked for every confirmed transmission.
 *
 * Successful transmission events are obtained from the MCP2518FD
 * Transmit Event FIFO. The sequence value can be matched with the value
 * returned by can_fd_service_transmit_tracked() or
 * can_fd_service_transmit_common_tracked().
 *
 * The callback runs in the CAN FD service task context. The event
 * pointer remains valid only during the callback.
 * 
 * The callback should complete quickly and must not call
 * can_fd_service_stop() or can_fd_service_reconfigure().
 */
typedef void (*can_fd_service_tx_confirmation_cb_t)(
    const can_fd_mcp2518fd_tx_event_t *event,
    void *context
);

/**
 * @brief CAN FD service configuration.
 */
typedef struct
{
    /**
     * Low-level MCP2518FD configuration.
     */
    can_fd_mcp2518fd_config_t driver;

    /**
     * Optional received-frame callback.
     */
    can_fd_service_receive_cb_t receive_callback;

    /**
     * Optional received-frame callback context.
     */
    void *receive_context;

    /**
     * Optional shared-frame receive callback.
     *
     * This callback is intended for can_router and new application
     * components. When both receive callbacks are configured, the
     * legacy receive_callback is invoked first, followed by this
     * callback.
     */
    can_fd_service_common_receive_cb_t
        common_receive_callback;

    /**
     * Optional shared-frame receive callback context.
     */
    void *common_receive_context;

    /**
     * Optional successful-transmission callback.
     */
    can_fd_service_tx_confirmation_cb_t
        tx_confirmation_callback;

    /**
     * Optional transmission-confirmation callback context.
     */
    void *tx_confirmation_context;

} can_fd_service_config_t;

/**
 * @brief CAN FD service queue and callback statistics.
 */
typedef struct
{
    /**
     * Physical RX frames delivered to at least one receive callback.
     *
     * A frame is counted once when one or both legacy and shared-frame
     * callbacks are configured.
     */
    uint32_t delivered_rx_frames;

    /**
     * TX events delivered to the confirmation callback.
     */
    uint32_t delivered_tx_confirmations;

    /**
     * RX frames consumed while neither the legacy nor shared-frame
     * receive callback was configured.
     */
    uint32_t unhandled_rx_frames;

    /**
     * TX events consumed while no confirmation callback was configured.
     */
    uint32_t unhandled_tx_confirmations;

    /**
     * Service receive-loop errors, excluding normal timeouts.
     */
    uint32_t receive_errors;

    /**
     * Service TEF processing errors, excluding empty-queue timeouts.
     */
    uint32_t tx_event_errors;

} can_fd_service_statistics_t;

/**
 * @brief Start the secondary CAN FD service.
 *
 * Initializes and starts the MCP2518FD driver and creates the service
 * processing task.
 *
 * @param[in] config Service configuration.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if config is NULL,
 * ESP_ERR_INVALID_STATE if the service is already running,
 * ESP_ERR_NO_MEM if required resources cannot be created, otherwise an
 * ESP-IDF error code.
 */
esp_err_t can_fd_service_start(
    const can_fd_service_config_t *config
);

/**
 * @brief Run a blocking MCP2518FD internal-loopback self-test.
 *
 * The secondary CAN service must not already be running. The test
 * verifies frame transmission, loopback reception and the matching TEF
 * confirmation sequence.
 *
 * Internal loopback keeps TXCAN recessive and does not transmit the
 * test frame onto the physical CAN bus.
 *
 * @param[in] nominal_bitrate Classical CAN bitrate used by the test.
 * @param[in] timeout_ms Maximum time to wait for RX and TEF events.
 *
 * @return ESP_OK when all checks pass, otherwise an ESP-IDF error code.
 */
esp_err_t can_fd_service_run_self_test(
    uint32_t nominal_bitrate,
    uint32_t timeout_ms
);

/**
 * @brief Stop the secondary CAN FD service.
 *
 * Stops the processing task, aborts pending transmissions, stops and
 * deinitializes the MCP2518FD driver and releases task-specific
 * resources. The service API synchronization mutex remains allocated
 * so the service can be started again.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not running, ESP_ERR_TIMEOUT if the processing task does not stop in
 * time, otherwise an ESP-IDF error code.
 */
esp_err_t can_fd_service_stop(void);

/**
 * @brief Reconfigure the running MCP2518FD controller.
 *
 * Reception and transmission are temporarily interrupted while the
 * new runtime configuration is applied. Application callbacks remain
 * unchanged. This function must not be called from any CAN FD service 
 * callback.
 *
 * @param[in] config New runtime configuration.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if config is NULL,
 * ESP_ERR_INVALID_STATE if the service is not running, otherwise an
 * ESP-IDF error code.
 */
esp_err_t can_fd_service_reconfigure(
    const can_fd_mcp2518fd_runtime_config_t *config
);

/**
 * @brief Queue an untracked Classical CAN or CAN FD frame.
 *
 * Successful return means that the frame was placed into the hardware
 * TX FIFO, not necessarily transmitted on the CAN bus.
 */
esp_err_t can_fd_service_transmit(
    const can_fd_mcp2518fd_frame_t *frame,
    uint32_t timeout_ms
);

/**
 * @brief Queue one shared Classical CAN or CAN FD frame.
 *
 * The frame must belong to CAN_BUS_SECONDARY. It is converted to the
 * MCP2518FD driver format before being placed into the hardware TX
 * FIFO.
 *
 * Successful return does not guarantee that the frame was transmitted
 * or acknowledged on the CAN bus.
 *
 * @param[in] frame Shared CAN frame.
 * @param[in] timeout_ms Maximum time to wait for a TX FIFO slot.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for an invalid frame
 * or incompatible bus, ESP_ERR_INVALID_SIZE for an unsupported payload
 * length, ESP_ERR_INVALID_STATE if the service is not running,
 * ESP_ERR_TIMEOUT on timeout, otherwise an ESP-IDF error code.
 */
esp_err_t can_fd_service_transmit_common(
    const can_frame_t *frame,
    uint32_t timeout_ms
);

/**
 * @brief Queue a tracked Classical CAN or CAN FD frame.
 *
 * The returned sequence can be matched with a later TX confirmation
 * callback. Zero is a valid MCP2518FD hardware sequence value.
 */
esp_err_t can_fd_service_transmit_tracked(
    const can_fd_mcp2518fd_frame_t *frame,
    uint32_t timeout_ms,
    uint32_t *sequence
);

/**
 * @brief Queue one tracked shared Classical CAN or CAN FD frame.
 *
 * The returned native sequence can be matched with a later MCP2518FD
 * TX confirmation event.
 *
 * @param[in] frame Shared CAN frame.
 * @param[in] timeout_ms Maximum time to wait for a TX FIFO slot.
 * @param[out] sequence Assigned MCP2518FD sequence. Zero is a valid
 * hardware sequence value.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid
 * arguments or an incompatible bus, ESP_ERR_INVALID_SIZE for an
 * unsupported payload length, ESP_ERR_INVALID_STATE if the service is
 * not running, ESP_ERR_TIMEOUT on timeout, otherwise an ESP-IDF error
 * code.
 */
esp_err_t can_fd_service_transmit_common_tracked(
    const can_frame_t *frame,
    uint32_t timeout_ms,
    uint32_t *sequence
);

/**
 * @brief Abort all pending transmissions.
 */
esp_err_t can_fd_service_abort_transmissions(void);

/**
 * @brief Synchronize the service with automatic Bus-off recovery.
 */
esp_err_t can_fd_service_recover(void);

/**
 * @brief Configure and enable one MCP2518FD acceptance filter.
 */
esp_err_t can_fd_service_set_filter(
    const can_fd_mcp2518fd_filter_t *filter
);

/**
 * @brief Disable one MCP2518FD acceptance filter.
 */
esp_err_t can_fd_service_disable_filter(
    uint8_t filter_index
);

/**
 * @brief Disable all MCP2518FD acceptance filters.
 */
esp_err_t can_fd_service_disable_all_filters(void);

/**
 * @brief Configure the MCP2518FD to accept all frames.
 */
esp_err_t can_fd_service_accept_all(void);

/**
 * @brief Copy current MCP2518FD runtime information.
 */
esp_err_t can_fd_service_get_info(
    can_fd_mcp2518fd_info_t *info
);

/**
 * @brief Copy service-level statistics.
 */
esp_err_t can_fd_service_get_statistics(
    can_fd_service_statistics_t *statistics
);

/**
 * @brief Reset service-level statistics.
 */
esp_err_t can_fd_service_reset_statistics(void);

/**
 * @brief Check whether the secondary CAN FD service is running.
 */
bool can_fd_service_is_running(void);

/**
 * @brief Copy MCP2518FD driver performance measurements.
 *
 * @param[out] profile Destination profile structure.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if profile is NULL,
 * ESP_ERR_INVALID_STATE if the service is not running,
 * ESP_ERR_NOT_SUPPORTED if driver profiling is disabled, otherwise
 * an ESP-IDF driver error.
 */
esp_err_t can_fd_service_get_driver_profile(
    can_fd_mcp2518fd_profile_t *profile
);

#ifdef __cplusplus
}
#endif
