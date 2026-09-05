/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SD_CARD_MOUNT_POINT "/sdcard"

/**
 * @brief Initialize the SD card driver resources.
 *
 * This function initializes the synchronization resources used by
 * the driver. It does not mount the SD card filesystem.
 *
 * @return ESP_OK on success, ESP_ERR_NO_MEM if the synchronization
 * resource cannot be created, otherwise an ESP-IDF error code.
 */
esp_err_t sd_card_driver_init(void);

/**
 * @brief Mount the SD card filesystem.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the driver
 * is not initialized, ESP_ERR_TIMEOUT if a required lock cannot
 * be acquired, otherwise an ESP-IDF error code.
 */
esp_err_t sd_card_driver_mount(void);

/**
 * @brief Unmount the SD card filesystem.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the driver
 * is not initialized or its internal state is inconsistent,
 * ESP_ERR_TIMEOUT if a required lock cannot be acquired, otherwise
 * an ESP-IDF error code.
 */
esp_err_t sd_card_driver_unmount(void);

/**
 * @brief Check whether the mounted SD card is accessible.
 *
 * @return ESP_OK when the card is mounted and accessible,
 * ESP_ERR_INVALID_STATE if the card is not mounted,
 * ESP_ERR_TIMEOUT if a required lock cannot be acquired, otherwise
 * an ESP-IDF error code.
 */
esp_err_t sd_card_driver_check(void);

/**
 * @brief Get the current SD card mount state.
 *
 * @param[out] mounted Set to true when the card is mounted;
 * otherwise set to false.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if mounted is NULL,
 * ESP_ERR_INVALID_STATE if the driver is not initialized, or
 * ESP_ERR_TIMEOUT if the driver lock cannot be acquired.
 */
esp_err_t sd_card_driver_get_mounted(
    bool *mounted
);

/**
 * @brief Write human-readable SD card information into a buffer.
 *
 * @param[out] buffer Destination text buffer.
 * @param[in] buffer_size Size of the destination buffer in bytes.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if buffer is NULL
 * or buffer_size is less than two bytes, ESP_ERR_INVALID_STATE if
 * the card is not mounted, ESP_ERR_TIMEOUT if the driver lock cannot
 * be acquired, otherwise an ESP-IDF error code.
 */
esp_err_t sd_card_driver_get_info_text(
    char *buffer,
    size_t buffer_size
);

#ifdef __cplusplus
}
#endif
