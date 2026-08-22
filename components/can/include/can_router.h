#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_ROUTER_BUS_MASK(bus) \
    (1UL << (uint32_t)(bus))

#define CAN_ROUTER_EVENT_MASK(type) \
    (1UL << (uint32_t)(type))

#define CAN_ROUTER_ALL_BUSES_MASK \
    ((1UL << (uint32_t)CAN_BUS_COUNT) - 1UL)

#define CAN_ROUTER_ALL_EVENTS_MASK \
    ((1UL << (uint32_t)CAN_EVENT_TYPE_COUNT) - 1UL)

#define CAN_ROUTER_SUBSCRIPTION_ID_NONE  (0U)

/**
 * @file can_router.h
 * @brief Shared routing service for all CAN interfaces.
 *
 * The router receives shared frames from the primary TWAI and
 * secondary MCP2518FD services, converts them into can_event_t objects
 * and distributes them to registered consumers.
 *
 * Monitoring, logging, ISO-TP, UDS, XCP and streaming components should
 * consume CAN traffic through this service instead of subscribing
 * directly to hardware-specific CAN services.
 */

/**
 * @brief Router event callback.
 * 
 * The callback runs in the CAN router task context. The event pointer
 * remains valid only during the callback.
 *
 * The callback must complete quickly. Consumers performing blocking
 * work should copy the event into their own queue.
 *
 * The callback must not call can_router_stop(),
 * can_router_subscribe() or can_router_unsubscribe().
 */
typedef void (*can_router_event_cb_t)(
    const can_event_t *event,
    void *context
);

/**
 * @brief CAN router configuration.
 */
typedef struct
{
    /**
     * Maximum number of pending ingress events.
     */
    uint32_t queue_depth;

    /**
     * Maximum number of registered event subscribers.
     */
    uint32_t subscriber_capacity;

    /**
     * Maximum number of tracked transmissions awaiting completion.
     */
    uint32_t pending_tx_capacity;

} can_router_config_t;

/**
 * @brief Event subscription configuration.
 */
typedef struct
{
    /**
     * Combination of CAN_ROUTER_BUS_MASK() values.
     */
    uint32_t bus_mask;

    /**
     * Combination of CAN_ROUTER_EVENT_MASK() values.
     */
    uint32_t event_mask;

    /**
     * Event callback.
     */
    can_router_event_cb_t callback;

    /**
     * Optional callback context.
     */
    void *context;

} can_router_subscription_t;

/**
 * @brief CAN router runtime statistics.
 */
typedef struct
{
    uint32_t queue_current;
    uint32_t queue_peak;
    uint32_t queue_capacity;

    uint32_t pending_tx_current;
    uint32_t pending_tx_peak;
    uint32_t pending_tx_capacity;

    uint32_t registered_subscribers;
    uint32_t subscriber_capacity;

    uint32_t received_frames;
    uint32_t transmitted_frames;

    uint32_t completed_transmissions;
    uint32_t failed_transmissions;
    uint32_t aborted_transmissions;

    uint32_t dispatched_events;
    uint32_t dropped_events;
    uint32_t unmatched_tx_confirmations;

} can_router_statistics_t;

/**
 * @brief Start the CAN router.
 *
 * The router should be started before the primary and secondary CAN
 * services begin delivering common receive callbacks.
 *
 * @param[in] config Router configuration.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for an invalid
 * configuration, ESP_ERR_INVALID_STATE if already running,
 * ESP_ERR_NO_MEM if resources cannot be created, otherwise an
 * ESP-IDF error code.
 */
esp_err_t can_router_start(
    const can_router_config_t *config
);

/**
 * @brief Stop the CAN router and release its resources.
 *
 * Primary and secondary CAN services must be stopped before the router
 * is stopped. This prevents their receive and confirmation callbacks
 * from accessing router resources after they are released.
 *
 * Pending ingress events and tracked transmissions are discarded.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not running,
 * ESP_ERR_TIMEOUT if the router task does not terminate in time,
 * otherwise an ESP-IDF error code.
 */
esp_err_t can_router_stop(void);

