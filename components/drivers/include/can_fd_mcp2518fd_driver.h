/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file can_fd_mcp2518fd_driver.h
 * @brief Secondary CAN controller driver based on MCP2518FD.
 *
 * The driver supports Classical CAN and CAN FD frames, optional Bit
 * Rate Switching, payloads up to 64 bytes, hardware timestamps,
 * acceptance filters and transmission-event tracking.
 *
 * The driver owns the dedicated SPI bus and MCP2518FD interrupt GPIO.
 *
 * Public runtime operations are thread-safe after successful
 * initialization. Driver deinitialization must only be performed by
 * the component that owns the driver lifecycle.
 */

#define CAN_FD_MCP2518FD_CLASSIC_DATA_MAX_LENGTH   (8U)
#define CAN_FD_MCP2518FD_DATA_MAX_LENGTH           (64U)

#define CAN_FD_MCP2518FD_STANDARD_ID_MAX           (0x7FFU)
#define CAN_FD_MCP2518FD_EXTENDED_ID_MAX           (0x1FFFFFFFU)

#define CAN_FD_MCP2518FD_BITRATE_50_KBIT           (50000U)
#define CAN_FD_MCP2518FD_BITRATE_100_KBIT          (100000U)
#define CAN_FD_MCP2518FD_BITRATE_125_KBIT          (125000U)
#define CAN_FD_MCP2518FD_BITRATE_250_KBIT          (250000U)
#define CAN_FD_MCP2518FD_BITRATE_500_KBIT          (500000U)
#define CAN_FD_MCP2518FD_BITRATE_800_KBIT          (800000U)
#define CAN_FD_MCP2518FD_BITRATE_1_MBIT            (1000000U)
#define CAN_FD_MCP2518FD_BITRATE_2_MBIT            (2000000U)
#define CAN_FD_MCP2518FD_BITRATE_4_MBIT            (4000000U)
#define CAN_FD_MCP2518FD_BITRATE_5_MBIT            (5000000U)
#define CAN_FD_MCP2518FD_BITRATE_8_MBIT            (8000000U)

#define CAN_FD_MCP2518FD_FILTER_COUNT              (32U)

#if CONFIG_CAN_FD_MCP2518FD_ENABLE_PROFILING
#define CAN_FD_MCP2518FD_ENABLE_PROFILING (1)
#else
#define CAN_FD_MCP2518FD_ENABLE_PROFILING (0)
#endif

/**
 * @brief MCP2518FD operating mode.
 */
typedef enum
{
    CAN_FD_MCP2518FD_MODE_NORMAL = 0,

    /**
     * Receive-only mode. No ACK or transmissions are generated.
     */
    CAN_FD_MCP2518FD_MODE_LISTEN_ONLY,

    /**
     * Internal controller loopback. TXCAN remains recessive.
     */
    CAN_FD_MCP2518FD_MODE_INTERNAL_LOOPBACK,

    /**
     * Internal loopback with the transmitted signal visible on TXCAN.
     *
     * External acknowledgement is not required and external bus
     * messages are not received because RXCAN is disconnected.
     */
    CAN_FD_MCP2518FD_MODE_EXTERNAL_LOOPBACK,

    /**
     * Receive frames and generate ACK, but ignore application TXREQ.
     */
    CAN_FD_MCP2518FD_MODE_RESTRICTED,

} can_fd_mcp2518fd_mode_t;

/**
 * @brief Current MCP2518FD controller state.
 */
typedef enum
{
    CAN_FD_MCP2518FD_STATE_STOPPED = 0,
    CAN_FD_MCP2518FD_STATE_ERROR_ACTIVE,
    CAN_FD_MCP2518FD_STATE_ERROR_WARNING,
    CAN_FD_MCP2518FD_STATE_ERROR_PASSIVE,
    CAN_FD_MCP2518FD_STATE_BUS_OFF,
    CAN_FD_MCP2518FD_STATE_UNKNOWN,

} can_fd_mcp2518fd_state_t;

/**
 * @brief Classical CAN or CAN FD frame.
 *
 * When fd_frame is false, data_length must not exceed 8 bytes and
 * bit_rate_switch and error_state_indicator must be false.
 *
 * Remote frames are supported only in Classical CAN format.
 */
