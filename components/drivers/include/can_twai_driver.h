#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_TWAI_CLASSIC_DATA_MAX_LENGTH    (8U)

#define CAN_TWAI_STANDARD_ID_MAX            (0x7FFU)
#define CAN_TWAI_EXTENDED_ID_MAX            (0x1FFFFFFFU)

#define CAN_TWAI_BITRATE_50_KBIT            (50000U)
#define CAN_TWAI_BITRATE_100_KBIT           (100000U)
#define CAN_TWAI_BITRATE_125_KBIT           (125000U)
#define CAN_TWAI_BITRATE_250_KBIT           (250000U)
#define CAN_TWAI_BITRATE_500_KBIT           (500000U)
#define CAN_TWAI_BITRATE_800_KBIT           (800000U)
#define CAN_TWAI_BITRATE_1_MBIT             (1000000U)

/**
 * @brief Primary TWAI controller operating mode.
 */
typedef enum
{
    /**
     * Normal CAN operation with transmission and acknowledgement.
     */
    CAN_TWAI_MODE_NORMAL = 0,

    /**
     * Passive monitoring without transmission or acknowledgement.
     */
    CAN_TWAI_MODE_LISTEN_ONLY,

    /**
     * Internal self-test with transmitted frames received locally.
     */
    CAN_TWAI_MODE_SELF_TEST,

} can_twai_mode_t;

/**
 * @brief Current primary TWAI controller state.
 */
typedef enum
{
    CAN_TWAI_STATE_STOPPED = 0,
    CAN_TWAI_STATE_ERROR_ACTIVE,
    CAN_TWAI_STATE_ERROR_WARNING,
    CAN_TWAI_STATE_ERROR_PASSIVE,
    CAN_TWAI_STATE_BUS_OFF,
    CAN_TWAI_STATE_RECOVERING,
    CAN_TWAI_STATE_UNKNOWN,

} can_twai_state_t;

/**
 * @brief Classical CAN frame.
 */
typedef struct
{
    uint32_t identifier;

    uint8_t data_length;

    bool extended;
    bool remote;

    uint8_t data[
        CAN_TWAI_CLASSIC_DATA_MAX_LENGTH
    ];

    /**
     * Receive timestamp in microseconds since system startup.
     * This value is zero for transmitted frames supplied by callers.
     */
    uint64_t timestamp_us;

} can_twai_frame_t;

/**
 * @brief Primary TWAI driver configuration.
 */
typedef struct
{
    uint32_t bitrate;

    /**
     * Sample point in permille. For example, 800 means 80.0 percent.
     * Set to zero to use the ESP-IDF default.
     */
    uint16_t sample_point_permill;

    can_twai_mode_t mode;

    /**
     * ESP-IDF hardware transmit-queue depth.
     */
    uint16_t tx_queue_depth;

    /**
     * Application receive-queue length.
     */
    uint16_t rx_queue_length;

    /**
     * Number of retransmission attempts after transmission failure.
     * Use -1 for unlimited retries and 0 for single-shot transmission.
     */
    int8_t transmit_retry_count;

} can_twai_driver_config_t;

/**
 * @brief Primary TWAI runtime information.
 */
typedef struct
{
    bool initialized;
    bool started;

    can_twai_state_t state;
    can_twai_mode_t mode;

    uint32_t bitrate;
    uint16_t sample_point_permill;

    uint32_t transmitted_frames;
    uint32_t received_frames;
    uint32_t dropped_rx_frames;

    uint32_t arbitration_lost_count;
    uint32_t bit_error_count;
    uint32_t form_error_count;
    uint32_t stuff_error_count;
    uint16_t transmit_error_count;
    uint16_t receive_error_count;
    uint32_t bus_error_count;
    uint32_t acknowledgement_error_count;

} can_twai_driver_info_t;

/**
 * @brief Initialize the primary ESP32-S3 TWAI controller.
 *
 * Creates the on-chip TWAI node, application receive queue and event
 * callbacks. The controller remains stopped after initialization.
 *
 * @param[in] config Driver configuration.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for an invalid
 * configuration, ESP_ERR_INVALID_STATE if already initialized,
 * ESP_ERR_NO_MEM if resources cannot be allocated, otherwise an
 * ESP-IDF error code.
 */
esp_err_t can_twai_driver_init(
    const can_twai_driver_config_t *config
);

/**
 * @brief Start the initialized TWAI controller.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the driver is
 * not initialized or already started, otherwise an ESP-IDF error code.
 */
esp_err_t can_twai_driver_start(void);

/**
 * @brief Stop the TWAI controller.
 *
 * Pending transmissions are aborted and queued received frames are
 * discarded.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the driver is
 * not initialized or is already stopped, otherwise an ESP-IDF error
 * code.
 */
esp_err_t can_twai_driver_stop(void);

/**
 * @brief Stop and release all TWAI driver resources.
 *
 * Calling this function when the driver is not initialized has no
 * effect.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t can_twai_driver_deinit(void);

/**
 * @brief Queue one Classical CAN frame for transmission.
 *
 * The frame is copied into driver-owned memory and may be released by
 * the caller immediately after this function returns. The timeout
 * applies only while waiting for an available internal transmit slot.
 *
 * @param[in] frame Frame to transmit.
 * @param[in] timeout_ms Maximum time to wait for an available internal
 * transmit slot.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if the frame is
 * invalid, ESP_ERR_INVALID_STATE if the driver is not running,
 * ESP_ERR_TIMEOUT on timeout, otherwise an ESP-IDF error code.
 */
esp_err_t can_twai_driver_transmit(
    const can_twai_frame_t *frame,
    uint32_t timeout_ms
);

/**
 * @brief Receive one Classical CAN frame.
 *
 * @param[out] frame Destination frame.
 * @param[in] timeout_ms Maximum time to wait for a frame.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if frame is NULL,
 * ESP_ERR_INVALID_STATE if the driver is not running, or
 * ESP_ERR_TIMEOUT when no frame is received before the timeout.
 */
esp_err_t can_twai_driver_receive(
    can_twai_frame_t *frame,
    uint32_t timeout_ms
);

/**
 * @brief Request recovery after the controller enters bus-off.
 *
 * Recovery completes asynchronously and its progress is available
 * through can_twai_driver_get_info().
 *
 * @return ESP_OK when recovery is started, ESP_ERR_INVALID_STATE if
 * recovery cannot currently be started, otherwise an ESP-IDF error
 * code.
 */
esp_err_t can_twai_driver_recover(void);

/**
 * @brief Copy current primary TWAI runtime information.
 *
 * @param[out] info Destination information structure.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if info is NULL,
 * otherwise an ESP-IDF error code.
 */
esp_err_t can_twai_driver_get_info(
    can_twai_driver_info_t *info
);

/**
 * @brief Check whether the primary TWAI driver is initialized.
 */
bool can_twai_driver_is_initialized(void);

/**
 * @brief Check whether the primary TWAI controller is running.
 */
bool can_twai_driver_is_started(void);

#ifdef __cplusplus
}
#endif
