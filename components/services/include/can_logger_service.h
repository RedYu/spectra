/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file can_logger_service.h
 * @brief Asynchronous CAN event logger service.
 *
 * The logger subscribes to the central CAN router and transfers accepted
 * events through a bounded queue to a dedicated storage writer task. Router
 * delivery is never blocked by filesystem operations.
 */

#define CAN_LOGGER_FILE_PATH_MAX_LENGTH  (256U)

/**
 * @brief Supported CAN log file formats.
 */
typedef enum
{
    /**
     * Vector ASCII log format.
     */
    CAN_LOGGER_FORMAT_ASC = 0,

    /**
     * Native binary Spectra CAN Log format.
     */
    CAN_LOGGER_FORMAT_SCL,

    CAN_LOGGER_FORMAT_COUNT,

} can_logger_format_t;

/**
 * @brief Logger recording state.
 */
typedef enum
{
    /**
     * The service is running but no recording is active.
     */
    CAN_LOGGER_STATE_IDLE = 0,

    /**
     * A log file is being opened and initialized.
     */
    CAN_LOGGER_STATE_STARTING,

    /**
     * CAN events are being written to the log file.
     */
    CAN_LOGGER_STATE_RECORDING,

    /**
     * Pending events are being flushed and the file is being closed.
     */
    CAN_LOGGER_STATE_STOPPING,

    /**
     * Recording stopped because an unrecoverable error occurred.
     */
    CAN_LOGGER_STATE_ERROR,

    CAN_LOGGER_STATE_COUNT,

} can_logger_state_t;

/**
 * @brief CAN event selection for one recording session.
 */
typedef struct
{
    /**
     * Record events from the Primary TWAI interface.
     */
    bool primary;

    /**
     * Record events from the Secondary MCP2518FD interface.
     */
    bool secondary;

    /**
     * Record received-frame events.
     */
    bool rx;

    /**
     * Record transmit lifecycle events.
     */
    bool tx;

} can_logger_filter_t;

/**
 * @brief Configuration for a new CAN recording session.
 */
typedef struct
{
    /**
     * Output file format.
     */
    can_logger_format_t format;

    /**
     * Event selection applied before events enter the logger queue.
     */
    can_logger_filter_t filter;

    /**
     * Absolute destination path below a logger-supported storage mount.
     *
     * The path must refer to a new regular file. The service does not
     * overwrite an existing file.
     */
    char file_path[
        CAN_LOGGER_FILE_PATH_MAX_LENGTH
    ];

} can_logger_recording_config_t;

/**
 * @brief Cumulative logger statistics for the current service run.
 */
typedef struct
{
    /**
     * Router events inspected by the logger subscriber.
     */
    uint64_t received_events;

    /**
     * Events excluded by the active recording filter.
     */
    uint64_t filtered_events;

    /**
     * Events accepted into the logger queue.
     */
    uint64_t queued_events;

    /**
     * Events rejected because the logger queue was full.
     */
    uint64_t dropped_events;

    /**
     * Events successfully serialized into the active log.
     */
    uint64_t written_events;

    /**
     * Bytes successfully written to the active log file.
     */
    uint64_t written_bytes;

    /**
     * Failed serialization operations.
     */
    uint64_t serialization_failures;

    /**
     * Failed filesystem write operations.
     */
    uint64_t write_failures;

    /**
     * Failed file synchronization operations.
     */
    uint64_t sync_failures;

    /**
     * Current number of events waiting in the logger queue.
     */
    size_t queue_current;

    /**
     * Highest observed logger queue occupancy.
     */
    size_t queue_peak;

    /**
     * Maximum number of events accepted by the logger queue.
     */
    size_t queue_capacity;

} can_logger_statistics_t;

/**
 * @brief Current logger state and active-session information.
 */
typedef struct
{
    /**
     * True when the logger service task and router subscription are active.
     */
    bool service_running;

    /**
     * Current recording state.
     */
    can_logger_state_t state;

    /**
     * Active format, or CAN_LOGGER_FORMAT_ASC while no session is active.
     */
    can_logger_format_t format;

    /**
     * Filters used by the active or most recently completed session.
     */
    can_logger_filter_t filter;

    /**
     * Active output path, or an empty string when no file is open.
     */
    char file_path[
        CAN_LOGGER_FILE_PATH_MAX_LENGTH
    ];

    /**
     * Microsecond timestamp at which the active session started.
     *
     * Zero means that no session is active.
     */
    uint64_t started_at_us;

    /**
     * Duration of the active session in milliseconds.
     *
     * Zero means that no session is active.
     */
    uint64_t duration_ms;

    /**
     * Last service or filesystem error, or ESP_OK when none is pending.
     */
    esp_err_t last_error;

    /**
     * Current cumulative service statistics.
     */
    can_logger_statistics_t statistics;

} can_logger_info_t;

