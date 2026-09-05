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
 * @file can_monitor_service.h
 * @brief Runtime monitoring for shared CAN traffic.
 *
 * The service subscribes to can_router events and maintains:
 *
 * - aggregate traffic statistics;
 * - per-bus statistics;
 * - recently observed CAN events;
 * - per-identifier statistics and the latest frame.
 *
 * Router callbacks only copy events into a non-blocking service queue.
 * Monitoring data is processed by a dedicated task.
 */

/**
 * @brief CAN monitor configuration.
 */
typedef struct
{
    /**
     * Number of router events waiting for monitor processing.
     *
     * Queue storage should remain reasonably small because complete
     * can_event_t objects are copied into it.
     */
    uint32_t queue_depth;

    /**
     * Number of recent events retained in chronological history.
     *
     * History storage may be allocated in PSRAM.
     */
    uint32_t history_capacity;

    /**
     * Maximum number of unique bus/identifier combinations tracked.
     *
     * Identifier-table storage may be allocated in PSRAM.
     */
    uint32_t identifier_capacity;

} can_monitor_service_config_t;

/**
 * @brief Statistics for one CAN interface.
 */
typedef struct
{
    uint64_t received_frames;
    uint64_t queued_transmissions;
    uint64_t completed_transmissions;
    uint64_t failed_transmissions;
    uint64_t aborted_transmissions;

    uint64_t received_bytes;
    uint64_t transmitted_bytes;

    uint64_t first_event_timestamp_us;
    uint64_t last_event_timestamp_us;

} can_monitor_bus_statistics_t;

/**
 * @brief Aggregate CAN monitor statistics.
 */
typedef struct
{
    can_monitor_bus_statistics_t buses[
        CAN_BUS_COUNT
    ];

    uint64_t processed_events;
    uint64_t dropped_input_events;

    uint32_t input_queue_current;
    uint32_t input_queue_peak;
    uint32_t input_queue_capacity;

    uint32_t history_current;
    uint32_t history_capacity;

    uint32_t tracked_identifiers;
    uint32_t identifier_capacity;

} can_monitor_service_statistics_t;

/**
 * @brief Direction of the latest CAN traffic event.
 */
typedef enum
{
    CAN_MONITOR_DIRECTION_NONE = 0,
    CAN_MONITOR_DIRECTION_RX,
    CAN_MONITOR_DIRECTION_TX,

} can_monitor_direction_t;

/**
 * @brief Statistics for one observed CAN identifier.
 *
 * Standard and extended identifiers with the same numeric value are
 * tracked separately.
 */
typedef struct
{
    can_bus_id_t bus;

    uint32_t identifier;

    bool extended;

    uint64_t received_frames;
    uint64_t transmitted_frames;

    uint64_t received_bytes;
    uint64_t transmitted_bytes;

    uint64_t first_seen_timestamp_us;
    uint64_t last_seen_timestamp_us;

    /**
     * Direction of the latest frame observed for this identifier.
     */
    can_monitor_direction_t last_direction;

    /**
     * Local monotonic time when the latest event was processed.
     *
     * Unlike the frame timestamp, this value always uses the ESP timer
     * time base and can therefore be used to calculate event age.
     */
    uint64_t last_activity_timestamp_us;

    /**
     * Last RX or TX frame observed for this identifier.
     */
    can_frame_t last_frame;

} can_monitor_identifier_info_t;

/**
 * @brief Start CAN traffic monitoring.
 *
 * The CAN router must already be running. The service creates its
 * processing queue and task, then subscribes to all CAN router events.
 *
 * @param[in] config Monitor configuration.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid
 * configuration, ESP_ERR_INVALID_STATE if already running or the
 * CAN router is unavailable, ESP_ERR_NO_MEM when resources cannot be
 * allocated, otherwise an ESP-IDF error code.
 */
esp_err_t can_monitor_service_start(
    const can_monitor_service_config_t *config
);

/**
 * @brief Stop CAN traffic monitoring.
 *
 * The service first unsubscribes from the CAN router, then waits for
 * its processing task to terminate and releases monitor resources.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not running,
 * ESP_ERR_TIMEOUT if the task does not stop in time, otherwise an
 * ESP-IDF error code.
 */
esp_err_t can_monitor_service_stop(void);

/**
 * @brief Copy aggregate monitor statistics.
 *
 * @param[out] statistics Destination statistics.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if statistics is
 * NULL, ESP_ERR_INVALID_STATE if the service is not running, otherwise
 * an ESP-IDF error code.
 */
esp_err_t can_monitor_service_get_statistics(
    can_monitor_service_statistics_t *statistics
);

/**
 * @brief Copy the most recent CAN events.
 *
 * Events are returned from newest to oldest. At most capacity events
 * are copied.
 *
 * @param[out] events Destination event array.
 * @param[in] capacity Number of entries available in events.
 * @param[out] count Number of events copied.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid
 * arguments, ESP_ERR_INVALID_STATE if the service is not running,
 * otherwise an ESP-IDF error code.
 */
esp_err_t can_monitor_service_get_recent_events(
    can_event_t *events,
    size_t capacity,
    size_t *count
);

/**
 * @brief Copy information about tracked CAN identifiers.
 *
 * Entries are returned in implementation-defined order. This API is
 * intended for snapshots used by GUI and Web consumers.
 *
 * @param[out] identifiers Destination array.
 * @param[in] capacity Number of available destination entries.
 * @param[out] count Number of entries copied.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid
 * arguments, ESP_ERR_INVALID_STATE if the service is not running,
 * otherwise an ESP-IDF error code.
 */
esp_err_t can_monitor_service_get_identifiers(
    can_monitor_identifier_info_t *identifiers,
    size_t capacity,
    size_t *count
);

/**
 * @brief Clear all accumulated CAN monitoring data.
 *
 * Discards pending input events, clears event history, identifier
 * statistics, bus statistics, dropped-event counters and queue peak
 * usage. Events received concurrently after the queue reset are
 * treated as new events.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not running, otherwise an ESP-IDF error code.
 */
esp_err_t can_monitor_service_clear(void);

/**
 * @brief Check whether CAN monitoring is running.
 */
bool can_monitor_service_is_running(void);

#ifdef __cplusplus
}
#endif