typedef struct
{
    uint32_t identifier;

    /**
     * Actual number of payload bytes.
     *
     * Classical CAN supports 0 to 8 bytes.
     * CAN FD supports 0 to 8, 12, 16, 20, 24, 32, 48 or 64 bytes.
     */
    uint8_t data_length;

    uint8_t data[
        CAN_FD_MCP2518FD_DATA_MAX_LENGTH
    ];

    bool extended;

    /**
     * Remote Transmission Request.
     *
     * Must be false for CAN FD frames because CAN FD does not support
     * remote frames.
     */
    bool remote;

    /**
     * CAN FD frame format.
     */
    bool fd_frame;

    /**
     * Enable the higher data-phase bitrate.
     *
     * Valid only when fd_frame is true.
     */
    bool bit_rate_switch;

    /**
     * Error State Indicator for a CAN FD frame.
     *
     * The field is decoded from received CAN FD frames. For transmission,
     * it is copied into the CAN FD TX object. It must remain false for
     * Classical CAN frames.
     */
    bool error_state_indicator;

    /**
     * Hardware receive timestamp.
     *
     * Value is expressed in microseconds and wraps at UINT32_MAX.
     * Valid only when timestamp_valid is true.
     */
    uint32_t timestamp;

    bool timestamp_valid;

} can_fd_mcp2518fd_frame_t;

/**
 * @brief Automatic retransmission policy.
 */
typedef enum
{
    /**
     * Retry indefinitely until transmission succeeds or is aborted.
     */
    CAN_FD_MCP2518FD_RETRANSMISSION_UNLIMITED = 0,

    /**
     * Allow up to three retransmission attempts.
     */
    CAN_FD_MCP2518FD_RETRANSMISSION_THREE_ATTEMPTS,

    /**
     * Do not retry after an unsuccessful transmission attempt.
     *
     * This is commonly called one-shot transmission.
     */
    CAN_FD_MCP2518FD_RETRANSMISSION_DISABLED,

} can_fd_mcp2518fd_retransmission_t;

/**
 * @brief MCP2518FD driver configuration.
 */
typedef struct
{
    uint32_t oscillator_hz;

    /**
     * Arbitration-phase bitrate.
     */
    uint32_t nominal_bitrate;

    /**
     * Data-phase bitrate used when BRS is enabled.
     */
    uint32_t data_bitrate;

    can_fd_mcp2518fd_mode_t mode;

    /**
     * Enable CAN FD frame support.
     */
    bool fd_enabled;

    /**
     * Enable switching to data_bitrate during CAN FD data phase.
     *
     * Requires fd_enabled. When false, data_bitrate must equal
     * nominal_bitrate.
     */
    bool brs_enabled;

    uint8_t tx_fifo_depth;
    uint8_t rx_fifo_depth;

    /**
     * Enable CRC-protected SPI commands.
     *
     * This option is reserved for future implementation and must
     * currently remain false.
     */
    bool spi_crc_enabled;

    /**
     * Automatic retransmission policy for TX FIFO1.
     */
    can_fd_mcp2518fd_retransmission_t retransmission;

} can_fd_mcp2518fd_config_t;

/**
 * @brief MCP2518FD runtime information.
 */