/**
 * @brief Start the asynchronous CAN logger service.
 *
 * Creates the logger queue and writer task and subscribes to CAN router
 * events. No file is opened until can_logger_service_start_recording() is
 * called.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is already
 * running or a required dependency is unavailable, ESP_ERR_NO_MEM if a
 * required resource cannot be allocated, otherwise an ESP-IDF error code.
 */
esp_err_t can_logger_service_start(void);

/**
 * @brief Stop the logger service and release its resources.
 *
 * If recording is active, the service first stops accepting new events,
 * drains its queue, synchronizes and closes the file, and then removes its CAN
 * router subscription. The function waits for writer-task acknowledgement.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is not
 * running, ESP_ERR_TIMEOUT if the task does not stop within the configured
 * timeout, or the first relevant filesystem/service error.
 */
esp_err_t can_logger_service_stop(void);

/**
 * @brief Start a new CAN recording session.
 *
 * Validates the configuration and requests asynchronous file creation. The
 * state changes through CAN_LOGGER_STATE_STARTING to
 * CAN_LOGGER_STATE_RECORDING when the writer has opened the file.
 *
 * At least one bus and one direction must be selected. The destination path
 * is copied by the service before this function returns.
 *
 * @param[in] config Recording configuration.
 *
 * @return ESP_OK when the start request is accepted, ESP_ERR_INVALID_ARG for
 * an invalid configuration, ESP_ERR_INVALID_STATE if the service is not
 * running or a recording is already active, ESP_ERR_TIMEOUT if the command
 * queue is full, otherwise an ESP-IDF error code.
 */
esp_err_t can_logger_service_start_recording(
    const can_logger_recording_config_t *config
);

/**
 * @brief Stop the active recording gracefully.
 *
 * Stops accepting new events and requests the writer task to drain queued
 * events, synchronize the file, and close it. The service remains running and
 * can start another recording later.
 *
 * @return ESP_OK when the stop request is accepted, ESP_ERR_INVALID_STATE if
 * the service is not recording, ESP_ERR_TIMEOUT if the command queue is full,
 * otherwise an ESP-IDF error code.
 */
esp_err_t can_logger_service_stop_recording(void);

/**
 * @brief Wait until the current recording reaches a terminal state.
 *
 * This function can be used by shutdown processing after requesting a stop.
 * It returns when the state becomes IDLE or ERROR.
 *
 * @param[in] timeout_ms Maximum time to wait in milliseconds. Zero performs a
 * non-blocking state check.
 *
 * @return ESP_OK when the recording is fully stopped, ESP_ERR_INVALID_STATE if
 * the service is not running, ESP_ERR_TIMEOUT if the timeout expires, or the
 * session error when the logger reaches CAN_LOGGER_STATE_ERROR.
 */
esp_err_t can_logger_service_wait_stopped(
    uint32_t timeout_ms
);

/**
 * @brief Get a consistent snapshot of logger state and statistics.
 *
 * @param[out] info Destination information structure.
 *
 * @return ESP_OK on success or ESP_ERR_INVALID_ARG if info is NULL.
 */
esp_err_t can_logger_service_get_info(
    can_logger_info_t *info
);

/**
 * @brief Reset cumulative logger statistics.
 *
 * Queue occupancy is recalculated from the live queue and cannot be forced to
 * zero while events are pending. Session state, path, and last_error are not
 * changed.
 *
 * @return ESP_OK on success or ESP_ERR_INVALID_STATE if the service is not
 * running.
 */
esp_err_t can_logger_service_reset_statistics(void);

/**
 * @brief Check whether the CAN logger service is running.
 */
bool can_logger_service_is_running(void);

/**
 * @brief Check whether a recording is starting, active, or stopping.
 */
bool can_logger_service_is_recording(void);

#ifdef __cplusplus
}
#endif
