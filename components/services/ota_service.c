/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "ota_service.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "sdkconfig.h"

#define OTA_SERVICE_LOCK_TIMEOUT_MS  (1000U)

static const char *TAG = "ota_service";

static SemaphoreHandle_t s_mutex = NULL;

static const esp_partition_t *s_running_partition = NULL;
static const esp_partition_t *s_update_partition = NULL;

static esp_ota_handle_t s_ota_handle = 0U;
static bool s_transfer_active = false;

static ota_service_info_t s_info = {
    .state = OTA_SERVICE_STATE_UNINITIALIZED,
};

static ota_service_image_state_t ota_service_convert_image_state(
    esp_ota_img_states_t state
)
{
    switch (state) {
        case ESP_OTA_IMG_NEW:
            return OTA_SERVICE_IMAGE_STATE_NEW;

        case ESP_OTA_IMG_PENDING_VERIFY:
            return OTA_SERVICE_IMAGE_STATE_PENDING_VERIFY;

        case ESP_OTA_IMG_VALID:
            return OTA_SERVICE_IMAGE_STATE_VALID;

        case ESP_OTA_IMG_INVALID:
            return OTA_SERVICE_IMAGE_STATE_INVALID;

        case ESP_OTA_IMG_ABORTED:
            return OTA_SERVICE_IMAGE_STATE_ABORTED;

        case ESP_OTA_IMG_UNDEFINED:
        default:
            return OTA_SERVICE_IMAGE_STATE_UNKNOWN;
    }
}

static esp_err_t ota_service_refresh_running_image_state(void)
{
    esp_ota_img_states_t image_state =
        ESP_OTA_IMG_UNDEFINED;

    const esp_err_t result =
        esp_ota_get_state_partition(
            s_running_partition,
            &image_state
        );

    if ((result == ESP_ERR_NOT_SUPPORTED) ||
        (result == ESP_ERR_NOT_FOUND)) {

        s_info.running_image_state =
            OTA_SERVICE_IMAGE_STATE_UNKNOWN;

        s_info.verification_pending = false;

        return ESP_OK;
    }

    if (result != ESP_OK) {
        return result;
    }

    s_info.running_image_state =
        ota_service_convert_image_state(image_state);

    s_info.verification_pending =
        image_state == ESP_OTA_IMG_PENDING_VERIFY;

    return ESP_OK;
}

static esp_err_t ota_service_lock(void)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_mutex,
            pdMS_TO_TICKS(
                OTA_SERVICE_LOCK_TIMEOUT_MS
            )
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void ota_service_unlock(void)
{
    if (s_mutex != NULL) {
        (void)xSemaphoreGive(s_mutex);
    }
}

static void ota_service_copy_partition_label(
    char *destination,
    size_t destination_size,
    const esp_partition_t *partition
)
{
    if ((destination == NULL) ||
        (destination_size == 0U)) {

        return;
    }

    (void)snprintf(
        destination,
        destination_size,
        "%s",
        partition != NULL
            ? partition->label
            : ""
    );
}

static void ota_service_copy_fixed_text(
    char *destination,
    size_t destination_size,
    const char *source,
    size_t source_size
)
{
    if ((destination == NULL) ||
        (destination_size == 0U)) {

        return;
    }

    const size_t maximum_size =
        destination_size - 1U;

    const size_t copy_size =
        source_size < maximum_size
            ? source_size
            : maximum_size;

    memcpy(
        destination,
        source,
        copy_size
    );

    destination[copy_size] = '\0';
}

static void ota_service_update_boot_partition(void)
{
    ota_service_copy_partition_label(
        s_info.boot_partition,
        sizeof(s_info.boot_partition),
        esp_ota_get_boot_partition()
    );
}

static void ota_service_clear_image_info(void)
{
    s_info.image_size = 0U;
    s_info.written_size = 0U;
    s_info.progress_percent = 0U;
    s_info.project_name[0] = '\0';
    s_info.version[0] = '\0';
}

