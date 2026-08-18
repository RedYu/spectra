#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

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
 * can_fd_service_stop().
 */
typedef void (*can_fd_service_receive_cb_t)(
    const can_fd_mcp2518fd_frame_t *frame,
    void *context
);

/**
 * @brief Callback invoked for every confirmed transmission.
 *
 * Successful transmission events are obtained from the MCP2518FD
 * Transmit Event FIFO. The sequence value can be matched with the value
 * returned by can_fd_service_transmit_tracked().
 *
 * The callback runs in the CAN FD service task context. The event
 * pointer remains valid only during the callback.
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
     * Frames delivered to the receive callback.
     */
    uint32_t delivered_rx_frames;

    /**
     * TX events delivered to the confirmation callback.
     */
    uint32_t delivered_tx_confirmations;

    /**
     * RX frames consumed while no receive callback was configured.
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
 * unchanged.
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
 * @brief Queue a tracked Classical CAN or CAN FD frame.
 *
 * The returned sequence can be matched with a later TX confirmation
 * callback.
 */
esp_err_t can_fd_service_transmit_tracked(
    const can_fd_mcp2518fd_frame_t *frame,
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

#ifdef __cplusplus
}
#endif
