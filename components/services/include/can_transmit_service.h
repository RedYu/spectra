/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_TRANSMIT_JOB_COUNT        (8U)
#define CAN_TRANSMIT_MIN_INTERVAL_MS  (10U)

/**
 * @brief Configuration of one cyclic transmission job.
 */
typedef struct
{
    /** Frame used for the first transmission attempt. */
    can_frame_t frame;

    /** Interval between transmission attempts in milliseconds. */
    uint32_t interval_ms;

    /** Number of attempts, or zero to continue until explicitly stopped. */
    uint32_t count;

    /** Identifier increment, or zero to keep the identifier unchanged. */
    uint32_t id_step;

    /** Last identifier before wrapping to the initial value. */
    uint32_t id_end;

    /** True to increment the DLC after every accepted submission. */
    bool increment_dlc;

    /** Last DLC before wrapping to the initial value. */
    uint8_t dlc_end;

    /** Offset of the incremented counter within the frame payload. */
    uint8_t data_offset;

    /** Counter width from one to eight bytes, or zero to disable it. */
    uint8_t data_width;

    /** Value added to the payload counter after each submission. */
    uint32_t data_step;

    /** True when the payload counter uses big-endian byte order. */
    bool data_big_endian;

    /** True to increment masked bits in each payload byte. */
    bool increment_data_bytes;

    /** Per-byte masks selecting independently incremented payload bits. */
    uint8_t data_byte_masks[CAN_FRAME_FD_DATA_MAX_LENGTH];

} can_transmit_job_config_t;

/**
 * @brief Current transmission job state.
 */
typedef enum
{
    CAN_TRANSMIT_IDLE = 0,
    CAN_TRANSMIT_ACTIVE,
    CAN_TRANSMIT_STOPPED,
    CAN_TRANSMIT_COMPLETE,
    CAN_TRANSMIT_ERROR,
    CAN_TRANSMIT_UNKNOWN,

} can_transmit_job_state_t;

/**
 * @brief Transmission configuration, counters and current state.
 */
typedef struct
{
    /** Job configuration supplied when the job was started. */
    can_transmit_job_config_t config;

    /** Frame prepared for the next transmission attempt. */
    can_frame_t next_frame;

    /** Current job state. */
    can_transmit_job_state_t state;

    /** Total number of transmission attempts. */
    uint32_t attempts;

    /** Frames accepted by the CAN router. */
    uint32_t queued;

    /** Frames reported as successfully transmitted. */
    uint32_t completed;

    /** Frames reported as failed. */
    uint32_t failed;

    /** Frames reported as aborted. */
    uint32_t aborted;

    /** Frames for which no final result was received. */
    uint32_t unknown;

    /** Transaction awaiting a final transmission result, or zero. */
    uint32_t pending_transaction;

    /** Most recent job or transmission error. */
    esp_err_t last_error;

} can_transmit_job_info_t;

/**
 * @brief Initialize the transmission service after the CAN router.
 *
 * Allocates a lifetime worker and bounded confirmation queue. Call once
 * from the startup task. No transmission starts automatically.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t can_transmit_service_init(void);

/**
 * @brief Stop new submissions before stopping the CAN router.
 *
 * Retains lifetime worker resources. Already queued hardware frames
 * cannot be cancelled by this service.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t can_transmit_service_stop(void);

/**
 * @brief Configure an inactive slot and start transmission.
 *
 * Rejects slots with pending transmissions and unavailable, listen-only
 * or incompatible interfaces.
 *
 * @param[in] slot Zero-based job slot.
 * @param[in] config Transmission configuration.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t can_transmit_job_start(
    uint32_t slot,
    const can_transmit_job_config_t *config
);

/**
 * @brief Stop new submissions for a job.
 *
 * A queued frame may still complete afterwards.
 *
 * @param[in] slot Zero-based job slot.
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t can_transmit_job_stop(
    uint32_t slot
);

/**
 * @brief Copy the current job information under the service lock.
 *
 * @param[in] slot Zero-based job slot.
 * @param[out] info Destination job information.
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t can_transmit_job_get(
    uint32_t slot,
    can_transmit_job_info_t *info
);

/**
 * @brief Validate a transmission configuration without starting a job.
 *
 * @param[in] config Configuration to validate.
 * @return ESP_OK on success or ESP_ERR_INVALID_ARG for invalid fields.
 */
esp_err_t can_transmit_job_validate(
    const can_transmit_job_config_t *config
);

/**
 * @brief Advance ID, DLC and DATA after an accepted submission.
 *
 * Newly exposed data bytes are zeroed.
 *
 * @param[in] config Validated increment configuration.
 * @param[in,out] frame Frame to advance.
 */
void can_transmit_job_advance(
    const can_transmit_job_config_t *config,
    can_frame_t *frame
);

#ifdef __cplusplus
}
#endif
