/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "storage_sd_benchmark.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "storage_sd_service.h"
#include "sd_card_driver.h"

#define STORAGE_SD_BENCHMARK_SECTOR_SIZE (512U)

#define STORAGE_SD_BENCHMARK_MAX_BLOCK_SIZE (64U * 1024U)

static const char *TAG = "storage_sd_benchmark";

static esp_err_t storage_sd_benchmark_raw_read(
    uint8_t *buffer,
    size_t total_size,
    size_t block_size,
    storage_sd_benchmark_result_t *result
);

static uint8_t storage_sd_benchmark_pattern(size_t offset)
{
    return (uint8_t)(((offset * 33U) + 17U) & 0xFFU);
}

static void storage_sd_benchmark_fill(
    uint8_t *buffer,
    size_t size,
    size_t file_offset
)
{
    for (size_t index = 0U; index < size; ++index) {
        buffer[index] = storage_sd_benchmark_pattern(file_offset + index);
    }
}

static bool storage_sd_benchmark_verify(
    const uint8_t *buffer,
    size_t size,
    size_t file_offset
)
{
    for (size_t index = 0U; index < size; ++index) {
        if (buffer[index] !=
            storage_sd_benchmark_pattern(file_offset + index)) {

            return false;
        }
    }

    return true;
}

static uint64_t storage_sd_benchmark_speed(
    size_t bytes,
    uint64_t elapsed_us
)
{
    if (elapsed_us == 0U) {
        return 0U;
    }

    return ((uint64_t)bytes * UINT64_C(1000000)) / elapsed_us;
}

static bool storage_sd_benchmark_config_valid(
    const storage_sd_benchmark_config_t *config
)
{
    return (config != NULL) &&
           (config->file_path != NULL) &&
           (config->file_path[0] != '\0') &&
           (config->file_size > 0U) &&
           (config->block_size > 0U) &&
           (config->block_size <=
            STORAGE_SD_BENCHMARK_MAX_BLOCK_SIZE) &&
           ((config->file_size %
             STORAGE_SD_BENCHMARK_SECTOR_SIZE) == 0U) &&
           ((config->block_size %
             STORAGE_SD_BENCHMARK_SECTOR_SIZE) == 0U);
}

