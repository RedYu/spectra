/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "lvgl_psram_pool.h"

#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"

#if LV_USE_STDLIB_MALLOC != LV_STDLIB_BUILTIN
#error "The PSRAM pool requires the LVGL built-in allocator"
#endif

#if LV_MEM_ADR != 0
#error "The PSRAM pool requires LV_MEM_ADR to be zero"
#endif

static const char *TAG =
    "lvgl_psram_pool";

static void *s_pool = NULL;

esp_err_t lvgl_psram_pool_prepare(void)
{
    if (s_pool != NULL) {
        return ESP_OK;
    }

    s_pool =
        heap_caps_malloc(
            LV_MEM_SIZE,
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

    if (s_pool == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to allocate LVGL pool in PSRAM: %u bytes",
            (unsigned int)LV_MEM_SIZE
        );

        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "LVGL pool allocated in PSRAM: %u bytes",
        (unsigned int)LV_MEM_SIZE
    );

    return ESP_OK;
}

void *lvgl_psram_pool_get(
    size_t size
)
{
    /*
     * lv_init() must never enter TLSF with a NULL or undersized pool.
     * Normal allocation failure is handled by prepare() before lv_init().
     */
    if ((s_pool == NULL) ||
        (size != LV_MEM_SIZE)) {

        ESP_LOGE(
            TAG,
            "LVGL pool was not prepared or has an unexpected size"
        );

        abort();
    }

    return s_pool;
}