static esp_err_t ota_service_fail(
    esp_err_t error,
    bool abort_transfer
)
{
    if (abort_transfer && s_transfer_active) {
        const esp_err_t abort_result =
            esp_ota_abort(s_ota_handle);

        if ((abort_result != ESP_OK) &&
            (error == ESP_OK)) {

            error = abort_result;
        }
    }

    s_transfer_active = false;
    s_ota_handle = 0U;
    s_info.state = OTA_SERVICE_STATE_ERROR;
    s_info.last_error = error;

    return error;
}

esp_err_t ota_service_init(void)
{
    if (s_mutex != NULL) {
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();

    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_running_partition =
        esp_ota_get_running_partition();

    s_update_partition =
        esp_ota_get_next_update_partition(NULL);

    if ((s_running_partition == NULL) ||
        (s_update_partition == NULL)) {

        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;

        return ESP_ERR_NOT_FOUND;
    }

    memset(&s_info, 0, sizeof(s_info));

    s_info.initialized = true;
    s_info.rollback_enabled =
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
        true;
#else
        false;
#endif

    s_info.state = OTA_SERVICE_STATE_IDLE;
    s_info.update_partition_size =
        s_update_partition->size;

    s_info.update_partition_address =
        s_update_partition->address;

    ota_service_copy_partition_label(
        s_info.running_partition,
        sizeof(s_info.running_partition),
        s_running_partition
    );

    ota_service_copy_partition_label(
        s_info.update_partition,
        sizeof(s_info.update_partition),
        s_update_partition
    );

    ota_service_update_boot_partition();

    const esp_err_t state_result =
        ota_service_refresh_running_image_state();

    if (state_result != ESP_OK) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;

        return state_result;
    }

    ESP_LOGI(
        TAG,
        "OTA service initialized: running=%s, update=%s, size=%u, "
        "rollback=%s, verification=%s",
        s_info.running_partition,
        s_info.update_partition,
        (unsigned int)s_info.update_partition_size,
        s_info.rollback_enabled
            ? "enabled"
            : "disabled",
        s_info.verification_pending
            ? "pending"
            : "not required"
    );

    return ESP_OK;
}

