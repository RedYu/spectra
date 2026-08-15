#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

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

} can_service_config_t;

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
 * @brief Reconfigure the running primary CAN interface.
 *
 * Reception and transmission are temporarily stopped while the TWAI
 * controller is recreated. The receive callback remains unchanged.
 *
 * If applying the new configuration fails, the service attempts to
 * restore the previous configuration.
 *
 * This function must not be called from the receive callback.
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
 * @brief Check whether the primary CAN service is running.
 */
bool can_service_is_running(void);

#ifdef __cplusplus
}
#endif