typedef struct
{
    bool initialized;
    bool started;

    /**
     * True after SPI communication with the controller is confirmed.
     */
    bool controller_detected;

    /**
     * True when the external oscillator reports a ready state.
     */
    bool oscillator_ready;

    /**
     * True when the controller accepts and transmits CAN FD frames.
     */
    bool fd_enabled;

    /**
     * True when CAN FD frames may use the configured data-phase bitrate.
     */
    bool brs_enabled;

    can_fd_mcp2518fd_state_t state;
    can_fd_mcp2518fd_mode_t mode;

    uint32_t oscillator_hz;

    /**
     * Arbitration-phase bitrate used by Classical CAN and CAN FD.
     */
    uint32_t nominal_bitrate;

    /**
     * CAN FD data-phase bitrate.
     *
     * Equal to nominal_bitrate while CAN FD or BRS is disabled.
     */
    uint32_t data_bitrate;

    /**
     * Number of transmissions confirmed by the Transmit Event FIFO.
     */
    uint32_t transmitted_frames;

    /**
     * Number of Transmit Event FIFO overflow events.
     */
    uint32_t transmit_event_overflow_count;

    /**
     * Sequence number of the latest completed transmission.
     */
    uint32_t last_transmit_sequence;

    /**
     * Hardware timestamp-counter frequency.
     */
    uint32_t timestamp_frequency_hz;

    /**
     * Timestamp of the latest successfully transmitted frame.
     */
    uint32_t last_transmit_timestamp;

    bool last_transmit_timestamp_valid;

    uint32_t received_frames;
    uint32_t dropped_rx_frames;

    uint32_t transmit_failures;
    uint32_t receive_overflow_count;
    uint32_t bus_error_count;

    uint8_t transmit_error_count;
    uint8_t receive_error_count;

    /**
     * Interrupt flags captured during the latest interrupt processing.
     * This is a diagnostic snapshot, not necessarily the current value
     * of the controller interrupt register.
     */
    uint32_t interrupt_flags;

    /**
     * Raw oscillator-register value.
     */
    uint32_t oscillator_register;

    /**
     * Raw CiBDIAG0 value captured during the latest diagnostic interrupt.
     */
    uint32_t diagnostic_register_0;

    /**
     * Raw CiBDIAG1 value captured during the latest diagnostic interrupt.
     */
    uint32_t diagnostic_register_1;

    /**
     * Accumulated receive-error events reported by CiBDIAG0.
     */
    uint32_t receive_error_events;

    /**
     * Accumulated transmit-error events reported by CiBDIAG0.
     */
    uint32_t transmit_error_events;

    /**
     * Error flags captured during the latest diagnostic interrupt.
     */
    bool acknowledge_error;
    bool can_crc_error;
    bool stuff_error;
    bool form_error;
    bool bit_error;

    /**
     * Number of completed Bus-off recovery sequences.
     */
    uint32_t bus_off_recovery_count;

    /**
     * Accumulated ECC error counters.
     */
    uint32_t ecc_single_error_count;
    uint32_t ecc_double_error_count;

    /**
     * Message RAM address of the latest ECC error.
     */
    uint16_t last_ecc_error_address;

    /**
     * Raw ECCSTAT value captured during the latest ECC interrupt.
     */
    uint32_t ecc_status_register;

    /**
     * Accumulated error-free message count from CiBDIAG1.
     */
    uint32_t error_free_frames;

    /**
     * Number of completed transmission events discarded because the
     * software TX-event queue was full.
     */
    uint32_t dropped_tx_events;

    can_fd_mcp2518fd_retransmission_t retransmission;

    uint16_t data_sample_point_permill;
    uint16_t data_brp;
    uint16_t data_tseg1;
    uint16_t data_tseg2;
    uint16_t data_sjw;
    uint16_t tdc_offset;

    bool tdc_enabled;

} can_fd_mcp2518fd_info_t;

/**
 * @brief MCP2518FD settings that may be changed while running.
 */
typedef struct
{
    /**
     * Arbitration-phase bitrate.
     */
    uint32_t nominal_bitrate;

    /**
     * CAN FD data-phase bitrate.
     *
     * Must equal nominal_bitrate when BRS is disabled.
     */
    uint32_t data_bitrate;

    can_fd_mcp2518fd_mode_t mode;
    can_fd_mcp2518fd_retransmission_t retransmission;

    /**
     * Enable CAN FD frame support.
     */
    bool fd_enabled;

    /**
     * Enable switching to data_bitrate during CAN FD data phase.
     *
     * Requires fd_enabled.
     */
    bool brs_enabled;

} can_fd_mcp2518fd_runtime_config_t;

/**
 * @brief Successfully completed transmission event.
 */
typedef struct
{
    /**
     * Sequence assigned to the transmitted frame.
     */
    uint32_t sequence;

    /**
     * Hardware SOF timestamp in microseconds.
     *
     * The 32-bit counter wraps periodically.
     */
    uint32_t timestamp;

} can_fd_mcp2518fd_tx_event_t;

