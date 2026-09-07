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

#define STORAGE_SD_BENCHMARK_DEFAULT_FILE_SIZE  (8U * 1024U * 1024U)
#define STORAGE_SD_BENCHMARK_DEFAULT_BLOCK_SIZE (16U * 1024U)

/**
 * @brief SD-card benchmark configuration.
 */
typedef struct
{
    const char *file_path;
    size_t file_size;
    size_t block_size;
    bool remove_file_after_test;

} storage_sd_benchmark_config_t;

/**
 * @brief SD-card benchmark measurements.
 *
 * Speeds are expressed in bytes per second. The write speed includes
 * writing, fflush() and fsync(), so it represents committed data rather
 * than only the C library buffer-copy rate.
 */
typedef struct
{
    size_t tested_bytes;
    size_t block_size;

    uint64_t write_time_us;
    uint64_t write_speed_bytes_per_second;
    uint64_t maximum_write_block_time_us;
    uint64_t flush_time_us;
    uint64_t sync_time_us;

    uint64_t read_time_us;
    uint64_t read_speed_bytes_per_second;
    uint64_t maximum_read_block_time_us;

    bool data_verified;

    /*
     * Non-destructive direct sector-read measurements. These bypass
     * FATFS and VFS but still use the configured SDSPI host.
     */
    size_t raw_read_tested_bytes;

    uint64_t raw_read_time_us;
    uint64_t raw_read_speed_bytes_per_second;
    uint64_t maximum_raw_read_block_time_us;

} storage_sd_benchmark_result_t;

/**
 * @brief Run raw-sector and filesystem SD-card benchmarks.
 *
 * First performs a non-destructive sequential raw-sector read through
 * the SDMMC/SDSPI driver. It then creates the configured benchmark
 * file, measures sequential write and synchronized commit performance,
 * reopens the file, measures sequential read performance and verifies
 * its contents.
 *
 * The filesystem part is destructive only for the configured file_path:
 * an existing file at that path is overwritten. The path must therefore
 * refer to a dedicated benchmark file. Other SD-card users should be
 * stopped before running the benchmark.
 *
 * file_size and block_size must be non-zero multiples of 512 bytes.
 * block_size must not exceed the implementation-defined maximum.
 *
 * @param[in] config Benchmark configuration.
 * @param[out] result Benchmark measurements.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid arguments,
 * ESP_ERR_NO_MEM if the DMA buffer cannot be allocated,
 * ESP_ERR_INVALID_RESPONSE if filesystem data verification fails,
 * otherwise an error returned by the SD-card driver or storage service.
 */
esp_err_t storage_sd_benchmark_run(
    const storage_sd_benchmark_config_t *config,
    storage_sd_benchmark_result_t *result
);

#ifdef __cplusplus
}
#endif
