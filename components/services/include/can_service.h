#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "can_frame.h"
#include "can_twai_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file can_service.h
 * @brief Primary Classical CAN service.
 *
 * The service owns the primary ESP32-S3 TWAI driver, receives CAN
 * frames in a dedicated task and forwards them to the configured
 * application callback.
 */

/**
 * @brief Callback invoked for every received CAN frame.
 *
 * The callback runs in the CAN service task context, not in an ISR.
 * The frame pointer remains valid only during the callback.
 *
 * The callback should complete quickly and must not call
 * can_service_stop() or can_service_reconfigure().
 *
 * @param[in] frame Received Classical CAN frame.
 * @param[in] context Optional application context.
 */
typedef void (*can_service_receive_cb_t)(
    const can_twai_frame_t *frame,
    void *context
);

/**
 * @brief Callback invoked for every received shared CAN frame.
 *
 * The callback runs in the CAN service task context, not in an ISR.
 * The frame pointer remains valid only during the callback.
 *
 * The callback should complete quickly and must not call
 * can_service_stop() or can_service_reconfigure().
 *
 * @param[in] frame Received frame converted to the shared format.
 * @param[in] context Optional application context.
 */
typedef void (*can_service_common_receive_cb_t)(
    const can_frame_t *frame,
    void *context
);

/**
 * @brief Completed CAN transmission callback.
 *
 * The callback runs in the CAN service task context and may use normal
 * FreeRTOS APIs. It must not call can_service_stop() or
 * can_service_reconfigure().
 *
 * The confirmation pointer remains valid only during the callback.
 */
typedef void (*can_service_tx_confirmation_cb_t)(
    const can_twai_tx_confirmation_t *confirmation,
    void *context
);

/**
 * @brief Primary CAN service configuration.
 */
typedef struct
{
    /**
     * Low-level TWAI driver configuration.
     */
    can_twai_driver_config_t driver;

    /**
     * Optional callback for received frames.
     */
    can_service_receive_cb_t receive_callback;

    /**
     * Optional receive-callback context.
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
    can_service_common_receive_cb_t
        common_receive_callback;

    /**
     * Optional shared-frame receive callback context.
     */
    void *common_receive_context;

    /**
     * Optional tracked-transmission completion callback.
     */
    can_service_tx_confirmation_cb_t
        tx_confirmation_callback;

    /**
     * Optional TX confirmation callback context.
     */
    void *tx_confirmation_context;

} can_service_config_t;

typedef struct
{
    uint32_t confirmation_queue_current;
    uint32_t confirmation_queue_peak;
    uint32_t confirmation_queue_capacity;

    uint32_t dropped_tx_confirmations;

} can_service_queue_statistics_t;

/**
 * @brief Start the primary CAN service.
 *
 * Initializes and starts the TWAI driver, creates the receive task and
 * begins processing CAN frames.
 *
 * @param[in] config Service configuration.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if config is NULL,
 * ESP_ERR_INVALID_STATE if the service is already running,
 * ESP_ERR_NO_MEM if resources cannot be created, otherwise an
 * ESP-IDF error code.
 */
esp_err_t can_service_start(
    const can_service_config_t *config
);

/**
 * @brief Run a blocking TWAI loopback self-test.
 *
 * The CAN service must not already be running. No other task may start
 * or stop the CAN service while the test is executing.
 *
 * @warning The test frame is also emitted through the physical TWAI TX
 * pin. Do not run this test on a live vehicle bus unless the external
 * CAN transceiver is disabled or disconnected from the bus.
 *
 * @param[in] bitrate Test CAN bitrate.
 * @param[in] timeout_ms Maximum time to wait for looped-back reception.
 *
 * @return ESP_OK when the test frame is transmitted and received,
 * ESP_ERR_INVALID_ARG if bitrate or timeout_ms is zero,
 * ESP_ERR_INVALID_STATE if the service is running or another self-test
 * remains active, ESP_ERR_NO_MEM if the synchronization semaphore
 * cannot be created, ESP_ERR_TIMEOUT when the test frame is not
 * received, otherwise an ESP-IDF error code.
 */
esp_err_t can_service_run_self_test(
    uint32_t bitrate,
    uint32_t timeout_ms
);

/**
 * @brief Reconfigure the running primary CAN interface.
 *
 * Reception and transmission are temporarily stopped while the TWAI
 * controller is recreated. Receive and transmission-confirmation 
 * callbacks remain unchanged.
 *
 * If applying the new configuration fails, the service attempts to
 * restore the previous configuration.
 *
 * This function must not be called from any CAN service callback,
 * including receive and transmission-confirmation callbacks.
 *
 * @param[in] driver_config New TWAI driver configuration.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if driver_config is
 * NULL, ESP_ERR_INVALID_STATE if the service is not running, otherwise
 * an ESP-IDF error code.
 */
esp_err_t can_service_reconfigure(
    const can_twai_driver_config_t *driver_config
);