/**
 * @brief MCP2518FD acceptance-filter configuration.
 *
 * Mask bits set to one participate in comparison. Mask bits cleared
 * to zero are treated as don't-care bits.
 *
 * Standard filters use the lower 11 bits of identifier and mask.
 * Extended filters use the lower 29 bits.
 */
typedef struct
{
    /**
     * Hardware filter index from 0 to 31.
     */
    uint8_t index;

    /**
     * Identifier compared with received frames.
     */
    uint32_t identifier;

    /**
     * Identifier comparison mask.
     */
    uint32_t mask;

    /**
     * Select Extended 29-bit identifiers. When false, the filter
     * accepts only Standard 11-bit identifiers.
     */
    bool extended;

} can_fd_mcp2518fd_filter_t;

/**
 * @brief Accumulated MCP2518FD performance measurements.
 *
 * All time values include the complete synchronous operation as seen
 * by the calling task, including SPI-driver scheduling and waiting for
 * transaction completion.
 */
typedef struct
{
    uint64_t interrupt_time_us;
    uint64_t fifo_status_time_us;
    uint64_t object_read_time_us;
    uint64_t fifo_increment_time_us;
    uint64_t decode_time_us;

    uint32_t interrupt_count;
    uint32_t received_frames;

} can_fd_mcp2518fd_profile_t;

/**
 * @brief Initialize the MCP2518FD driver.
 *
 * Creates the dedicated SPI bus, registers the MCP2518FD SPI device,
 * configures its interrupt GPIO, resets the controller and verifies
 * SPI communication.
 *
 * The controller remains stopped after initialization.
 *
 * @param[in] config Driver configuration.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if the configuration
 * is invalid, ESP_ERR_INVALID_STATE if already initialized,
 * ESP_ERR_NOT_FOUND if the controller does not respond, otherwise an
 * ESP-IDF error code.
 */
esp_err_t can_fd_mcp2518fd_driver_init(
    const can_fd_mcp2518fd_config_t *config
);

/**
 * @brief Reconfigure the running MCP2518FD controller.
 *
 * Pending transmissions are aborted. Hardware TX/RX FIFOs, TEF and
 * the software TX-event queue are reset. Acceptance filters remain
 * configured.
 *
 * If applying the new configuration fails, the driver attempts to
 * restore the previous configuration.
 *
 * @param[in] config New runtime configuration.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if config is invalid,
 * ESP_ERR_INVALID_STATE if the driver is not running, otherwise an
 * ESP-IDF error code.
 */
esp_err_t can_fd_mcp2518fd_driver_reconfigure(
    const can_fd_mcp2518fd_runtime_config_t *config
);

/**
 * @brief Start the MCP2518FD CAN controller.
 *
 * Configures nominal and data-phase timing, transmitter delay
 * compensation, Message RAM, TEF, RX/TX FIFOs, acceptance filters and
 * interrupt sources. The controller is then switched into the selected
 * operating mode.
 *
 * FIFO payload size is selected automatically: 8 bytes for Classical
 * CAN and 64 bytes when CAN FD is enabled.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the driver is
 * not initialized or already started, ESP_ERR_NOT_SUPPORTED if the
 * requested bit timing cannot be generated, otherwise an ESP-IDF
 * error code.
 */
esp_err_t can_fd_mcp2518fd_driver_start(void);

/**
 * @brief Stop the MCP2518FD CAN controller.
 *
 * Returns the controller to Configuration mode and interrupts pending
 * receive operations.
 *
 * @return ESP_OK on success or ESP_ERR_INVALID_STATE if the driver is
 * not initialized.
 */
esp_err_t can_fd_mcp2518fd_driver_stop(void);