/**
 * @brief Register an event subscriber.
 *
 * @param[in] subscription Subscription configuration.
 * @param[out] subscription_id Assigned non-zero subscription ID.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid
 * arguments, ESP_ERR_INVALID_STATE if the router is not running,
 * or ESP_ERR_NO_MEM when no subscription slot is available.
 */
esp_err_t can_router_subscribe(
    const can_router_subscription_t *subscription,
    uint32_t *subscription_id
);

/**
 * @brief Remove an event subscriber.
 *
 * An event callback already copied into the router dispatch snapshot
 * may still execute while this function is running. The subscriber
 * context must remain valid until the caller knows that no callback is
 * in progress.
 * 
 * @param[in] subscription_id Subscription ID returned by
 * can_router_subscribe().
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if the ID is zero,
 * ESP_ERR_NOT_FOUND if it does not exist, or ESP_ERR_INVALID_STATE if
 * the router is not running.
 */
esp_err_t can_router_unsubscribe(
    uint32_t subscription_id
);

/**
 * @brief Common receive callback for CAN services.
 *
 * Configure this function as common_receive_callback in both
 * can_service_config_t and can_fd_service_config_t.
 *
 * The frame is copied into the router queue. The context argument is
 * unused and may be NULL.
 */
void can_router_receive_callback(
    const can_frame_t *frame,
    void *context
);

/**
 * @brief Transmit a shared CAN frame through its selected bus.
 *
 * CAN_BUS_PRIMARY is routed through the TWAI service.
 * CAN_BUS_SECONDARY is routed through the MCP2518FD service.
 *
 * A successful call creates a CAN_EVENT_TX_QUEUED event. Later driver
 * confirmation creates CAN_EVENT_TX_COMPLETED, CAN_EVENT_TX_FAILED or
 * CAN_EVENT_TX_ABORTED with the same transaction ID.
 *
 * @param[in] frame Frame to transmit.
 * @param[in] timeout_ms Maximum time to wait for a hardware TX slot.
 * @param[out] transaction_id Assigned non-zero router transaction ID.
 *
 * @return ESP_OK when queued, ESP_ERR_INVALID_ARG for invalid
 * arguments, ESP_ERR_INVALID_STATE if the router or selected CAN
 * service is not running, ESP_ERR_NO_MEM when the pending-transmission
 * table is full, ESP_ERR_TIMEOUT on timeout, otherwise an ESP-IDF
 * error code.
 */
esp_err_t can_router_transmit(
    const can_frame_t *frame,
    uint32_t timeout_ms,
    uint32_t *transaction_id
);

/**
 * @brief Report a hardware transmission result to the router.
 *
 * This function is intended for the TWAI and MCP2518FD confirmation
 * adapters. The bus and native sequence identify the corresponding
 * pending router transmission.
 * 
 * This function may be called from a CAN service task callback. It
 * must not wait indefinitely and must not invoke subscriber callbacks
 * directly. The resulting event is copied into the router queue.
 *
 * @param[in] bus Source CAN interface.
 * @param[in] native_sequence Driver-specific transmission sequence.
 * @param[in] event_type CAN_EVENT_TX_COMPLETED,
 * CAN_EVENT_TX_FAILED or CAN_EVENT_TX_ABORTED.
 * @param[in] result Transmission result.
 * @param[in] timestamp_us Optional confirmation timestamp.
 * @param[in] timestamp_source Timestamp source.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid fields,
 * ESP_ERR_NOT_FOUND when no pending transmission matches, or
 * ESP_ERR_INVALID_STATE if the router is not running.
 */
esp_err_t can_router_report_tx_result(
    can_bus_id_t bus,
    uint32_t native_sequence,
    can_event_type_t event_type,
    esp_err_t result,
    uint64_t timestamp_us,
    can_timestamp_source_t timestamp_source
);

/**
 * @brief Copy current router statistics.
 */
esp_err_t can_router_get_statistics(
    can_router_statistics_t *statistics
);

/**
 * @brief Reset cumulative router statistics.
 *
 * Current queue, capacity, subscriber and pending-transmission values
 * are not reset.
 */
esp_err_t can_router_reset_statistics(void);

/**
 * @brief Check whether the CAN router is running.
 */
bool can_router_is_running(void);

#ifdef __cplusplus
}
#endif
