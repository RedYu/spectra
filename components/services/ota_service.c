/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "ota_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "cJSON.h"
#include "storage_sd_service.h"
#include "system_model.h"

#define OTA_SERVICE_LOCK_TIMEOUT_MS  (1000U)
#define OTA_SERVICE_SD_BUFFER_SIZE    (4U * 1024U)
#define OTA_SERVICE_BACKEND_TIMEOUT_MS       (20000U)
#define OTA_SERVICE_BACKEND_RESPONSE_SIZE    (1024U)

static const char *TAG = "ota_service";

static SemaphoreHandle_t s_mutex = NULL;

static const esp_partition_t *s_running_partition = NULL;
static const esp_partition_t *s_update_partition = NULL;

static esp_ota_handle_t s_ota_handle = 0U;
static bool s_transfer_active = false;

static ota_service_info_t s_info = {
    .state = OTA_SERVICE_STATE_UNINITIALIZED,
};

typedef struct
{
    char *data;
    size_t capacity;
    size_t size;

    int64_t request_started_us;
    int64_t connected_us;
    int64_t headers_sent_us;
    int64_t first_data_us;
    int64_t finished_us;

} ota_service_backend_response_t;

static esp_err_t ota_service_backend_event_handler(
    esp_http_client_event_t *event
)
{
    if (event == NULL) {
        return ESP_OK;
    }

    ota_service_backend_response_t *response =
        event->user_data;

    if (response == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const int64_t now_us = esp_timer_get_time();

    switch (event->event_id) {
        case HTTP_EVENT_ON_CONNECTED:
            response->connected_us = now_us;
            break;

        case HTTP_EVENT_HEADERS_SENT:
            response->headers_sent_us = now_us;
            break;

        case HTTP_EVENT_ON_DATA:
            if (response->first_data_us == 0) {
                response->first_data_us = now_us;
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            response->finished_us = now_us;
            break;

        default:
            break;
    }

    if ((event->event_id != HTTP_EVENT_ON_DATA) ||
        (event->data_len <= 0)) {

        return ESP_OK;
    }

    if (response->data == NULL) {

        return ESP_ERR_INVALID_STATE;
    }

    const size_t data_size =
        (size_t)event->data_len;

    if (data_size >
        (response->capacity - response->size - 1U)) {

        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(
        response->data + response->size,
        event->data,
        data_size
    );

    response->size += data_size;
    response->data[response->size] = '\0';

    return ESP_OK;
}

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

static bool ota_service_path_is_sd_update(
    const char *path
)
{
    if (path == NULL) {
        return false;
    }

    static const char prefix[] =
        OTA_SERVICE_SD_UPDATE_DIRECTORY "/";

    if (strncmp(
            path,
            prefix,
            sizeof(prefix) - 1U
        ) != 0) {

        return false;
    }

    const char *name =
        path + sizeof(prefix) - 1U;

    if ((name[0] == '\0') ||
        (strchr(name, '/') != NULL) ||
        (strchr(name, '\\') != NULL)) {

        return false;
    }

    const size_t name_length = strlen(name);

    if (name_length < 5U) {
        return false;
    }

    const char *extension =
        name + name_length - 4U;

    return
        (extension[0] == '.') &&
        ((extension[1] == 'b') ||
         (extension[1] == 'B')) &&
        ((extension[2] == 'i') ||
         (extension[2] == 'I')) &&
        ((extension[3] == 'n') ||
         (extension[3] == 'N'));
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

    (void)system_model_set_ota_available(true);

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

esp_err_t ota_service_install_from_sd(
    const char *path
)
{
    if (!ota_service_path_is_sd_update(path)) {
        return ESP_ERR_INVALID_ARG;
    }

    struct stat file_status;

    esp_err_t result =
        storage_sd_service_stat(
            path,
            &file_status
        );

    if (result != ESP_OK) {
        return result;
    }

    if (!S_ISREG(file_status.st_mode) ||
        (file_status.st_size <= 0)) {

        return ESP_ERR_INVALID_SIZE;
    }

    FILE *file = NULL;

    result = storage_sd_service_open(
        path,
        "rb",
        &file
    );

    if (result != ESP_OK) {
        return result;
    }

    result = ota_service_begin(
        (size_t)file_status.st_size
    );

    uint8_t *buffer = NULL;

    if (result == ESP_OK) {
        buffer = malloc(OTA_SERVICE_SD_BUFFER_SIZE);

        if (buffer == NULL) {
            result = ESP_ERR_NO_MEM;
        }
    }

    size_t total_read = 0U;

    while ((result == ESP_OK) &&
           (total_read < (size_t)file_status.st_size)) {

        const size_t remaining =
            (size_t)file_status.st_size - total_read;

        const size_t requested =
            remaining < OTA_SERVICE_SD_BUFFER_SIZE
                ? remaining
                : OTA_SERVICE_SD_BUFFER_SIZE;

        size_t bytes_read = 0U;

        result = storage_sd_service_read(
            file,
            buffer,
            requested,
            &bytes_read
        );

        if ((result == ESP_OK) &&
            (bytes_read == 0U)) {

            result = ESP_ERR_INVALID_SIZE;
        }

        if (result == ESP_OK) {
            result = ota_service_write(
                buffer,
                bytes_read
            );
        }

        total_read += bytes_read;
    }

    free(buffer);

    const esp_err_t close_result =
        storage_sd_service_close(&file);

    if ((result == ESP_OK) &&
        (close_result != ESP_OK)) {

        result = close_result;
    }

    if (result == ESP_OK) {
        result = ota_service_finish();
    }

    if (result != ESP_OK) {
        (void)ota_service_cancel();

        ESP_LOGE(
            TAG,
            "Failed to install SD image '%s': %s",
            path,
            esp_err_to_name(result)
        );

        return result;
    }

    ESP_LOGI(
        TAG,
        "SD image installed: path=%s, size=%u",
        path,
        (unsigned int)total_read
    );

    return ESP_OK;
}

esp_err_t ota_service_check_backend(void)
{
    esp_err_t result = ota_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (s_info.backend_state ==
        OTA_SERVICE_BACKEND_STATE_CHECKING) {

        ota_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    s_info.backend_state =
        OTA_SERVICE_BACKEND_STATE_CHECKING;

    s_info.backend_last_error = ESP_OK;
    s_info.backend_version[0] = '\0';

    ota_service_unlock();

    system_model_t system;

    result = system_model_get_snapshot(&system);

    char *response_buffer = NULL;
    esp_http_client_handle_t client = NULL;

    ota_service_backend_response_t response = {
        .capacity = OTA_SERVICE_BACKEND_RESPONSE_SIZE,
    };
    int64_t request_started_us = 0;

    if (result == ESP_OK) {
        response_buffer = calloc(
            1U,
            response.capacity
        );

        if (response_buffer == NULL) {
            result = ESP_ERR_NO_MEM;
        } else {
            response.data = response_buffer;
        }
    }

    if (result == ESP_OK) {
        const esp_http_client_config_t config = {
            .url = CONFIG_SPECTRA_OTA_BACKEND_CHECK_URL,
            .method = HTTP_METHOD_GET,
            .timeout_ms = OTA_SERVICE_BACKEND_TIMEOUT_MS,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .tls_version = ESP_HTTP_CLIENT_TLS_VER_TLS_1_2,
            .keep_alive_enable = false,
            .event_handler =
                ota_service_backend_event_handler,
            .user_data = &response,
        };

        client = esp_http_client_init(&config);

        if (client == NULL) {
            result = ESP_ERR_NO_MEM;
        }
    }

    if (result == ESP_OK) {
        result = esp_http_client_set_header(
            client,
            "Accept",
            "application/json"
        );
    }

    if (result == ESP_OK) {
        result = esp_http_client_set_header(
            client,
            "X-Spectra-Device-Id",
            system.device_id
        );
    }

    if (result == ESP_OK) {
        result = esp_http_client_set_header(
            client,
            "X-Spectra-Firmware-Version",
            system.firmware_version
        );
    }

    if (result == ESP_OK) {
        result = esp_http_client_set_header(
            client,
            "X-Spectra-Hardware-Version",
            system.hardware_version
        );
    }

    if (result == ESP_OK) {
        request_started_us = esp_timer_get_time();
        response.request_started_us = request_started_us;
        result = esp_http_client_perform(client);
    }

    if (request_started_us != 0) {
        const uint64_t elapsed_ms = (uint64_t)(
            (esp_timer_get_time() - request_started_us) /
            1000LL
        );

        ESP_LOGI(
            TAG,
            "Backend firmware check finished: result=%s, elapsed=%llu ms",
            esp_err_to_name(result),
            (unsigned long long)elapsed_ms
        );

        if ((response.connected_us != 0) &&
            (response.first_data_us != 0)) {

            const uint64_t connect_ms = (uint64_t)(
                (response.connected_us -
                 response.request_started_us) /
                1000LL
            );

            const int64_t response_started_us =
                (response.headers_sent_us != 0) ?
                response.headers_sent_us :
                response.connected_us;

            const uint64_t server_ms = (uint64_t)(
                (response.first_data_us -
                 response_started_us) /
                1000LL
            );

            const int64_t response_finished_us =
                (response.finished_us != 0) ?
                response.finished_us :
                esp_timer_get_time();

            const uint64_t receive_ms = (uint64_t)(
                (response_finished_us -
                 response.first_data_us) /
                1000LL
            );

            ESP_LOGI(
                TAG,
                "Backend timing: connect_tls=%llu ms, "
                "server=%llu ms, receive=%llu ms",
                (unsigned long long)connect_ms,
                (unsigned long long)server_ms,
                (unsigned long long)receive_ms
            );
        }
    }

    if (result == ESP_OK) {
        const int status_code =
            esp_http_client_get_status_code(client);

        if ((status_code < 200) ||
            (status_code >= 300)) {

            result = ESP_ERR_INVALID_RESPONSE;
        }
    }

    bool update_available = false;
    char available_version[
        OTA_SERVICE_VERSION_MAX_LENGTH
    ] = {0};

    if ((result == ESP_OK) &&
        (response.size > 0U)) {

        cJSON *manifest =
            cJSON_Parse(response.data);

        if (manifest != NULL) {
            const cJSON *available =
                cJSON_GetObjectItemCaseSensitive(
                    manifest,
                    "update_available"
                );

            update_available =
                cJSON_IsTrue(available);

            const cJSON *version =
                cJSON_GetObjectItemCaseSensitive(
                    manifest,
                    "version"
                );

            if (cJSON_IsString(version) &&
                (version->valuestring != NULL)) {

                (void)strlcpy(
                    available_version,
                    version->valuestring,
                    sizeof(available_version)
                );
            }

            cJSON_Delete(manifest);
        }
    }

    if (client != NULL) {
        esp_http_client_cleanup(client);
    }

    free(response_buffer);

    const esp_err_t lock_result =
        ota_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    s_info.backend_last_check_ms =
        (uint64_t)(esp_timer_get_time() / 1000LL);

    s_info.backend_last_error = result;

    if (result == ESP_OK) {
        s_info.backend_state =
            update_available
                ? OTA_SERVICE_BACKEND_STATE_UPDATE_AVAILABLE
                : OTA_SERVICE_BACKEND_STATE_NO_UPDATE;

        (void)strlcpy(
            s_info.backend_version,
            available_version,
            sizeof(s_info.backend_version)
        );

        (void)system_model_set_ota_available(
            update_available ||
            (s_info.state == OTA_SERVICE_STATE_READY)
        );

        if (update_available) {
            ESP_LOGI(
                TAG,
                "Backend firmware update available: %s",
                s_info.backend_version
            );
        } else {
            ESP_LOGI(
                TAG,
                "Backend reports no firmware update"
            );
        }
    } else {
        s_info.backend_state =
            OTA_SERVICE_BACKEND_STATE_ERROR;

        (void)system_model_set_ota_available(
            s_info.state == OTA_SERVICE_STATE_READY
        );

        ESP_LOGW(
            TAG,
            "Backend firmware check failed: %s",
            esp_err_to_name(result)
        );
    }

    ota_service_unlock();

    return result;
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

        (void)system_model_set_ota_available(false);

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
