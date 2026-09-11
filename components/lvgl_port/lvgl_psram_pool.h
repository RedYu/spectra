/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Allocate the process-lifetime LVGL memory pool in PSRAM.
 *
 * This function must be called by the serialized LVGL initialization
 * path before lv_init(). Repeated calls succeed without allocating a
 * second pool.
 *
 * @return ESP_OK when the pool is available or ESP_ERR_NO_MEM when
 * the PSRAM allocation fails.
 */
esp_err_t lvgl_psram_pool_prepare(void);

/**
 * @brief Return the prepared memory pool to the LVGL allocator.
 *
 * This function is used as the LV_MEM_POOL_ALLOC hook. LVGL owns the
 * contents of the returned pool, while the backing allocation remains
 * valid until the device reboots.
 *
 * The function aborts when the pool has not been prepared or size does
 * not match LV_MEM_SIZE, because continuing would initialize the LVGL
 * allocator with invalid memory.
 *
 * @param[in] size Pool size requested by LVGL.
 *
 * @return Pointer to the prepared PSRAM memory pool.
 */
void *lvgl_psram_pool_get(
    size_t size
);

#ifdef __cplusplus
}
#endif
