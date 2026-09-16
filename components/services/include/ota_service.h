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

#define OTA_SERVICE_PARTITION_LABEL_MAX_LENGTH  (17U)
#define OTA_SERVICE_PROJECT_NAME_MAX_LENGTH     (32U)
#define OTA_SERVICE_VERSION_MAX_LENGTH          (32U)
#define OTA_SERVICE_SD_UPDATE_DIRECTORY         "/updates"

/**
 * @file ota_service.h
 * @brief Stream application images into an inactive OTA partition.
 */

/**
 * @brief OTA update state.
 */
typedef enum
{
    OTA_SERVICE_STATE_UNINITIALIZED = 0,
    OTA_SERVICE_STATE_IDLE,
    OTA_SERVICE_STATE_RECEIVING,
    OTA_SERVICE_STATE_READY,
    OTA_SERVICE_STATE_ERROR,

    OTA_SERVICE_STATE_COUNT,

} ota_service_state_t;

/**
 * @brief Validation state of the currently running application image.
 */
typedef enum
{
    OTA_SERVICE_IMAGE_STATE_UNKNOWN = 0,
    OTA_SERVICE_IMAGE_STATE_NEW,
    OTA_SERVICE_IMAGE_STATE_PENDING_VERIFY,
    OTA_SERVICE_IMAGE_STATE_VALID,
    OTA_SERVICE_IMAGE_STATE_INVALID,
    OTA_SERVICE_IMAGE_STATE_ABORTED,

    OTA_SERVICE_IMAGE_STATE_COUNT,

} ota_service_image_state_t;

/**
 * @brief State of the latest firmware check through the Spectra backend.
 */
typedef enum
{
    OTA_SERVICE_BACKEND_STATE_NOT_CHECKED = 0,
    OTA_SERVICE_BACKEND_STATE_CHECKING,
    OTA_SERVICE_BACKEND_STATE_NO_UPDATE,
    OTA_SERVICE_BACKEND_STATE_UPDATE_AVAILABLE,
    OTA_SERVICE_BACKEND_STATE_ERROR,

    OTA_SERVICE_BACKEND_STATE_COUNT,

} ota_service_backend_state_t;

/**
 * @brief Current OTA service and transfer information.
 */
typedef struct
{
    bool initialized;
    bool rollback_enabled;
    bool verification_pending;

    ota_service_state_t state;
    ota_service_image_state_t running_image_state;
    ota_service_backend_state_t backend_state;

    uint64_t backend_last_check_ms;
    esp_err_t backend_last_error;

    char backend_version[
        OTA_SERVICE_VERSION_MAX_LENGTH
    ];

    size_t image_size;
    size_t written_size;
    uint8_t progress_percent;

    size_t update_partition_size;
    uint32_t update_partition_address;

    char running_partition[
        OTA_SERVICE_PARTITION_LABEL_MAX_LENGTH
    ];

    char boot_partition[
        OTA_SERVICE_PARTITION_LABEL_MAX_LENGTH
    ];

    char update_partition[
        OTA_SERVICE_PARTITION_LABEL_MAX_LENGTH
    ];

    char project_name[
        OTA_SERVICE_PROJECT_NAME_MAX_LENGTH
    ];

    char version[
        OTA_SERVICE_VERSION_MAX_LENGTH
    ];

    esp_err_t last_error;

} ota_service_info_t;

/**
 * @brief Initialize OTA synchronization and partition information.
 *
 * The function does not erase or write Flash and may be called repeatedly.
 *
 * @return ESP_OK on success, ESP_ERR_NO_MEM if the service mutex cannot be
 * created, or ESP_ERR_NOT_FOUND if a required OTA partition is unavailable.
 */
esp_err_t ota_service_init(void);

/**
 * @brief Begin a new application image transfer.
 *
 * The destination is selected by ESP-IDF and is never the currently running
 * application partition. The complete image size must be known before the
 * transfer begins.
 *
 * @param[in] image_size Complete image size in bytes.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_SIZE if the image does not fit,
 * ESP_ERR_INVALID_STATE if a transfer or completed update is already pending,
 * otherwise an ESP-IDF OTA or Flash error code.
 */
esp_err_t ota_service_begin(
    size_t image_size
);

/**
 * @brief Append the next sequential block of firmware data.
 *
 * @param[in] data Firmware data block.
 * @param[in] size Firmware data size in bytes.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid input,
 * ESP_ERR_INVALID_STATE without an active transfer, ESP_ERR_INVALID_SIZE if
 * the block exceeds the declared image size, otherwise an OTA or Flash error.
 */
esp_err_t ota_service_write(
    const void *data,
    size_t size
);

/**
 * @brief Validate the completed image and select it for the next boot.
 *
 * The function requires exactly the declared number of bytes. It validates
 * the image through ESP-IDF and rejects images built for another project.
 * It does not restart the device.
 *
 * @return ESP_OK when the image is ready to boot, otherwise an ESP-IDF error.
 */
esp_err_t ota_service_finish(void);

/**
 * @brief Install an application image stored in the SD update directory.
 *
 * The file is read in bounded blocks and written directly to the inactive
 * OTA partition. Only regular .bin files located immediately inside
 * /updates are accepted.
 *
 * @param[in] path Absolute SD-relative path beginning with /updates/.
 *
 * @return ESP_OK when the image is validated and ready to boot, otherwise
 * an ESP-IDF storage, OTA, or image-validation error code.
 */
esp_err_t ota_service_install_from_sd(
    const char *path
);

/**
 * @brief Query the configured backend for a firmware update manifest.
 *
 * This synchronous function is intended for the Internet service task. A
 * successful backend response without an update manifest is stored as
 * OTA_SERVICE_BACKEND_STATE_NO_UPDATE.
 *
 * @return ESP_OK when the backend response was processed, otherwise an
 * ESP-IDF networking, allocation, or parsing error code.
 */
esp_err_t ota_service_check_backend(void);

/**
 * @brief Abort an active transfer or cancel a prepared boot selection.
 *
 * Cancelling a ready update restores the currently running partition as the
 * next boot partition.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service has not
 * been initialized, otherwise an ESP-IDF OTA or Flash error code.
 */
esp_err_t ota_service_cancel(void);

/**
 * @brief Confirm that the running OTA image passed startup validation.
 *
 * When rollback support is enabled, a newly booted image remains pending
 * until this function marks it valid. Calling the function for an already
 * valid image has no effect.
 *
 * @return ESP_OK when the image is valid or does not require confirmation,
 * otherwise an ESP-IDF OTA error code.
 */
esp_err_t ota_service_confirm_running_image(void);

/**
 * @brief Copy current OTA service information.
 *
 * @param[out] info Destination information structure.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if info is NULL,
 * ESP_ERR_INVALID_STATE before initialization, or ESP_ERR_TIMEOUT if the
 * service lock cannot be acquired.
 */
esp_err_t ota_service_get_info(
    ota_service_info_t *info
);

#ifdef __cplusplus
}
#endif
