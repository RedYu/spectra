/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "sd_card_driver.h"

#include <stdint.h>
#include <stdio.h>

#include "driver/sdspi_host.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdmmc_cmd.h"

#include "board.h"
#include "board_config.h"

#define SD_CARD_PROBE_SECTOR  (0U)
#define SD_CARD_PROBE_SIZE    (512U)

static const char *TAG = "sd_card_driver";

static sdmmc_card_t *s_card = NULL;
static SemaphoreHandle_t s_mutex = NULL;
static bool s_mounted = false;

DMA_ATTR static uint8_t s_probe_buffer[
    SD_CARD_PROBE_SIZE
];

esp_err_t sd_card_driver_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();

        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

esp_err_t sd_card_driver_mount(void)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!board_spi_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_mutex,
            pdMS_TO_TICKS(1000U)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    if (s_mounted && (s_card != NULL)) {
        (void)xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    if (s_mounted || (s_card != NULL)) {
        (void)xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    host.slot = board_spi_get_host();
    host.max_freq_khz = SD_SPI_CLOCK_KHZ;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();

    slot_config.host_id = board_spi_get_host();
    slot_config.gpio_cs = SD_PIN_CS;
    slot_config.gpio_cd = SDSPI_SLOT_NO_CD;
    slot_config.gpio_wp = SDSPI_SLOT_NO_WP;

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8U,
        .allocation_unit_size = 16U * 1024U,
    };

    if (!board_spi_lock(
            pdMS_TO_TICKS(3000U)
        )) {
        (void)xSemaphoreGive(s_mutex);
        return ESP_ERR_TIMEOUT;
    }

    const esp_err_t result =
        esp_vfs_fat_sdspi_mount(
            SD_CARD_MOUNT_POINT,
            &host,
            &slot_config,
            &mount_config,
            &s_card
        );

    board_spi_unlock();

    if (result == ESP_OK) {
        s_mounted = true;

        ESP_LOGI(
            TAG,
            "SD card mounted"
        );

        sdmmc_card_print_info(
            stdout,
            s_card
        );
    } else {
        s_card = NULL;
        s_mounted = false;

        ESP_LOGW(
            TAG,
            "SD card mount failed: %s",
            esp_err_to_name(result)
        );
    }

    (void)xSemaphoreGive(s_mutex);

    return result;
}

esp_err_t sd_card_driver_check(void)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_mutex,
            pdMS_TO_TICKS(500U)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    if (!s_mounted || (s_card == NULL)) {
        (void)xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    if (!board_spi_lock(
            pdMS_TO_TICKS(2000U)
        )) {
        (void)xSemaphoreGive(s_mutex);
        return ESP_ERR_TIMEOUT;
    }

    const esp_err_t result =
        sdmmc_read_sectors(
            s_card,
            s_probe_buffer,
            SD_CARD_PROBE_SECTOR,
            1U
        );

    board_spi_unlock();

    (void)xSemaphoreGive(s_mutex);

    return result;
}

esp_err_t sd_card_driver_unmount(void)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_mutex,
            pdMS_TO_TICKS(1000U)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    if (!s_mounted && (s_card == NULL)) {
        (void)xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    if (!s_mounted || (s_card == NULL)) {
        (void)xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    if (!board_spi_lock(
            pdMS_TO_TICKS(3000U)
        )) {
        (void)xSemaphoreGive(s_mutex);
        return ESP_ERR_TIMEOUT;
    }

    const esp_err_t result =
        esp_vfs_fat_sdcard_unmount(
            SD_CARD_MOUNT_POINT,
            s_card
        );

    board_spi_unlock();

    if (result == ESP_OK) {
        s_card = NULL;
        s_mounted = false;

        ESP_LOGI(
            TAG,
            "SD card unmounted"
        );
    } else {
        ESP_LOGE(
            TAG,
            "Failed to unmount SD card: %s",
            esp_err_to_name(result)
        );
    }

    (void)xSemaphoreGive(s_mutex);

    return result;
}

esp_err_t sd_card_driver_get_mounted(
    bool *mounted
)
{
    if (mounted == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *mounted = false;

    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_mutex,
            pdMS_TO_TICKS(100U)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    *mounted =
        s_mounted && (s_card != NULL);

    (void)xSemaphoreGive(s_mutex);

    return ESP_OK;
}

esp_err_t sd_card_driver_get_info_text(
    char *buffer,
    size_t buffer_size
)
{
    if ((buffer == NULL) ||
        (buffer_size < 2U)) {

        return ESP_ERR_INVALID_ARG;
    }

    buffer[0] = '\0';

    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_mutex,
            pdMS_TO_TICKS(1000U)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = ESP_OK;

    if (!s_mounted || (s_card == NULL)) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        FILE *stream = fmemopen(
            buffer,
            buffer_size,
            "w"
        );

        if (stream == NULL) {
            result = ESP_FAIL;
        } else {
            sdmmc_card_print_info(
                stream,
                s_card
            );

            if (fflush(stream) != 0) {
                result = ESP_FAIL;
            }

            if (fclose(stream) != 0) {
                result = ESP_FAIL;
            }

            buffer[buffer_size - 1U] = '\0';
        }
    }

    (void)xSemaphoreGive(s_mutex);

    return result;
}
