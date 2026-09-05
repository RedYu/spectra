/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "system_model.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define SYSTEM_MODEL_LOCK_TIMEOUT_MS  (100U)
#define SYSTEM_CPU_USAGE_MAX          (100U)

static system_model_t s_model;
static SemaphoreHandle_t s_mutex = NULL;

static esp_err_t system_model_lock(void)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_mutex,
            pdMS_TO_TICKS(
                SYSTEM_MODEL_LOCK_TIMEOUT_MS
            )
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void system_model_unlock(void)
{
    (void)xSemaphoreGive(
        s_mutex
    );
}

static void system_model_validate(
    system_model_t *model
)
{
    model->device_id[
        SYSTEM_DEVICE_ID_MAX_LENGTH - 1U
    ] = '\0';

    model->firmware_version[
        SYSTEM_FW_VERSION_MAX_LENGTH - 1U
    ] = '\0';

    model->hardware_version[
        SYSTEM_HW_VERSION_MAX_LENGTH - 1U
    ] = '\0';

    model->serial_number[
        SYSTEM_SERIAL_MAX_LENGTH - 1U
    ] = '\0';

    model->device_name[
        SYSTEM_DEVICE_NAME_MAX_LENGTH - 1U
    ] = '\0';

    model->chip_model[
        SYSTEM_CHIP_MODEL_MAX_LENGTH - 1U
    ] = '\0';

    if (model->cpu_usage >
        SYSTEM_CPU_USAGE_MAX) {

        model->cpu_usage =
            SYSTEM_CPU_USAGE_MAX;
    }

    if (!model->chip_temperature_valid ||
        !isfinite(
            model->chip_temperature_celsius
        )) {

        model->chip_temperature_celsius =
            0.0F;

        model->chip_temperature_valid =
            false;
    }
}

esp_err_t system_model_init(void)
{
    if (s_mutex != NULL) {
        return ESP_OK;
    }

    s_mutex =
        xSemaphoreCreateMutex();

    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    memset(
        &s_model,
        0,
        sizeof(s_model)
    );

    return ESP_OK;
}

esp_err_t system_model_set(
    const system_model_t *model
)
{
    if (model == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    system_model_t validated =
        *model;

    system_model_validate(
        &validated
    );

    const esp_err_t result =
        system_model_lock();

    if (result != ESP_OK) {
        return result;
    }

    s_model = validated;

    system_model_unlock();

    return ESP_OK;
}

esp_err_t system_model_get_snapshot(
    system_model_t *out_model
)
{
    if (out_model == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(
        out_model,
        0,
        sizeof(*out_model)
    );

    const esp_err_t result =
        system_model_lock();

    if (result != ESP_OK) {
        return result;
    }

    *out_model =
        s_model;

    system_model_unlock();

    return ESP_OK;
}

esp_err_t system_model_set_hardware_info(
    const char *chip_model,
    uint8_t chip_cores,
    uint16_t chip_revision,
    uint32_t cpu_frequency_mhz,
    uint32_t flash_size,
    uint32_t psram_size,
    esp_reset_reason_t reset_reason
)
{
    if (chip_model == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t result =
        system_model_lock();

    if (result != ESP_OK) {
        return result;
    }

    (void)strlcpy(
        s_model.chip_model,
        chip_model,
        sizeof(s_model.chip_model)
    );

    s_model.chip_cores =
        chip_cores;

    s_model.chip_revision =
        chip_revision;

    s_model.cpu_frequency_mhz =
        cpu_frequency_mhz;

    s_model.flash_size =
        flash_size;

    s_model.psram_size =
        psram_size;

    s_model.reset_reason =
        reset_reason;

    system_model_unlock();

    return ESP_OK;
}

esp_err_t system_model_set_storage_ready(
    bool ready
)
{
    const esp_err_t result =
        system_model_lock();

    if (result != ESP_OK) {
        return result;
    }

    s_model.storage_ready =
        ready;

    system_model_unlock();

    return ESP_OK;
}

esp_err_t system_model_set_sd_card_mounted(
    bool mounted
)
{
    const esp_err_t result =
        system_model_lock();

    if (result != ESP_OK) {
        return result;
    }

    s_model.sd_card_mounted =
        mounted;

    system_model_unlock();

    return ESP_OK;
}

esp_err_t system_model_set_ota_available(
    bool available
)
{
    const esp_err_t result =
        system_model_lock();

    if (result != ESP_OK) {
        return result;
    }

    s_model.ota_available =
        available;

    system_model_unlock();

    return ESP_OK;
}

esp_err_t system_model_set_internet_available(
    bool available
)
{
    const esp_err_t result =
        system_model_lock();

    if (result != ESP_OK) {
        return result;
    }

    s_model.internet_available =
        available;

    system_model_unlock();

    return ESP_OK;
}

esp_err_t system_model_set_runtime(
    uint32_t uptime_sec,
    uint32_t free_heap,
    uint32_t minimum_free_heap,
    uint32_t psram_free,
    uint32_t psram_minimum_free,
    uint8_t cpu_usage
)
{
    const esp_err_t result =
        system_model_lock();

    if (result != ESP_OK) {
        return result;
    }

    s_model.uptime_sec =
        uptime_sec;

    s_model.free_heap =
        free_heap;

    s_model.minimum_free_heap =
        minimum_free_heap;

    s_model.psram_free =
        psram_free;

    s_model.psram_minimum_free =
        psram_minimum_free;

    if (cpu_usage >
        SYSTEM_CPU_USAGE_MAX) {

        s_model.cpu_usage =
            SYSTEM_CPU_USAGE_MAX;
    } else {
        s_model.cpu_usage =
            cpu_usage;
    }

    system_model_unlock();

    return ESP_OK;
}

esp_err_t system_model_set_chip_temperature(
    float temperature_celsius,
    bool valid
)
{
    if (valid &&
        !isfinite(temperature_celsius)) {

        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t result =
        system_model_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (valid) {
        s_model.chip_temperature_celsius =
            temperature_celsius;

        s_model.chip_temperature_valid =
            true;
    } else {
        s_model.chip_temperature_celsius =
            0.0F;

        s_model.chip_temperature_valid =
            false;
    }

    system_model_unlock();

    return ESP_OK;
}
