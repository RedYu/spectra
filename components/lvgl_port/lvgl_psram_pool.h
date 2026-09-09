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

/*
 * Allocate the process-lifetime LVGL pool before lv_init().
 * Called only by the serialized LVGL initialization path.
 */
esp_err_t lvgl_psram_pool_prepare(void);

/*
 * LV_MEM_POOL_ALLOC hook. Returns the already prepared pool.
 * LVGL owns its contents; the backing allocation lives until reboot.
 */
void *lvgl_psram_pool_get(
    size_t size
);

#ifdef __cplusplus
}
#endif