/**
 * @brief Release MCP2518FD driver resources.
 *
 * Stops the controller, removes the SPI device, releases the dedicated
 * SPI bus and resets the interrupt GPIO.
 *
 * Calling this function when the driver is not initialized has no
 * effect.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t can_fd_mcp2518fd_driver_deinit(void);

/**
 * @brief Queue one Classical CAN or CAN FD frame for transmission.
 *
 * Classical CAN frames may contain up to 8 payload bytes. CAN FD
 * frames support payload lengths of 0 through 8, 12, 16, 20, 24, 32,
 * 48 or 64 bytes. BRS frames require CAN FD and BRS to be enabled in
 * the current driver configuration.
 *
 * Successful return means that the frame was placed into TX FIFO.
 * Actual bus transmission is confirmed asynchronously through the
 * Transmit Event FIFO.
 *
 * @param[in] frame Frame to transmit.
 * @param[in] timeout_ms Maximum time to wait for an available TX FIFO
 * entry.
 *
 * @return ESP_OK when queued, ESP_ERR_INVALID_ARG if the frame is
 * invalid or incompatible with the current configuration,
 * ESP_ERR_INVALID_STATE if the controller cannot transmit,
 * ESP_ERR_TIMEOUT on timeout, otherwise an ESP-IDF error code.
 */
esp_err_t can_fd_mcp2518fd_driver_transmit(
    const can_fd_mcp2518fd_frame_t *frame,
    uint32_t timeout_ms
);

/**
 * @brief Wait for a successfully completed transmission event.
 *
 * Events are generated from the MCP2518FD Transmit Event FIFO.
 *
 * @param[out] event Destination event.
 * @param[in] timeout_ms Maximum time to wait.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if event is NULL,
 * ESP_ERR_INVALID_STATE if the driver is not running, or
 * ESP_ERR_TIMEOUT if no event is received before the timeout.
 */
esp_err_t can_fd_mcp2518fd_driver_receive_tx_event(
    can_fd_mcp2518fd_tx_event_t *event,
    uint32_t timeout_ms
);

/**
 * @brief Queue one CAN frame and return its TEF sequence number.
 *
 * The returned sequence can be matched with a later
 * can_fd_mcp2518fd_tx_event_t received through
 * can_fd_mcp2518fd_driver_receive_tx_event().
 *
 * A sequence number is written only after the frame has been
 * successfully placed into the hardware TX FIFO.
 *
 * @param[in] frame Frame to transmit.
 * @param[in] timeout_ms Maximum time to wait for a free TX FIFO object.
 * @param[out] sequence Assigned hardware TEF sequence number.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if an argument is
 * invalid, ESP_ERR_INVALID_STATE if the driver cannot transmit,
 * ESP_ERR_TIMEOUT if the TX FIFO remains full, otherwise an ESP-IDF
 * error code.
 */
esp_err_t can_fd_mcp2518fd_driver_transmit_tracked(
    const can_fd_mcp2518fd_frame_t *frame,
    uint32_t timeout_ms,
    uint32_t *sequence
);

/**
 * @brief Receive one Classical CAN or CAN FD frame.
 *
 * The returned structure contains the decoded payload, FDF, BRS and
 * ESI flags and the hardware receive timestamp.
 *
 * @param[out] frame Destination frame.
 * @param[in] timeout_ms Maximum time to wait for an RX frame.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if frame is NULL,
 * ESP_ERR_INVALID_STATE if the controller is not running,
 * ESP_ERR_TIMEOUT on timeout, otherwise an ESP-IDF error code.
 */
esp_err_t can_fd_mcp2518fd_driver_receive(
    can_fd_mcp2518fd_frame_t *frame,
    uint32_t timeout_ms
);

/**
 * @brief Receive multiple frames from RX FIFO2.
 *
 * The function waits up to timeout_ms for the first frame. After the
 * first frame is received, all immediately available frames are drained
 * without additional waiting, up to frame_capacity.
 *
 * The returned frames are ordered from oldest to newest.
 *
 * @param[out] frames Destination frame array.
 * @param[in] frame_capacity Number of elements available in frames.
 * @param[out] received_count Number of frames successfully received.
 * @param[in] timeout_ms Maximum time to wait for the first frame.
 *
 * @return ESP_OK when at least one frame is received,
 * ESP_ERR_INVALID_ARG for invalid arguments,
 * ESP_ERR_INVALID_STATE if the driver is not running,
 * ESP_ERR_TIMEOUT when no frame arrives before the timeout,
 * otherwise an ESP-IDF error code.
 */
