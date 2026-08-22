#pragma once

#include "esp_err.h"

#include "can_frame.h"
#include "can_twai_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file can_twai_frame_adapter.h
 * @brief Conversion between TWAI driver frames and shared CAN frames.
 */

/**
 * @brief Convert a received TWAI frame to the shared frame format.
 *
 * The resulting frame belongs to CAN_BUS_PRIMARY. A non-zero TWAI
 * timestamp is marked as a software timestamp.
 *
 * @param[in] source TWAI driver frame.
 * @param[out] destination Shared CAN frame.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid pointers
 * or frame fields, or ESP_ERR_INVALID_SIZE for an invalid payload
 * length.
 */
esp_err_t can_twai_frame_to_common(
    const can_twai_frame_t *source,
    can_frame_t *destination
);

/**
 * @brief Convert a shared frame to a TWAI transmission frame.
 *
 * Only Classical CAN frames assigned to CAN_BUS_PRIMARY are accepted.
 * CAN FD, BRS and ESI are not supported by the TWAI controller.
 *
 * The TWAI transmission timestamp is cleared because it is assigned
 * only to received frames.
 *
 * @param[in] source Shared CAN frame.
 * @param[out] destination TWAI driver frame.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid pointers,
 * fields or an incompatible bus, ESP_ERR_INVALID_SIZE for an
 * unsupported DLC, or ESP_ERR_NOT_SUPPORTED for CAN FD frames.
 */
esp_err_t can_twai_frame_from_common(
    const can_frame_t *source,
    can_twai_frame_t *destination
);

#ifdef __cplusplus
}
#endif