esp_err_t storage_sd_benchmark_run(
    const storage_sd_benchmark_config_t *config,
    storage_sd_benchmark_result_t *result
)
{
    esp_err_t operation_result;

    if (!storage_sd_benchmark_config_valid(config) ||
        (result == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));
    result->tested_bytes = config->file_size;
    result->block_size = config->block_size;

    uint8_t *buffer = heap_caps_malloc(
        config->block_size,
        MALLOC_CAP_INTERNAL |
        MALLOC_CAP_DMA |
        MALLOC_CAP_8BIT
    );

    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    operation_result =
        storage_sd_benchmark_raw_read(
            buffer,
            config->file_size,
            config->block_size,
            result
        );

    if (operation_result != ESP_OK) {
        free(buffer);
        return operation_result;
    }

    FILE *file = NULL;
    bool benchmark_file_created = false;
    operation_result = storage_sd_service_open(
        config->file_path,
        "wb",
        &file
    );

    if (operation_result != ESP_OK) {
        free(buffer);
        return operation_result;
    }

    benchmark_file_created = true;

    size_t offset = 0U;
    const int64_t write_started_us = esp_timer_get_time();

    while (offset < config->file_size) {
        const size_t remaining = config->file_size - offset;
        const size_t chunk_size =
            remaining < config->block_size
                ? remaining
                : config->block_size;

        storage_sd_benchmark_fill(buffer, chunk_size, offset);

        size_t written = 0U;
        const int64_t block_started_us = esp_timer_get_time();

        operation_result = storage_sd_service_write(
            file,
            buffer,
            chunk_size,
            &written
        );

        const uint64_t block_time_us = (uint64_t)(
            esp_timer_get_time() - block_started_us
        );

        if (block_time_us > result->maximum_write_block_time_us) {
            result->maximum_write_block_time_us = block_time_us;
        }

        if (operation_result != ESP_OK) {
            goto cleanup;
        }

        if (written != chunk_size) {
            operation_result = ESP_FAIL;
            goto cleanup;
        }

        offset += written;
    }

    const int64_t flush_started_us = esp_timer_get_time();
    operation_result = storage_sd_service_flush(file);
    result->flush_time_us = (uint64_t)(
        esp_timer_get_time() - flush_started_us
    );

    if (operation_result != ESP_OK) {
        goto cleanup;
    }

    const int64_t sync_started_us = esp_timer_get_time();
    operation_result = storage_sd_service_sync(file);
    result->sync_time_us = (uint64_t)(
        esp_timer_get_time() - sync_started_us
    );

    if (operation_result != ESP_OK) {
        goto cleanup;
    }

    result->write_time_us = (uint64_t)(
        esp_timer_get_time() - write_started_us
    );
    result->write_speed_bytes_per_second = storage_sd_benchmark_speed(
        config->file_size,
        result->write_time_us
    );

    operation_result = storage_sd_service_close(&file);

    if (operation_result != ESP_OK) {
        goto cleanup;
    }

    operation_result = storage_sd_service_open(
        config->file_path,
        "rb",
        &file
    );

    if (operation_result != ESP_OK) {
        goto cleanup;
    }

    offset = 0U;
    const int64_t read_started_us = esp_timer_get_time();

    while (offset < config->file_size) {
        const size_t remaining = config->file_size - offset;
        const size_t chunk_size =
            remaining < config->block_size
                ? remaining
                : config->block_size;

        size_t bytes_read = 0U;
        const int64_t block_started_us = esp_timer_get_time();

        operation_result = storage_sd_service_read(
            file,
            buffer,
            chunk_size,
            &bytes_read
        );

        const uint64_t block_time_us = (uint64_t)(
            esp_timer_get_time() - block_started_us
        );

        if (block_time_us > result->maximum_read_block_time_us) {
            result->maximum_read_block_time_us = block_time_us;
        }

        if (operation_result != ESP_OK) {
            goto cleanup;
        }

        if (bytes_read != chunk_size) {
            operation_result = ESP_ERR_INVALID_SIZE;
            goto cleanup;
        }

        if (!storage_sd_benchmark_verify(buffer, bytes_read, offset)) {
            operation_result = ESP_ERR_INVALID_RESPONSE;
            goto cleanup;
        }

        offset += bytes_read;
    }

    result->read_time_us = (uint64_t)(
        esp_timer_get_time() - read_started_us
    );
    result->read_speed_bytes_per_second = storage_sd_benchmark_speed(
        config->file_size,
        result->read_time_us
    );
    result->data_verified = true;

cleanup:
    if (file != NULL) {
        const esp_err_t close_result =
            storage_sd_service_close(&file);

        if (operation_result == ESP_OK) {
            operation_result = close_result;
        }
    }

    if (config->remove_file_after_test &&
        benchmark_file_created) {
        const esp_err_t remove_result =
            storage_sd_service_remove(config->file_path);

        if ((operation_result == ESP_OK) &&
            (remove_result != ESP_OK)) {

            operation_result = remove_result;
        }
    }

    free(buffer);

    if (operation_result == ESP_OK) {
       ESP_LOGI(
            TAG,
            "SD benchmark passed: bytes=%u, block=%u, "
            "write=%llu B/s, read=%llu B/s, "
            "raw_read=%llu B/s, "
            "flush=%llu us, sync=%llu us, "
            "max_write=%llu us, max_read=%llu us, "
            "max_raw_read=%llu us",
            (unsigned int)result->tested_bytes,
            (unsigned int)result->block_size,
            (unsigned long long)
                result->write_speed_bytes_per_second,
            (unsigned long long)
                result->read_speed_bytes_per_second,
            (unsigned long long)
                result->raw_read_speed_bytes_per_second,
            (unsigned long long)
                result->flush_time_us,
            (unsigned long long)
                result->sync_time_us,
            (unsigned long long)
                result->maximum_write_block_time_us,
            (unsigned long long)
                result->maximum_read_block_time_us,
            (unsigned long long)
                result->maximum_raw_read_block_time_us
        );
    } else {
        ESP_LOGE(
            TAG,
            "SD benchmark failed at offset=%u: %s",
            (unsigned int)offset,
            esp_err_to_name(operation_result)
        );
    }

    return operation_result;
}

static esp_err_t storage_sd_benchmark_raw_read(
    uint8_t *buffer,
    size_t total_size,
    size_t block_size,
    storage_sd_benchmark_result_t *result
)
{
    if ((buffer == NULL) ||
        (result == NULL) ||
        (total_size == 0U) ||
        (block_size == 0U) ||
        ((total_size %
        STORAGE_SD_BENCHMARK_SECTOR_SIZE) != 0U) ||
        ((block_size %
        STORAGE_SD_BENCHMARK_SECTOR_SIZE) != 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    size_t offset = 0U;
    size_t sector = 0U;

    const int64_t started_us =
        esp_timer_get_time();

    while (offset < total_size) {
        const size_t remaining =
            total_size - offset;

        size_t current_size =
            remaining < block_size
                ? remaining
                : block_size;

        current_size -=
            current_size %
            STORAGE_SD_BENCHMARK_SECTOR_SIZE;

        if (current_size == 0U) {
            break;
        }

        const size_t current_sector_count =
            current_size /
            STORAGE_SD_BENCHMARK_SECTOR_SIZE;

        const int64_t block_started_us =
            esp_timer_get_time();

        const esp_err_t operation_result =
            sd_card_driver_read_sectors(
                buffer,
                sector,
                current_sector_count
            );

        const uint64_t block_time_us =
            (uint64_t)(
                esp_timer_get_time() -
                block_started_us
            );

        if (block_time_us >
            result->maximum_raw_read_block_time_us) {

            result->maximum_raw_read_block_time_us =
                block_time_us;
        }

        if (operation_result != ESP_OK) {
            return operation_result;
        }

        offset += current_size;
        sector += current_sector_count;
    }

    result->raw_read_tested_bytes = offset;

    result->raw_read_time_us =
        (uint64_t)(
            esp_timer_get_time() -
            started_us
        );

    result->raw_read_speed_bytes_per_second =
        storage_sd_benchmark_speed(
            offset,
            result->raw_read_time_us
        );

    return ESP_OK;
}