/**
 * @brief Stop the primary CAN service.
 *
 * Stops the receive task, aborts pending transmissions, stops and
 * deinitializes the TWAI driver and releases service resources.
 *
 * The function waits for the receive task to terminate.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not running, ESP_ERR_TIMEOUT if the service task does not terminate
 * in time, otherwise an ESP-IDF error code.
 */
esp_err_t can_service_stop(void);

/**
 * @brief Queue one Classical CAN frame for transmission.
 *
 * The frame contents are copied by the TWAI driver. The caller may
 * release or modify the source frame after this function returns.
 *
 * @param[in] frame Frame to transmit.
 * @param[in] timeout_ms Maximum time to wait for an available internal
 * transmission slot.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if frame is invalid,
 * ESP_ERR_INVALID_STATE if the service is not running,
 * ESP_ERR_TIMEOUT on timeout, otherwise an ESP-IDF error code.
 */
esp_err_t can_service_transmit(
    const can_twai_frame_t *frame,
    uint32_t timeout_ms
);

/**
 * @brief Queue one shared Classical CAN frame for transmission.
 *
 * The frame must belong to CAN_BUS_PRIMARY and must not use CAN FD,
 * BRS or ESI. It is converted to the TWAI driver format before being
 * queued.
 *
 * @param[in] frame Shared CAN frame.
 * @param[in] timeout_ms Maximum time to wait for an internal
 * transmission slot.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for an invalid frame
 * or bus, ESP_ERR_NOT_SUPPORTED for CAN FD, ESP_ERR_INVALID_STATE if
 * the service is not running, ESP_ERR_TIMEOUT on timeout, otherwise
 * an ESP-IDF error code.
 */
esp_err_t can_service_transmit_common(
    const can_frame_t *frame,
    uint32_t timeout_ms
);

/**
 * @brief Queue a tracked Classical CAN frame for transmission.
 *
 * Completion is delivered through the configured TX confirmation
 * callback. The transmission context is returned unchanged in the
 * confirmation.
 *
 * @param[in] frame TWAI frame to transmit.
 * @param[in] transmission_context Optional per-transmission context.
 * @param[in] timeout_ms Maximum time to wait for a TX slot.
 * @param[out] transmission_id Assigned non-zero transmission ID.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid
 * arguments, ESP_ERR_INVALID_STATE if the service is not running,
 * ESP_ERR_TIMEOUT on timeout, otherwise an ESP-IDF error code.
 */
esp_err_t can_service_transmit_tracked(
    const can_twai_frame_t *frame,
    void *transmission_context,
    uint32_t timeout_ms,
    uint32_t *transmission_id
);

/**
 * @brief Queue a tracked shared Classical CAN frame.
 *
 * The source frame is converted to the TWAI driver format. Completion
 * is delivered through the existing service TX confirmation callback.
 *
 * transmission_context is returned unchanged in the confirmation and
 * can be used by can_router to locate its pending transmission.
 *
 * @param[in] frame Shared CAN frame.
 * @param[in] transmission_context Per-transmission context.
 * @param[in] timeout_ms Maximum time to wait for a TX slot.
 * @param[out] transmission_id Native non-zero TWAI transmission ID.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid
 * arguments, ESP_ERR_NOT_SUPPORTED for CAN FD,
 * ESP_ERR_INVALID_STATE if the service is not running,
 * ESP_ERR_TIMEOUT on timeout, otherwise an ESP-IDF error code.
 */
esp_err_t can_service_transmit_common_tracked(
    const can_frame_t *frame,
    void *transmission_context,
    uint32_t timeout_ms,
    uint32_t *transmission_id
);

/**
 * @brief Request recovery after the CAN controller enters bus-off.
 *
 * Recovery is asynchronous. Its current state can be inspected using
 * can_service_get_info().
 *
 * @return ESP_OK when recovery starts, ESP_ERR_INVALID_STATE if the
 * service is not running or the controller is not in bus-off,
 * otherwise an ESP-IDF error code.
 */
esp_err_t can_service_recover(void);

/**
 * @brief Copy current primary CAN runtime information.
 *
 * @param[out] info Destination information structure.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if info is NULL,
 * ESP_ERR_INVALID_STATE if the service is not running, otherwise an
 * ESP-IDF error code.
 */
esp_err_t can_service_get_info(
    can_twai_driver_info_t *info
);

/**
 * @brief Copy TX confirmation queue statistics.
 *
 * Peak and dropped-confirmation counters accumulate from the most
 * recent can_service_start() call and are preserved across runtime
 * driver reconfiguration.
 *
 * The historical peak may exceed the current capacity when a runtime
 * reconfiguration reduces the TX queue depth.
 *
 * @param[out] statistics Destination statistics structure.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if statistics is
 * NULL, or ESP_ERR_INVALID_STATE if the CAN service is not running.
 */
esp_err_t can_service_get_queue_statistics(
    can_service_queue_statistics_t *statistics
);

/**
 * @brief Check whether the primary CAN service is running.
 */
bool can_service_is_running(void);

#ifdef __cplusplus
}
#endif