esp_err_t ota_service_begin(
    size_t image_size
)
{
    esp_err_t result = ota_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (s_info.state != OTA_SERVICE_STATE_IDLE) {
        ota_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    if ((image_size < sizeof(esp_image_header_t)) ||
        (image_size > s_update_partition->size)) {

        ota_service_unlock();

        return ESP_ERR_INVALID_SIZE;
    }

    ota_service_clear_image_info();

    result = esp_ota_begin(
        s_update_partition,
        image_size,
        &s_ota_handle
    );

    if (result != ESP_OK) {
        s_info.state = OTA_SERVICE_STATE_ERROR;
        s_info.last_error = result;
        ota_service_unlock();

        return result;
    }

    s_transfer_active = true;
    s_info.state = OTA_SERVICE_STATE_RECEIVING;
    s_info.image_size = image_size;
    s_info.last_error = ESP_OK;

    ESP_LOGI(
        TAG,
        "OTA transfer started: partition=%s, size=%u",
        s_update_partition->label,
        (unsigned int)image_size
    );

    ota_service_unlock();

    return ESP_OK;
}

esp_err_t ota_service_write(
    const void *data,
    size_t size
)
{
    if ((data == NULL) ||
        (size == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ota_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if ((s_info.state != OTA_SERVICE_STATE_RECEIVING) ||
        !s_transfer_active) {

        ota_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    if (size >
        (s_info.image_size - s_info.written_size)) {

        ota_service_unlock();

        return ESP_ERR_INVALID_SIZE;
    }

    result = esp_ota_write(
        s_ota_handle,
        data,
        size
    );

    if (result != ESP_OK) {
        result = ota_service_fail(
            result,
            true
        );

        ota_service_unlock();

        return result;
    }

    s_info.written_size += size;

    s_info.progress_percent =
        (uint8_t)(
            (s_info.written_size * 100U) /
            s_info.image_size
        );

    ota_service_unlock();

    return ESP_OK;
}

esp_err_t ota_service_finish(void)
{
    esp_err_t result = ota_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if ((s_info.state != OTA_SERVICE_STATE_RECEIVING) ||
        !s_transfer_active) {

        ota_service_unlock();

        return ESP_ERR_INVALID_STATE;
    }

    if (s_info.written_size != s_info.image_size) {
        ota_service_unlock();

        return ESP_ERR_INVALID_SIZE;
    }

    result = esp_ota_end(s_ota_handle);
    s_transfer_active = false;
    s_ota_handle = 0U;

    if (result != ESP_OK) {
        result = ota_service_fail(
            result,
            false
        );

        ota_service_unlock();

        return result;
    }

    esp_app_desc_t image_description;

    result = esp_ota_get_partition_description(
        s_update_partition,
        &image_description
    );

    if (result != ESP_OK) {
        result = ota_service_fail(
            result,
            false
        );

        ota_service_unlock();

        return result;
    }

    const esp_app_desc_t *running_description =
        esp_app_get_description();

    if ((running_description == NULL) ||
        (strncmp(
            image_description.project_name,
            running_description->project_name,
            sizeof(image_description.project_name)
        ) != 0)) {

        result = ota_service_fail(
            ESP_ERR_INVALID_VERSION,
            false
        );

        ota_service_unlock();

        return result;
    }

    ota_service_copy_fixed_text(
        s_info.project_name,
        sizeof(s_info.project_name),
        image_description.project_name,
        sizeof(image_description.project_name)
    );

    ota_service_copy_fixed_text(
        s_info.version,
        sizeof(s_info.version),
        image_description.version,
        sizeof(image_description.version)
    );

    result = esp_ota_set_boot_partition(
        s_update_partition
    );

    if (result != ESP_OK) {
        result = ota_service_fail(
            result,
            false
        );

        ota_service_unlock();

        return result;
    }

    s_info.progress_percent = 100U;
    s_info.state = OTA_SERVICE_STATE_READY;
    s_info.last_error = ESP_OK;
    ota_service_update_boot_partition();

    ESP_LOGI(
        TAG,
        "OTA image ready: project=%s, version=%s, partition=%s",
        s_info.project_name,
        s_info.version,
        s_info.update_partition
    );

    ota_service_unlock();

    return ESP_OK;
}

esp_err_t ota_service_cancel(void)
{
    esp_err_t result = ota_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (s_transfer_active) {
        result = esp_ota_abort(s_ota_handle);

        s_transfer_active = false;
        s_ota_handle = 0U;
    } else if (s_info.state == OTA_SERVICE_STATE_READY) {
        result = esp_ota_set_boot_partition(
            s_running_partition
        );
    } else {
        result = ESP_OK;
    }

    if (result == ESP_OK) {
        ota_service_clear_image_info();
        s_info.state = OTA_SERVICE_STATE_IDLE;
        s_info.last_error = ESP_OK;
        ota_service_update_boot_partition();

        ESP_LOGI(TAG, "OTA update cancelled");
    } else {
        s_info.state = OTA_SERVICE_STATE_ERROR;
        s_info.last_error = result;
    }

    ota_service_unlock();

    return result;
}

esp_err_t ota_service_confirm_running_image(void)
{
    esp_err_t result = ota_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    result = ota_service_refresh_running_image_state();

    if ((result == ESP_OK) &&
        s_info.verification_pending) {

        result =
            esp_ota_mark_app_valid_cancel_rollback();

        if (result == ESP_OK) {
            result =
                ota_service_refresh_running_image_state();
        }
    }

    if (result == ESP_OK) {
        ESP_LOGI(
            TAG,
            "Running application image confirmed"
        );
    } else {
        s_info.last_error = result;

        ESP_LOGE(
            TAG,
            "Failed to confirm running application image: %s",
            esp_err_to_name(result)
        );
    }

    ota_service_unlock();

    return result;
}

esp_err_t ota_service_get_info(
    ota_service_info_t *info
)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t result = ota_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    *info = s_info;

    ota_service_unlock();

    return ESP_OK;
}