esp_err_t can_fd_mcp2518fd_driver_receive_batch(
    can_fd_mcp2518fd_frame_t *frames,
    size_t frame_capacity,
    size_t *received_count,
    uint32_t timeout_ms
);

/**
 * @brief Confirm automatic recovery from the Bus-off state.
 *
 * MCP2518FD starts Bus-off recovery automatically. This function
 * verifies that the controller is currently in Bus-off and
 * synchronizes the driver's pending-transmission state.
 *
 * Recovery completes asynchronously after the controller detects
 * 128 occurrences of 11 consecutive recessive bits.
 *
 * @return ESP_OK when automatic recovery is active,
 * ESP_ERR_INVALID_STATE if the driver is not running or is not
 * currently in Bus-off, otherwise an ESP-IDF error code.
 */
esp_err_t can_fd_mcp2518fd_driver_recover(void);

/**
 * @brief Abort all pending MCP2518FD transmissions.
 *
 * Frames that have not started transmission are aborted. A frame that
 * has already transmitted its Start Of Frame bit may complete before
 * the abort operation finishes.
 *
 * Successful transmissions completed before the abort are processed
 * through the Transmit Event FIFO. Remaining pending transmissions
 * are discarded and TX FIFO1 is reset.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the driver is
 * not running, ESP_ERR_TIMEOUT if transmit requests remain active,
 * otherwise an ESP-IDF error code.
 */
esp_err_t can_fd_mcp2518fd_driver_abort_transmissions(void);

/**
 * @brief Configure and enable one hardware acceptance filter.
 *
 * The filter is temporarily disabled while its object and mask are
 * updated, then routed to the driver's RX FIFO2.
 *
 * This function may be called while the controller is running.
 *
 * @param[in] filter Filter configuration.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if the filter is
 * invalid, ESP_ERR_INVALID_STATE if the driver is not initialized,
 * otherwise an ESP-IDF error code.
 */
esp_err_t can_fd_mcp2518fd_driver_set_filter(
    const can_fd_mcp2518fd_filter_t *filter
);

/**
 * @brief Disable one hardware acceptance filter.
 *
 * @param[in] filter_index Hardware filter index from 0 to 31.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if the index is
 * invalid, ESP_ERR_INVALID_STATE if the driver is not initialized,
 * otherwise an ESP-IDF error code.
 */
esp_err_t can_fd_mcp2518fd_driver_disable_filter(
    uint8_t filter_index
);

/**
 * @brief Disable all hardware acceptance filters.
 *
 * Received frames are rejected until at least one filter is enabled.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the driver is
 * not initialized, otherwise an ESP-IDF error code.
 */
esp_err_t can_fd_mcp2518fd_driver_disable_all_filters(void);

/**
 * @brief Configure Filter 0 to accept all Standard and Extended frames.
 *
 * All other filters are disabled. Filter 0 is routed to RX FIFO2.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the driver is
 * not initialized, otherwise an ESP-IDF error code.
 */
esp_err_t can_fd_mcp2518fd_driver_accept_all(void);

/**
 * @brief Copy current MCP2518FD runtime information.
 *
 * @param[out] info Destination information structure.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if info is NULL, or
 * ESP_ERR_INVALID_STATE if the driver is not initialized.
 */
esp_err_t can_fd_mcp2518fd_driver_get_info(
    can_fd_mcp2518fd_info_t *info
);

/**
 * @brief Check whether the MCP2518FD driver is initialized.
 */
bool can_fd_mcp2518fd_driver_is_initialized(void);

/**
 * @brief Check whether the MCP2518FD CAN controller is running.
 */
bool can_fd_mcp2518fd_driver_is_started(void);

/**
 * @brief Copy accumulated MCP2518FD performance measurements.
 *
 * Measurements are reset whenever the controller is successfully
 * started.
 *
 * @param[out] profile Destination profile structure.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if profile is NULL,
 * ESP_ERR_NOT_SUPPORTED if profiling is disabled,
 * ESP_ERR_INVALID_STATE if the driver is not initialized, otherwise
 * an ESP-IDF synchronization error.
 */
esp_err_t can_fd_mcp2518fd_driver_get_profile(
    can_fd_mcp2518fd_profile_t *profile
);

#ifdef __cplusplus
}
#endif
