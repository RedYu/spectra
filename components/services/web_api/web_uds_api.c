/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "web_uds_api.h"

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_task_priorities.h"
#include "firmware_image.h"
#include "uds_client.h"
#include "uds_did_catalog_service.h"
#include "uds_download.h"
#include "uds_profile_service.h"
#include "uds_security_provider.h"
#include "storage_sd_service.h"
#include "time_service.h"
#include "web_api_common.h"

#define WEB_UDS_BUFFER_SIZE       (1024U)
#define WEB_UDS_BODY_MAX_SIZE     (4096U)
#define WEB_UDS_LOCK_TIMEOUT_MS   (100U)
#define WEB_UDS_WORKER_TASK_STACK_SIZE    (4096U)
#define WEB_UDS_WORKER_PERIOD_MS          (10U)
#define WEB_UDS_TESTER_PRESENT_DEFAULT_INTERVAL_MS (2000U)
#define WEB_UDS_TESTER_PRESENT_MIN_INTERVAL_MS     (250U)
#define WEB_UDS_TESTER_PRESENT_MAX_INTERVAL_MS     (10000U)
#define WEB_UDS_FIRMWARE_DIRECTORY       "/firmwares/"
#define WEB_UDS_FIRMWARE_MAX_SIZE        (64U * 1024U * 1024U)
#define WEB_UDS_FIRMWARE_INITIAL_SEGMENTS  (16U)
#define WEB_UDS_FIRMWARE_MAX_SEGMENTS      (4096U)
#define WEB_UDS_JOURNAL_DIRECTORY        "/logs/firmware"
#define WEB_UDS_JOURNAL_PATH_MAX_SIZE    (128U)
#define WEB_UDS_JOURNAL_LINE_MAX_SIZE    (384U)
#define WEB_UDS_PROFILE_LIST_CAPACITY      (8U)
#define WEB_UDS_DID_CATALOG_LIST_CAPACITY  (8U)
#define WEB_UDS_QUERY_MAX_SIZE            (192U)

typedef enum
{
    WEB_UDS_SECURITY_IDLE = 0,
    WEB_UDS_SECURITY_WAITING_SEED,
    WEB_UDS_SECURITY_CALCULATING_KEY,
    WEB_UDS_SECURITY_SENDING_KEY,
    WEB_UDS_SECURITY_UNLOCKED,
    WEB_UDS_SECURITY_ERROR,

} web_uds_security_state_t;

typedef struct
{
    bool enabled;
    size_t offset;
    uint8_t pending_value;
    uint8_t success_value;

} web_uds_routine_status_policy_t;

typedef struct
{
    uint64_t address;
    uint64_t size;

} web_uds_firmware_segment_t;

static const char *TAG = "web_uds_api";

static SemaphoreHandle_t s_lock = NULL;
static uint8_t *s_receive_buffer = NULL;
static uint8_t *s_transmit_buffer = NULL;
static uint8_t *s_response_buffer = NULL;
static uint8_t *s_manual_request_buffer = NULL;
static size_t s_response_size = 0U;
static uds_client_t s_client;
static uds_client_config_t s_client_config;
static bool s_client_config_valid = false;
static uds_security_provider_t s_security_provider;
static bool s_security_provider_initialized = false;
static uint8_t s_security_level = 0U;
static web_uds_security_state_t s_security_state =
    WEB_UDS_SECURITY_IDLE;
static esp_err_t s_security_result = ESP_OK;
static uint8_t s_security_negative_response_code = 0U;
static uint32_t s_security_generation = 0U;
static uint32_t s_security_active_generation = 0U;
static uint8_t s_security_pending_key[
    UDS_SECURITY_PROVIDER_MANUAL_KEY_SIZE
];
static size_t s_security_pending_key_length = 0U;
static bool s_security_key_ready = false;
static uds_download_t s_download;
static bool s_download_active = false;
static FILE *s_download_file = NULL;
static FILE *s_download_journal = NULL;
static uint64_t s_download_source_file_offset = 0U;
static uint64_t s_download_image_offset = 0U;
static firmware_image_reader_t *s_download_image_reader = NULL;
static firmware_image_block_t s_download_image_block;
static size_t s_download_image_block_offset = 0U;
static web_uds_firmware_segment_t *s_download_segments = NULL;
static uint32_t s_download_segment_count = 0U;
static uint32_t s_download_stream_segment = 0U;
static uint64_t s_download_stream_segment_offset = 0U;
static uint64_t s_download_started_at_us = 0U;
static uint64_t s_download_finished_at_us = 0U;
static uint32_t s_download_logged_retries = 0U;
static bool s_download_journal_finalized = false;
static char s_download_journal_path[
    WEB_UDS_JOURNAL_PATH_MAX_SIZE
] = {0};
static uint8_t *s_download_transfer_buffer = NULL;
static uint8_t s_download_security_seed[
    UDS_SECURITY_PROVIDER_MANUAL_KEY_SIZE
];
static uint8_t s_download_security_key[
    UDS_SECURITY_PROVIDER_MANUAL_KEY_SIZE
];
static uint8_t s_download_erase_record[
    UDS_DOWNLOAD_ROUTINE_RECORD_MAX_LENGTH
];
static size_t s_download_erase_record_length = 0U;
static uint8_t s_download_verify_record[
    UDS_DOWNLOAD_ROUTINE_RECORD_MAX_LENGTH
];
static size_t s_download_verify_record_length = 0U;
static web_uds_routine_status_policy_t
    s_download_erase_status_policy;
static web_uds_routine_status_policy_t
    s_download_verify_status_policy;
static TaskHandle_t s_worker_task = NULL;
static bool s_tester_present_enabled = true;
static bool s_tester_present_session_active = false;
static bool s_tester_present_request_pending = false;
static bool s_tester_present_deferred = false;
static uint8_t s_diagnostic_session = 1U;
static uint8_t s_pending_diagnostic_session = 0U;
static uint64_t s_tester_present_interval_us =
    (uint64_t)WEB_UDS_TESTER_PRESENT_DEFAULT_INTERVAL_MS * 1000ULL;
static uint64_t s_tester_present_deadline_us = 0U;
static uint32_t s_tester_present_sent = 0U;
static uint32_t s_tester_present_deferred_count = 0U;
static esp_err_t s_tester_present_last_result = ESP_OK;
static uint32_t s_sequence = 0U;
static uds_client_event_type_t s_last_event =
    UDS_CLIENT_EVENT_TRANSMITTED;
static esp_err_t s_last_result = ESP_OK;
static isotp_session_error_t s_transport_error =
    ISOTP_SESSION_ERROR_NONE;
static bool s_positive = false;
static uint8_t s_response_service_id = 0U;
static uint8_t s_request_service_id = 0U;
static uint8_t s_negative_response_code = 0U;

static bool web_uds_number(
    const cJSON *root,
    const char *name,
    uint32_t maximum,
    uint32_t *value
);

static bool web_uds_boolean(
    const cJSON *root,
    const char *name,
    bool fallback,
    bool *value
);

static bool web_uds_hex_uint64(
    const cJSON *root,
    const char *name,
    uint64_t *value
);

static esp_err_t web_uds_receive_json(
    httpd_req_t *request,
    cJSON **root
);

static esp_err_t web_uds_parse_hex(
    const char *text,
    uint8_t *buffer,
    size_t capacity,
    size_t *size
);

static void web_uds_erase(
    void *data,
    size_t size
);

static char *web_uds_format_hex(
    const uint8_t *data,
    size_t size
);

static void web_uds_client_callback(
    const uds_client_event_t *event,
    void *context
);

static void web_uds_security_reset(void);

static uint32_t web_uds_security_next_generation(void);

static bool web_uds_security_busy(void);

static void web_uds_security_finish_error(
    esp_err_t result,
    uint8_t negative_response_code
);

static esp_err_t web_uds_security_unlock(
    const cJSON *root
);

static void web_uds_security_submit_key(void);

static esp_err_t web_uds_security_cancel(void);

static esp_err_t web_uds_configure(
    const cJSON *root
);

static esp_err_t web_uds_request(
    const cJSON *root
);

static esp_err_t web_uds_download_read(
    uint64_t offset,
    uint8_t *buffer,
    size_t capacity,
    size_t *read_size,
    void *context
);

static esp_err_t web_uds_firmware_source_read(
    uint64_t offset,
    uint8_t *buffer,
    size_t capacity,
    size_t *read_size,
    void *context
);

static esp_err_t web_uds_download_segment(
    uint32_t index,
    uint64_t *address,
    uint64_t *size,
    void *context
);

static esp_err_t web_uds_prepare_firmware_image(
    const char *path,
    uint64_t file_size,
    uint64_t binary_address,
    firmware_image_info_t *info
);

static uds_download_routine_result_t
web_uds_download_evaluate_routine_result(
    uint16_t routine_identifier,
    const uint8_t *status_record,
    size_t status_record_length,
    void *context
);

static void web_uds_download_close_file(void);

static esp_err_t web_uds_download_open_journal(
    const char *firmware_path,
    uint64_t firmware_size,
    uint64_t memory_address,
    uint8_t data_format_identifier
);

static esp_err_t web_uds_download_write_journal(
    const char *format,
    ...
);

static void web_uds_download_log_progress(
    const uds_download_progress_t *progress
);

static void web_uds_download_finalize_journal(
    const uds_download_progress_t *progress,
    const char *outcome
);

static esp_err_t web_uds_download_start(
    const cJSON *root
);

static bool web_uds_firmware_path_valid(
    const char *path
);

static void web_uds_worker_task(
    void *context
);

static void web_uds_tester_present_stop(void);

static void web_uds_tester_present_note_activity(
    uint64_t now_us
);

static void web_uds_tester_present_poll(
    uint64_t now_us
);

static esp_err_t web_uds_get_handler(
    httpd_req_t *request
);

static esp_err_t web_uds_profile_get_handler(
    httpd_req_t *request
);

static esp_err_t web_uds_profile_post_handler(
    httpd_req_t *request
);

static esp_err_t web_uds_profile_remove(
    const cJSON *root
);

static esp_err_t web_uds_did_catalog_get_handler(
    httpd_req_t *request
);

static esp_err_t web_uds_did_catalog_post_handler(
    httpd_req_t *request
);

static esp_err_t web_uds_did_catalog_remove(
    const cJSON *root
);

static esp_err_t web_uds_post_handler(
    httpd_req_t *request
);

static bool web_uds_number(
    const cJSON *root,
    const char *name,
    uint32_t maximum,
    uint32_t *value
)
{
    const cJSON *item =
        cJSON_GetObjectItemCaseSensitive(root, name);

    if (!cJSON_IsNumber(item) ||
        (item->valuedouble < 0.0) ||
        (item->valuedouble > maximum) ||
        ((double)(uint32_t)item->valuedouble !=
         item->valuedouble)) {

        return false;
    }

    *value = (uint32_t)item->valuedouble;
    return true;
}

static bool web_uds_boolean(
    const cJSON *root,
    const char *name,
    bool fallback,
    bool *value
)
{
    const cJSON *item =
        cJSON_GetObjectItemCaseSensitive(root, name);

    if (item == NULL) {
        *value = fallback;
        return true;
    }

    if (!cJSON_IsBool(item)) {
        return false;
    }

    *value = cJSON_IsTrue(item);
    return true;
}

static bool web_uds_hex_uint64(
    const cJSON *root,
    const char *name,
    uint64_t *value
)
{
    const cJSON *item =
        cJSON_GetObjectItemCaseSensitive(root, name);

    if (!cJSON_IsString(item) ||
        (item->valuestring[0] == '\0')) {

        return false;
    }

    uint64_t parsed = 0U;

    for (const char *cursor = item->valuestring;
         *cursor != '\0';
         ++cursor) {

        uint8_t digit = 0U;

        if ((*cursor >= '0') && (*cursor <= '9')) {
            digit = (uint8_t)(*cursor - '0');
        } else if ((*cursor >= 'A') && (*cursor <= 'F')) {
            digit = (uint8_t)(*cursor - 'A' + 10);
        } else if ((*cursor >= 'a') && (*cursor <= 'f')) {
            digit = (uint8_t)(*cursor - 'a' + 10);
        } else {
            return false;
        }

        if (parsed > ((UINT64_MAX - digit) >> 4U)) {
            return false;
        }

        parsed = (parsed << 4U) | digit;
    }

    *value = parsed;
    return true;
}

static esp_err_t web_uds_receive_json(
    httpd_req_t *request,
    cJSON **root
)
{
    if ((request == NULL) ||
        (root == NULL) ||
        (request->content_len == 0U) ||
        (request->content_len > WEB_UDS_BODY_MAX_SIZE)) {

        return ESP_ERR_INVALID_SIZE;
    }

    char *body = heap_caps_malloc(
        request->content_len + 1U,
        MALLOC_CAP_SPIRAM |
        MALLOC_CAP_8BIT
    );

    if (body == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t received = 0U;

    while (received < request->content_len) {
        const int count =
            httpd_req_recv(
                request,
                body + received,
                request->content_len - received
            );

        if (count <= 0) {
            heap_caps_free(body);
            return ESP_FAIL;
        }

        received += (size_t)count;
    }

    body[received] = '\0';
    *root =
        cJSON_ParseWithLengthOpts(
            body,
            received + 1U,
            NULL,
            true
        );

    heap_caps_free(body);

    return (*root != NULL)
        ? ESP_OK
        : ESP_ERR_INVALID_ARG;
}

static void web_uds_log_did_catalog_memory(
    const char *operation,
    const char *stage
)
{
    const uint32_t capabilities =
        MALLOC_CAP_INTERNAL |
        MALLOC_CAP_8BIT;

    ESP_LOGI(
        TAG,
        "DID catalog %s %s: internal=%u, largest=%u, minimum=%u",
        operation,
        stage,
        (unsigned int)heap_caps_get_free_size(capabilities),
        (unsigned int)heap_caps_get_largest_free_block(capabilities),
        (unsigned int)heap_caps_get_minimum_free_size(capabilities)
    );
}

static esp_err_t web_uds_receive_catalog_json(
    httpd_req_t *request,
    char **json
)
{
    if ((request == NULL) ||
        (json == NULL) ||
        (request->content_len == 0U) ||
        (request->content_len >
         UDS_DID_CATALOG_FILE_MAX_SIZE)) {

        return ESP_ERR_INVALID_SIZE;
    }

    *json = heap_caps_malloc(
        request->content_len + 1U,
        MALLOC_CAP_SPIRAM |
        MALLOC_CAP_8BIT
    );

    if (*json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t received = 0U;

    while (received < request->content_len) {
        const int count = httpd_req_recv(
            request,
            *json + received,
            request->content_len - received
        );

        if (count <= 0) {
            heap_caps_free(*json);
            *json = NULL;
            return ESP_FAIL;
        }

        received += (size_t)count;
    }

    (*json)[received] = '\0';
    return ESP_OK;
}

static esp_err_t web_uds_receive_profile_json(
    httpd_req_t *request,
    char **json
)
{
    if ((request == NULL) ||
        (json == NULL) ||
        (request->content_len == 0U) ||
        (request->content_len > UDS_PROFILE_FILE_MAX_SIZE)) {

        return ESP_ERR_INVALID_SIZE;
    }

    *json = heap_caps_malloc(
        request->content_len + 1U,
        MALLOC_CAP_SPIRAM |
        MALLOC_CAP_8BIT
    );

    if (*json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t received = 0U;

    while (received < request->content_len) {
        const int count = httpd_req_recv(
            request,
            *json + received,
            request->content_len - received
        );

        if (count <= 0) {
            heap_caps_free(*json);
            *json = NULL;
            return ESP_FAIL;
        }

        received += (size_t)count;
    }

    (*json)[received] = '\0';
    return ESP_OK;
}

static esp_err_t web_uds_parse_hex(
    const char *text,
    uint8_t *buffer,
    size_t capacity,
    size_t *size
)
{
    if ((text == NULL) ||
        (buffer == NULL) ||
        (size == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *size = 0U;
    int high = -1;

    while (*text != '\0') {
        if (isspace((unsigned char)*text) ||
            (*text == ',') || (*text == ':') ||
            (*text == '-')) {

            ++text;
            continue;
        }

        int value = -1;

        if ((*text >= '0') && (*text <= '9')) {
            value = *text - '0';
        } else if ((*text >= 'A') && (*text <= 'F')) {
            value = *text - 'A' + 10;
        } else if ((*text >= 'a') && (*text <= 'f')) {
            value = *text - 'a' + 10;
        } else {
            return ESP_ERR_INVALID_ARG;
        }

        if (high < 0) {
            high = value;
        } else {
            if (*size >= capacity) {
                return ESP_ERR_INVALID_SIZE;
            }

            buffer[*size] = (uint8_t)((high << 4) | value);
            (*size)++;
            high = -1;
        }

        ++text;
    }

    return (high < 0)
        ? ESP_OK
        : ESP_ERR_INVALID_ARG;
}

static void web_uds_erase(
    void *data,
    size_t size
)
{
    volatile uint8_t *bytes =
        (volatile uint8_t *)data;

    while (size > 0U) {
        *bytes = 0U;
        bytes++;
        size--;
    }
}

static char *web_uds_format_hex(
    const uint8_t *data,
    size_t size
)
{
    if ((data == NULL) || (size == 0U)) {
        return strdup("");
    }

    char *text = malloc((size * 3U) + 1U);

    if (text == NULL) {
        return NULL;
    }

    static const char digits[] = "0123456789ABCDEF";
    size_t offset = 0U;

    for (size_t index = 0U; index < size; ++index) {
        if (index != 0U) {
            text[offset++] = ' ';
        }

        text[offset++] = digits[data[index] >> 4U];
        text[offset++] = digits[data[index] & 0x0FU];
    }

    text[offset] = '\0';
    return text;
}

static void web_uds_security_reset(void)
{
    if (s_security_provider_initialized) {
        uds_security_provider_clear_manual_key(
            &s_security_provider
        );
    }

    s_security_generation =
        web_uds_security_next_generation();
    s_security_active_generation = 0U;
    s_security_level = 0U;
    s_security_state = WEB_UDS_SECURITY_IDLE;
    s_security_result = ESP_OK;
    s_security_negative_response_code = 0U;
    web_uds_erase(
        s_security_pending_key,
        sizeof(s_security_pending_key)
    );
    s_security_pending_key_length = 0U;
    s_security_key_ready = false;
}

static uint32_t web_uds_security_next_generation(void)
{
    uint32_t generation = s_security_generation + 1U;

    if (generation == 0U) {
        generation = 1U;
    }

    return generation;
}

static bool web_uds_security_busy(void)
{
    return (s_security_state ==
            WEB_UDS_SECURITY_WAITING_SEED) ||
           (s_security_state ==
            WEB_UDS_SECURITY_CALCULATING_KEY) ||
           (s_security_state ==
            WEB_UDS_SECURITY_SENDING_KEY);
}

static void web_uds_security_finish_error(
    esp_err_t result,
    uint8_t negative_response_code
)
{
    if (s_security_provider_initialized) {
        uds_security_provider_clear_manual_key(
            &s_security_provider
        );
    }

    web_uds_erase(
        s_security_pending_key,
        sizeof(s_security_pending_key)
    );

    s_security_pending_key_length = 0U;
    s_security_key_ready = false;
    s_security_state = WEB_UDS_SECURITY_ERROR;
    s_security_result = result;
    s_security_negative_response_code =
        negative_response_code;
}

static esp_err_t web_uds_security_unlock(
    const cJSON *root
)
{
    const cJSON *data =
        cJSON_GetObjectItemCaseSensitive(root, "data");
    const cJSON *key =
        cJSON_GetObjectItemCaseSensitive(root, "key");
    uint32_t security_level = 0U;

    if (!s_security_provider_initialized ||
        web_uds_security_busy() ||
        !web_uds_number(
            root,
            "value",
            UDS_SECURITY_ACCESS_LEVEL_MAX,
            &security_level
        ) ||
        ((security_level & 1U) == 0U) ||
        !cJSON_IsString(data) ||
        !cJSON_IsString(key)) {

        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data_record[UDS_CLIENT_SECURITY_DATA_MAX_LENGTH];
    size_t data_record_length = 0U;
    uint8_t manual_key[UDS_SECURITY_PROVIDER_MANUAL_KEY_SIZE];
    size_t manual_key_length = 0U;

    esp_err_t result =
        web_uds_parse_hex(
            data->valuestring,
            data_record,
            sizeof(data_record),
            &data_record_length
        );

    if (result == ESP_OK) {
        result =
            web_uds_parse_hex(
                key->valuestring,
                manual_key,
                sizeof(manual_key),
                &manual_key_length
            );
    }

    if ((result == ESP_OK) && (manual_key_length == 0U)) {
        result = ESP_ERR_INVALID_ARG;
    }

    if (result == ESP_OK) {
        result =
            uds_security_provider_set_manual_key(
                &s_security_provider,
                manual_key,
                manual_key_length
            );
    }

    web_uds_erase(
        manual_key,
        sizeof(manual_key)
    );

    if (result == ESP_OK) {
        result =
            uds_client_security_access_request_seed(
                &s_client,
                (uint8_t)security_level,
                data_record,
                data_record_length,
                esp_timer_get_time()
            );
    }

    if (result == ESP_OK) {
        s_security_generation =
            web_uds_security_next_generation();
        s_security_active_generation =
            s_security_generation;
        s_security_level = (uint8_t)security_level;
        s_security_state = WEB_UDS_SECURITY_WAITING_SEED;
        s_security_negative_response_code = 0U;
    } else {
        web_uds_security_finish_error(
            result,
            0U
        );
    }

    s_security_result = result;
    return result;
}

static void web_uds_security_submit_key(void)
{
    if (!s_security_key_ready ||
        (s_security_state !=
         WEB_UDS_SECURITY_CALCULATING_KEY) ||
        (s_security_active_generation == 0U) ||
        (s_security_active_generation !=
         s_security_generation)) {

        if (s_security_key_ready) {
            web_uds_security_reset();
        }

        return;
    }

    const esp_err_t result =
        uds_client_security_access_send_key(
            &s_client,
            s_security_level,
            s_security_pending_key,
            s_security_pending_key_length,
            esp_timer_get_time()
        );

    web_uds_erase(
        s_security_pending_key,
        sizeof(s_security_pending_key)
    );

    s_security_pending_key_length = 0U;
    s_security_key_ready = false;
    s_security_result = result;

    if (result == ESP_OK) {
        s_security_state = WEB_UDS_SECURITY_SENDING_KEY;
    } else {
        web_uds_security_finish_error(
            result,
            0U
        );
    }
}

static esp_err_t web_uds_security_cancel(void)
{
    if (!web_uds_security_busy() ||
        !s_client_config_valid) {

        return ESP_ERR_INVALID_STATE;
    }

    web_uds_security_reset();

    esp_err_t result =
        uds_client_close(&s_client);

    if (result == ESP_OK) {
        result =
            uds_client_open(
                &s_client,
                &s_client_config
            );
    }

    if (result != ESP_OK) {
        web_uds_security_finish_error(
            result,
            0U
        );
    }

    s_sequence++;
    return result;
}

static void web_uds_client_callback(
    const uds_client_event_t *event,
    void *context
)
{
    (void)context;

    if ((event == NULL) || (s_lock == NULL)) {
        return;
    }

    if (xSemaphoreTakeRecursive(
            s_lock,
            pdMS_TO_TICKS(WEB_UDS_LOCK_TIMEOUT_MS)
        ) != pdTRUE) {

        return;
    }

    const uint64_t now_us = esp_timer_get_time();

    if (s_tester_present_request_pending &&
        (s_client.request_service_id == UDS_SERVICE_TESTER_PRESENT)) {

        s_tester_present_request_pending = false;
        s_tester_present_last_result = event->result;
        web_uds_tester_present_note_activity(now_us);
        s_sequence++;
        xSemaphoreGiveRecursive(s_lock);
        return;
    }

    s_last_event = event->type;
    s_last_result = event->result;
    s_transport_error = event->transport_error;
    s_response_size = 0U;
    s_positive = false;
    s_response_service_id = 0U;
    s_negative_response_code = 0U;

    if ((event->type == UDS_CLIENT_EVENT_RESPONSE) ||
        (event->type == UDS_CLIENT_EVENT_NEGATIVE_RESPONSE) ||
        (event->type == UDS_CLIENT_EVENT_RESPONSE_PENDING)) {

        s_positive = event->response.positive;
        s_response_service_id = event->response.service_id;
        s_request_service_id = event->response.request_service_id;
        s_negative_response_code =
            event->response.negative_response_code;
        s_response_size =
            (event->response.payload_length < WEB_UDS_BUFFER_SIZE)
                ? event->response.payload_length
                : WEB_UDS_BUFFER_SIZE;

        if (s_response_size != 0U) {
            memcpy(
                s_response_buffer,
                event->response.payload,
                s_response_size
            );
        }
    }

    if ((s_client.request_service_id ==
         UDS_SERVICE_DIAGNOSTIC_SESSION_CONTROL) &&
        (((event->type == UDS_CLIENT_EVENT_RESPONSE) &&
          event->response.positive) ||
         ((event->type == UDS_CLIENT_EVENT_TRANSMITTED) &&
          (s_pending_diagnostic_session != 0U)))) {

        const uint8_t session =
            (event->type == UDS_CLIENT_EVENT_RESPONSE) &&
            (event->response.payload_length != 0U)
                ? (event->response.payload[0] & 0x7FU)
                : s_pending_diagnostic_session;

        s_diagnostic_session = session;
        s_tester_present_session_active = session != 1U;
        s_pending_diagnostic_session = 0U;
        web_uds_tester_present_note_activity(now_us);
    } else if ((s_client.request_service_id ==
                UDS_SERVICE_DIAGNOSTIC_SESSION_CONTROL) &&
               ((event->type == UDS_CLIENT_EVENT_NEGATIVE_RESPONSE) ||
                (event->type == UDS_CLIENT_EVENT_TIMEOUT) ||
                (event->type == UDS_CLIENT_EVENT_TRANSPORT_ERROR) ||
                (event->type == UDS_CLIENT_EVENT_PROTOCOL_ERROR))) {

        s_pending_diagnostic_session = 0U;
        web_uds_tester_present_note_activity(now_us);
    } else if ((s_client.request_service_id ==
                UDS_SERVICE_ECU_RESET) &&
               ((event->type == UDS_CLIENT_EVENT_RESPONSE) ||
                (event->type == UDS_CLIENT_EVENT_TRANSMITTED))) {

        web_uds_tester_present_stop();
    } else {
        web_uds_tester_present_note_activity(now_us);
    }

    if ((s_security_state ==
         WEB_UDS_SECURITY_WAITING_SEED) &&
        (s_security_active_generation ==
         s_security_generation) &&
        (event->type == UDS_CLIENT_EVENT_RESPONSE)) {

        const bool valid_seed =
            (event->response.service_id ==
             (UDS_SERVICE_SECURITY_ACCESS +
              UDS_POSITIVE_RESPONSE_OFFSET)) &&
            (event->response.payload_length >= 1U) &&
            (event->response.payload[0] == s_security_level);

        if (!valid_seed) {
            web_uds_security_finish_error(
                ESP_ERR_INVALID_RESPONSE,
                0U
            );
        } else if (event->response.payload_length == 1U) {
            uds_security_provider_clear_manual_key(
                &s_security_provider
            );
            s_security_state = WEB_UDS_SECURITY_UNLOCKED;
            s_security_result = ESP_OK;
        } else {
            s_security_state =
                WEB_UDS_SECURITY_CALCULATING_KEY;

            const esp_err_t result =
                uds_security_provider_calculate(
                    s_security_level,
                    &event->response.payload[1],
                    event->response.payload_length - 1U,
                    s_security_pending_key,
                    sizeof(s_security_pending_key),
                    &s_security_pending_key_length,
                    &s_security_provider
                );

            s_security_key_ready = result == ESP_OK;
            s_security_result = result;

            if (result != ESP_OK) {
                web_uds_security_finish_error(
                    result,
                    0U
                );
            }
        }
    } else if ((s_security_state ==
                WEB_UDS_SECURITY_SENDING_KEY) &&
               (s_security_active_generation ==
                s_security_generation) &&
               (event->type == UDS_CLIENT_EVENT_RESPONSE)) {

        const bool valid_key_response =
            (event->response.service_id ==
             (UDS_SERVICE_SECURITY_ACCESS +
              UDS_POSITIVE_RESPONSE_OFFSET)) &&
            (event->response.payload_length >= 1U) &&
            (event->response.payload[0] ==
             (uint8_t)(s_security_level + 1U));

        if (valid_key_response) {
            s_security_state = WEB_UDS_SECURITY_UNLOCKED;
            s_security_result = ESP_OK;
        } else {
            web_uds_security_finish_error(
                ESP_ERR_INVALID_RESPONSE,
                0U
            );
        }
    } else if (web_uds_security_busy() &&
               ((event->type ==
                 UDS_CLIENT_EVENT_NEGATIVE_RESPONSE) ||
                (event->type == UDS_CLIENT_EVENT_TIMEOUT) ||
                (event->type ==
                 UDS_CLIENT_EVENT_TRANSPORT_ERROR) ||
                (event->type ==
                 UDS_CLIENT_EVENT_PROTOCOL_ERROR))) {

        const esp_err_t security_result =
            (event->result != ESP_OK)
                ? event->result
                : ESP_FAIL;

        web_uds_security_finish_error(
            security_result,
            event->response.negative_response_code
        );
    }

    s_sequence++;
    xSemaphoreGiveRecursive(s_lock);
}

static void web_uds_tester_present_stop(void)
{
    s_tester_present_session_active = false;
    s_tester_present_request_pending = false;
    s_tester_present_deferred = false;
    s_diagnostic_session = 1U;
    s_pending_diagnostic_session = 0U;
    s_tester_present_deadline_us = 0U;
}

static void web_uds_tester_present_note_activity(
    uint64_t now_us
)
{
    if (s_tester_present_enabled &&
        s_tester_present_session_active) {

        s_tester_present_deadline_us =
            now_us + s_tester_present_interval_us;
    }

    s_tester_present_deferred = false;
}

static void web_uds_tester_present_poll(
    uint64_t now_us
)
{
    if (!s_tester_present_enabled ||
        !s_tester_present_session_active ||
        (s_client.state == UDS_CLIENT_CLOSED) ||
        (now_us < s_tester_present_deadline_us)) {

        return;
    }

    if (uds_client_busy(&s_client)) {
        if (!s_tester_present_deferred) {
            s_tester_present_deferred = true;
            s_tester_present_deferred_count++;
        }

        return;
    }

    s_tester_present_request_pending = true;
    const esp_err_t result =
        uds_client_tester_present(
            &s_client,
            true,
            now_us
        );

    s_tester_present_last_result = result;

    if (result == ESP_OK) {
        s_tester_present_sent++;
        s_tester_present_deadline_us =
            now_us + s_tester_present_interval_us;
    } else {
        s_tester_present_request_pending = false;
        s_tester_present_deadline_us =
            now_us + 100000ULL;
    }
}

static esp_err_t web_uds_configure(
    const cJSON *root
)
{
    uint32_t bus = 0U;
    uint32_t receive_identifier = 0U;
    uint32_t transmit_identifier = 0U;
    uint32_t link_data_length = 0U;
    uint32_t block_size = 0U;
    uint32_t st_min = 0U;
    uint32_t p2_ms = 0U;
    uint32_t p2_star_ms = 0U;
    uint32_t tester_present_interval_ms =
        WEB_UDS_TESTER_PRESENT_DEFAULT_INTERVAL_MS;
    bool extended = false;
    bool can_fd = false;
    bool brs = false;
    bool functional = false;
    bool tester_present_enabled = true;

    const cJSON *tester_present_interval =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "tester_present_interval_ms"
        );

    if ((tester_present_interval != NULL) &&
        (!web_uds_number(
            root,
            "tester_present_interval_ms",
            WEB_UDS_TESTER_PRESENT_MAX_INTERVAL_MS,
            &tester_present_interval_ms
        ) ||
         (tester_present_interval_ms <
          WEB_UDS_TESTER_PRESENT_MIN_INTERVAL_MS))) {

        return ESP_ERR_INVALID_ARG;
    }

    if (!web_uds_number(root, "bus", 1U, &bus) ||
        !web_uds_number(
            root,
            "rx_id",
            CAN_FRAME_EXTENDED_ID_MAX,
            &receive_identifier
        ) ||
        !web_uds_number(
            root,
            "tx_id",
            CAN_FRAME_EXTENDED_ID_MAX,
            &transmit_identifier
        ) ||
        !web_uds_number(
            root,
            "link_data_length",
            CAN_FRAME_FD_DATA_MAX_LENGTH,
            &link_data_length
        ) ||
        !web_uds_number(root, "block_size", UINT8_MAX, &block_size) ||
        !web_uds_number(root, "st_min", UINT8_MAX, &st_min) ||
        !web_uds_number(root, "p2_ms", 60000U, &p2_ms) ||
        !web_uds_number(root, "p2_star_ms", 60000U, &p2_star_ms) ||
        !web_uds_boolean(root, "extended", false, &extended) ||
        !web_uds_boolean(root, "fd", false, &can_fd) ||
        !web_uds_boolean(root, "brs", false, &brs) ||
        !web_uds_boolean(
            root,
            "functional",
            false,
            &functional
        ) ||
        !web_uds_boolean(
            root,
            "tester_present_enabled",
            true,
            &tester_present_enabled
        ) ||
        (p2_ms == 0U) || (p2_star_ms == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (s_client.state != UDS_CLIENT_CLOSED) {
        const esp_err_t close_result =
            uds_client_close(&s_client);

        if (close_result != ESP_OK) {
            return close_result;
        }
    }

    const uds_client_config_t config = {
        .transport = {
            .bus = (can_bus_id_t)bus,
            .receive_identifier = receive_identifier,
            .transmit_identifier = transmit_identifier,
            .extended_identifier = extended,
            .can_fd = can_fd,
            .bit_rate_switch = brs,
            .session = {
                .link_data_length = (uint8_t)link_data_length,
                .functional_transmit = functional,
                .receive_block_size = (uint8_t)block_size,
                .receive_st_min = (uint8_t)st_min,
                .maximum_wait_frames = 3U,
                .flow_control_timeout_us =
                    (uint64_t)p2_star_ms * 1000ULL,
                .consecutive_frame_timeout_us =
                    (uint64_t)p2_star_ms * 1000ULL,
            },
            .receive_buffer = s_receive_buffer,
            .receive_capacity = WEB_UDS_BUFFER_SIZE,
            .transmit_buffer = s_transmit_buffer,
            .transmit_capacity = WEB_UDS_BUFFER_SIZE,
        },
        .p2_timeout_us = (uint64_t)p2_ms * 1000ULL,
        .p2_star_timeout_us = (uint64_t)p2_star_ms * 1000ULL,
        .callback = web_uds_client_callback,
        .callback_context = NULL,
    };

    const esp_err_t result =
        uds_client_open(&s_client, &config);

    if (result == ESP_OK) {
        s_client_config = config;
        s_client_config_valid = true;
        s_tester_present_enabled =
            tester_present_enabled &&
            !functional;
        s_tester_present_interval_us =
            (uint64_t)tester_present_interval_ms * 1000ULL;
        web_uds_tester_present_stop();
        s_last_result = ESP_OK;
        s_response_size = 0U;
        s_negative_response_code = 0U;
        web_uds_security_reset();
        s_sequence++;
    }

    return result;
}

static esp_err_t web_uds_request(
    const cJSON *root
)
{
    const cJSON *kind =
        cJSON_GetObjectItemCaseSensitive(root, "kind");

    if (!cJSON_IsString(kind) ||
        (s_client.state == UDS_CLIENT_CLOSED)) {

        return ESP_ERR_INVALID_STATE;
    }

    const bool security_request =
        (strcmp(kind->valuestring, "security_seed") == 0) ||
        (strcmp(kind->valuestring, "security_key") == 0) ||
        (strcmp(kind->valuestring, "security_unlock") == 0);

    if (security_request && web_uds_security_busy()) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint64_t now_us = esp_timer_get_time();
    uint32_t value = 0U;
    bool suppress = false;

    if (strcmp(kind->valuestring, "read_did") == 0) {
        if (!web_uds_number(root, "did", UINT16_MAX, &value)) {
            return ESP_ERR_INVALID_ARG;
        }

        return uds_client_read_data_by_identifier(
            &s_client,
            (uint16_t)value,
            now_us
        );
    }

    if (strcmp(kind->valuestring, "read_scaling_did") == 0) {
        if (!web_uds_number(root, "did", UINT16_MAX, &value)) {
            return ESP_ERR_INVALID_ARG;
        }

        return uds_client_read_scaling_data_by_identifier(
            &s_client,
            (uint16_t)value,
            now_us
        );
    }

    if (strcmp(kind->valuestring, "write_did") == 0) {
        const cJSON *data =
            cJSON_GetObjectItemCaseSensitive(root, "data");

        if (!web_uds_number(root, "did", UINT16_MAX, &value) ||
            !cJSON_IsString(data)) {

            return ESP_ERR_INVALID_ARG;
        }

        uint8_t payload[UDS_CLIENT_WRITE_DATA_MAX_LENGTH];
        size_t payload_size = 0U;
        const esp_err_t parse_result =
            web_uds_parse_hex(
                data->valuestring,
                payload,
                sizeof(payload),
                &payload_size
            );

        if ((parse_result != ESP_OK) ||
            (payload_size == 0U)) {

            return (parse_result != ESP_OK)
                ? parse_result
                : ESP_ERR_INVALID_ARG;
        }

        return uds_client_write_data_by_identifier(
            &s_client,
            (uint16_t)value,
            payload,
            payload_size,
            now_us
        );
    }

    if (strcmp(kind->valuestring, "read_memory") == 0) {
        uint32_t address_length = 0U;
        uint32_t size_length = 0U;
        uint64_t memory_address = 0U;
        uint64_t memory_size = 0U;

        if (!web_uds_number(root, "address_length", 8U, &address_length) ||
            !web_uds_number(root, "size_length", 8U, &size_length) ||
            !web_uds_hex_uint64(root, "address", &memory_address) ||
            !web_uds_hex_uint64(root, "size", &memory_size)) {

            return ESP_ERR_INVALID_ARG;
        }

        return uds_client_read_memory_by_address(
            &s_client,
            memory_address,
            (uint8_t)address_length,
            memory_size,
            (uint8_t)size_length,
            now_us
        );
    }

    if (strcmp(kind->valuestring, "write_memory") == 0) {
        const cJSON *data =
            cJSON_GetObjectItemCaseSensitive(root, "data");
        uint32_t address_length = 0U;
        uint32_t size_length = 0U;
        uint64_t memory_address = 0U;
        uint64_t memory_size = 0U;

        if (!web_uds_number(root, "address_length", 8U, &address_length) ||
            !web_uds_number(root, "size_length", 8U, &size_length) ||
            !web_uds_hex_uint64(root, "address", &memory_address) ||
            !web_uds_hex_uint64(root, "size", &memory_size) ||
            !cJSON_IsString(data)) {

            return ESP_ERR_INVALID_ARG;
        }

        size_t payload_size = 0U;
        const esp_err_t parse_result =
            web_uds_parse_hex(
                data->valuestring,
                s_manual_request_buffer,
                UDS_CLIENT_MEMORY_DATA_MAX_LENGTH,
                &payload_size
            );

        if ((parse_result != ESP_OK) ||
            (payload_size == 0U) ||
            (memory_size != payload_size)) {

            return (parse_result != ESP_OK)
                ? parse_result
                : ESP_ERR_INVALID_ARG;
        }

        return uds_client_write_memory_by_address(
            &s_client,
            memory_address,
            (uint8_t)address_length,
            memory_size,
            (uint8_t)size_length,
            s_manual_request_buffer,
            payload_size,
            now_us
        );
    }

    if (strcmp(kind->valuestring, "communication_control") == 0) {
        uint32_t communication_type = 0U;

        if (!web_uds_number(root, "value", 0x03U, &value) ||
            !web_uds_number(
                root,
                "communication_type",
                UINT8_MAX,
                &communication_type
            ) ||
            !web_uds_boolean(root, "suppress", false, &suppress)) {

            return ESP_ERR_INVALID_ARG;
        }

        return uds_client_communication_control(
            &s_client,
            (uint8_t)value,
            (uint8_t)communication_type,
            suppress,
            now_us
        );
    }

    if (strcmp(kind->valuestring, "io_control") == 0) {
        const cJSON *data =
            cJSON_GetObjectItemCaseSensitive(root, "data");
        uint32_t control_parameter = 0U;

        if (!web_uds_number(root, "did", UINT16_MAX, &value) ||
            !web_uds_number(
                root,
                "control_parameter",
                UINT8_MAX,
                &control_parameter
            ) ||
            !cJSON_IsString(data)) {

            return ESP_ERR_INVALID_ARG;
        }

        uint8_t control_state[UDS_CLIENT_CONTROL_DATA_MAX_LENGTH];
        size_t control_state_length = 0U;
        const esp_err_t parse_result = web_uds_parse_hex(
            data->valuestring,
            control_state,
            sizeof(control_state),
            &control_state_length
        );

        if (parse_result != ESP_OK) {
            return parse_result;
        }

        return uds_client_input_output_control_by_identifier(
            &s_client,
            (uint16_t)value,
            (uint8_t)control_parameter,
            control_state,
            control_state_length,
            now_us
        );
    }

    if (strcmp(kind->valuestring, "control_dtc_setting") == 0) {
        const cJSON *data =
            cJSON_GetObjectItemCaseSensitive(root, "data");

        if (!web_uds_number(root, "value", 0x02U, &value) ||
            !web_uds_boolean(root, "suppress", false, &suppress) ||
            !cJSON_IsString(data)) {

            return ESP_ERR_INVALID_ARG;
        }

        uint8_t option_record[UDS_CLIENT_CONTROL_DATA_MAX_LENGTH];
        size_t option_record_length = 0U;
        const esp_err_t parse_result = web_uds_parse_hex(
            data->valuestring,
            option_record,
            sizeof(option_record),
            &option_record_length
        );

        if (parse_result != ESP_OK) {
            return parse_result;
        }

        return uds_client_control_dtc_setting(
            &s_client,
            (uint8_t)value,
            option_record,
            option_record_length,
            suppress,
            now_us
        );
    }

    if (strcmp(kind->valuestring, "read_dtc") == 0) {
        uint32_t status_mask = 0U;

        if (!web_uds_number(root, "value", 0xFFU, &value) ||
            !web_uds_number(
                root,
                "status_mask",
                0xFFU,
                &status_mask
            )) {

            return ESP_ERR_INVALID_ARG;
        }

        return uds_client_read_dtc_information(
            &s_client,
            (uint8_t)value,
            (uint8_t)status_mask,
            now_us
        );
    }

    if (strcmp(kind->valuestring, "clear_dtc") == 0) {
        if (!web_uds_number(
                root,
                "group",
                0xFFFFFFU,
                &value
            )) {

            return ESP_ERR_INVALID_ARG;
        }

        return uds_client_clear_diagnostic_information(
            &s_client,
            value,
            now_us
        );
    }

    if (strcmp(kind->valuestring, "routine") == 0) {
        const cJSON *data =
            cJSON_GetObjectItemCaseSensitive(root, "data");
        uint32_t routine_identifier = 0U;

        if (!web_uds_number(root, "value", 0x7FU, &value) ||
            !web_uds_number(
                root,
                "routine_id",
                UINT16_MAX,
                &routine_identifier
            ) ||
            !web_uds_boolean(root, "suppress", false, &suppress) ||
            !cJSON_IsString(data)) {

            return ESP_ERR_INVALID_ARG;
        }

        uint8_t option_record[
            UDS_CLIENT_ROUTINE_OPTION_MAX_LENGTH
        ];
        size_t option_record_length = 0U;
        const esp_err_t parse_result =
            web_uds_parse_hex(
                data->valuestring,
                option_record,
                sizeof(option_record),
                &option_record_length
            );

        if (parse_result != ESP_OK) {
            return parse_result;
        }

        return uds_client_routine_control(
            &s_client,
            (uint8_t)value,
            (uint16_t)routine_identifier,
            option_record,
            option_record_length,
            suppress,
            now_us
        );
    }

    if ((strcmp(kind->valuestring, "security_seed") == 0) ||
        (strcmp(kind->valuestring, "security_key") == 0)) {

        const bool send_key =
            strcmp(kind->valuestring, "security_key") == 0;
        const cJSON *data =
            cJSON_GetObjectItemCaseSensitive(root, "data");

        if (!web_uds_number(
                root,
                "value",
                UDS_SECURITY_ACCESS_LEVEL_MAX,
                &value
            ) ||
            !cJSON_IsString(data)) {

            return ESP_ERR_INVALID_ARG;
        }

        uint8_t security_data[
            UDS_CLIENT_SECURITY_DATA_MAX_LENGTH
        ];
        size_t security_data_length = 0U;
        const esp_err_t parse_result =
            web_uds_parse_hex(
                data->valuestring,
                security_data,
                sizeof(security_data),
                &security_data_length
            );

        if (parse_result != ESP_OK) {
            return parse_result;
        }

        if (send_key) {
            return uds_client_security_access_send_key(
                &s_client,
                (uint8_t)value,
                security_data,
                security_data_length,
                now_us
            );
        }

        return uds_client_security_access_request_seed(
            &s_client,
            (uint8_t)value,
            security_data,
            security_data_length,
            now_us
        );
    }

    if (strcmp(kind->valuestring, "security_unlock") == 0) {
        return web_uds_security_unlock(root);
    }

    if ((strcmp(kind->valuestring, "request_download") == 0) ||
        (strcmp(kind->valuestring, "request_upload") == 0)) {

        const bool upload =
            strcmp(kind->valuestring, "request_upload") == 0;
        uint32_t data_format_identifier = 0U;
        uint32_t address_length = 0U;
        uint32_t size_length = 0U;
        uint64_t memory_address = 0U;
        uint64_t memory_size = 0U;

        if (!web_uds_number(
                root,
                "data_format",
                UINT8_MAX,
                &data_format_identifier
            ) ||
            !web_uds_number(root, "address_length", 8U, &address_length) ||
            !web_uds_number(root, "size_length", 8U, &size_length) ||
            !web_uds_hex_uint64(root, "address", &memory_address) ||
            !web_uds_hex_uint64(root, "size", &memory_size)) {

            return ESP_ERR_INVALID_ARG;
        }

        return upload
            ? uds_client_request_upload(
                &s_client,
                (uint8_t)data_format_identifier,
                memory_address,
                (uint8_t)address_length,
                memory_size,
                (uint8_t)size_length,
                now_us
            )
            : uds_client_request_download(
                &s_client,
                (uint8_t)data_format_identifier,
                memory_address,
                (uint8_t)address_length,
                memory_size,
                (uint8_t)size_length,
                now_us
            );
    }

    if (strcmp(kind->valuestring, "transfer_data") == 0) {
        const cJSON *data =
            cJSON_GetObjectItemCaseSensitive(root, "data");

        if (!web_uds_number(root, "counter", UINT8_MAX, &value) ||
            !cJSON_IsString(data)) {

            return ESP_ERR_INVALID_ARG;
        }

        uint8_t transfer_data[
            UDS_CLIENT_TRANSFER_DATA_MAX_LENGTH
        ];
        size_t transfer_data_length = 0U;
        const esp_err_t parse_result =
            web_uds_parse_hex(
                data->valuestring,
                transfer_data,
                sizeof(transfer_data),
                &transfer_data_length
            );

        if ((parse_result != ESP_OK) ||
            (transfer_data_length == 0U)) {

            return (parse_result != ESP_OK)
                ? parse_result
                : ESP_ERR_INVALID_ARG;
        }

        return uds_client_transfer_data(
            &s_client,
            (uint8_t)value,
            transfer_data,
            transfer_data_length,
            now_us
        );
    }

    if (strcmp(kind->valuestring, "transfer_exit") == 0) {
        const cJSON *data =
            cJSON_GetObjectItemCaseSensitive(root, "data");

        if (!cJSON_IsString(data)) {
            return ESP_ERR_INVALID_ARG;
        }

        uint8_t parameter_record[
            UDS_CLIENT_TRANSFER_EXIT_MAX_LENGTH
        ];
        size_t parameter_record_length = 0U;
        const esp_err_t parse_result =
            web_uds_parse_hex(
                data->valuestring,
                parameter_record,
                sizeof(parameter_record),
                &parameter_record_length
            );

        if (parse_result != ESP_OK) {
            return parse_result;
        }

        return uds_client_request_transfer_exit(
            &s_client,
            parameter_record,
            parameter_record_length,
            now_us
        );
    }

    if (strcmp(kind->valuestring, "session") == 0) {
        if (!web_uds_number(root, "value", 0x7FU, &value) ||
            !web_uds_boolean(root, "suppress", false, &suppress)) {

            return ESP_ERR_INVALID_ARG;
        }

        s_pending_diagnostic_session = (uint8_t)value;
        const esp_err_t result =
            uds_client_diagnostic_session_control(
                &s_client,
                (uint8_t)value,
                suppress,
                now_us
            );

        if (result != ESP_OK) {
            s_pending_diagnostic_session = 0U;
        }

        return result;
    }

    if (strcmp(kind->valuestring, "reset") == 0) {
        if (!web_uds_number(root, "value", 0x7FU, &value) ||
            !web_uds_boolean(root, "suppress", false, &suppress)) {

            return ESP_ERR_INVALID_ARG;
        }

        return uds_client_ecu_reset(
            &s_client,
            (uint8_t)value,
            suppress,
            now_us
        );
    }

    if (strcmp(kind->valuestring, "tester_present") == 0) {
        if (!web_uds_boolean(root, "suppress", true, &suppress)) {
            return ESP_ERR_INVALID_ARG;
        }

        return uds_client_tester_present(
            &s_client,
            suppress,
            now_us
        );
    }

    if (strcmp(kind->valuestring, "raw") == 0) {
        const cJSON *data =
            cJSON_GetObjectItemCaseSensitive(root, "data");

        if (!web_uds_number(root, "sid", 0xBFU, &value) ||
            !cJSON_IsString(data)) {

            return ESP_ERR_INVALID_ARG;
        }

        size_t parameter_size = 0U;
        const esp_err_t parse_result =
            web_uds_parse_hex(
                data->valuestring,
                s_response_buffer,
                WEB_UDS_BUFFER_SIZE - 1U,
                &parameter_size
            );

        if (parse_result != ESP_OK) {
            return parse_result;
        }

        return uds_client_request(
            &s_client,
            (uint8_t)value,
            s_response_buffer,
            parameter_size,
            now_us
        );
    }

    return ESP_ERR_INVALID_ARG;
}

static esp_err_t web_uds_download_read(
    uint64_t offset,
    uint8_t *buffer,
    size_t capacity,
    size_t *read_size,
    void *context
)
{
    (void)context;

    if ((s_download_image_reader == NULL) ||
        (offset != s_download_image_offset)) {

        return ESP_ERR_INVALID_STATE;
    }

    *read_size = 0U;

    while (*read_size < capacity) {
        if (s_download_image_block_offset >=
            s_download_image_block.size) {

            const esp_err_t result = firmware_image_next(
                s_download_image_reader,
                &s_download_image_block
            );

            if (result == ESP_ERR_NOT_FOUND) {
                break;
            }

            if (result != ESP_OK) {
                return result;
            }

            s_download_image_block_offset = 0U;

            if ((s_download_stream_segment >=
                 s_download_segment_count) ||
                (s_download_image_block.address !=
                 (s_download_segments[
                    s_download_stream_segment
                 ].address +
                  s_download_stream_segment_offset))) {

                return ESP_ERR_INVALID_RESPONSE;
            }
        }

        size_t available =
            s_download_image_block.size -
            s_download_image_block_offset;
        const size_t remaining = capacity - *read_size;

        if (available > remaining) {
            available = remaining;
        }

        memcpy(
            buffer + *read_size,
            s_download_image_block.data +
                s_download_image_block_offset,
            available
        );
        s_download_image_block_offset += available;
        *read_size += available;
        s_download_stream_segment_offset += available;

        if (s_download_stream_segment_offset ==
            s_download_segments[
                s_download_stream_segment
            ].size) {

            s_download_stream_segment++;
            s_download_stream_segment_offset = 0U;
        }
    }

    if (*read_size == 0U) {
        return ESP_ERR_INVALID_SIZE;
    }

    s_download_image_offset += *read_size;
    return ESP_OK;
}

static esp_err_t web_uds_firmware_source_read(
    uint64_t offset,
    uint8_t *buffer,
    size_t capacity,
    size_t *read_size,
    void *context
)
{
    (void)context;

    if ((s_download_file == NULL) ||
        (offset > LONG_MAX)) {

        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ESP_OK;

    if (offset != s_download_source_file_offset) {
        result = storage_sd_service_seek(
            s_download_file,
            (long)offset,
            SEEK_SET
        );
    }

    if (result == ESP_OK) {
        result = storage_sd_service_read(
            s_download_file,
            buffer,
            capacity,
            read_size
        );
    }

    if (result == ESP_OK) {
        s_download_source_file_offset = offset + *read_size;
    }

    return result;
}

static esp_err_t web_uds_download_segment(
    uint32_t index,
    uint64_t *address,
    uint64_t *size,
    void *context
)
{
    (void)context;

    if ((address == NULL) ||
        (size == NULL) ||
        (s_download_segments == NULL) ||
        (index >= s_download_segment_count)) {

        return ESP_ERR_INVALID_ARG;
    }

    *address = s_download_segments[index].address;
    *size = s_download_segments[index].size;
    return ESP_OK;
}

static esp_err_t web_uds_prepare_firmware_image(
    const char *path,
    uint64_t file_size,
    uint64_t binary_address,
    firmware_image_info_t *info
)
{
    firmware_image_format_t format;
    esp_err_t result = firmware_image_format_from_name(
        path,
        &format
    );

    if (result != ESP_OK) {
        return result;
    }

    s_download_image_reader = heap_caps_calloc(
        1U,
        sizeof(*s_download_image_reader),
        MALLOC_CAP_SPIRAM |
        MALLOC_CAP_8BIT
    );

    if (s_download_image_reader == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const firmware_image_config_t config = {
        .format = format,
        .read = web_uds_firmware_source_read,
        .file_size = file_size,
        .binary_address = binary_address,
    };

    result = firmware_image_open(
        s_download_image_reader,
        &config
    );

    size_t segment_capacity =
        WEB_UDS_FIRMWARE_INITIAL_SEGMENTS;
    s_download_segments = heap_caps_calloc(
        segment_capacity,
        sizeof(*s_download_segments),
        MALLOC_CAP_SPIRAM |
        MALLOC_CAP_8BIT
    );

    if (s_download_segments == NULL) {
        return ESP_ERR_NO_MEM;
    }

    memset(info, 0, sizeof(*info));
    firmware_image_block_t block;

    while ((result == ESP_OK) &&
           ((result = firmware_image_next(
                s_download_image_reader,
                &block
            )) == ESP_OK)) {

        if (info->block_count == 0U) {
            info->lowest_address = block.address;
        }

        if ((info->block_count == 0U) ||
            ((s_download_segments[
                info->segment_count - 1U
            ].address +
              s_download_segments[
                info->segment_count - 1U
            ].size) != block.address)) {

            if (info->segment_count == segment_capacity) {
                if (segment_capacity >=
                    WEB_UDS_FIRMWARE_MAX_SEGMENTS) {

                    return ESP_ERR_INVALID_SIZE;
                }

                size_t new_capacity = segment_capacity * 2U;

                if (new_capacity > WEB_UDS_FIRMWARE_MAX_SEGMENTS) {
                    new_capacity = WEB_UDS_FIRMWARE_MAX_SEGMENTS;
                }

                web_uds_firmware_segment_t *segments =
                    heap_caps_realloc(
                        s_download_segments,
                        new_capacity *
                            sizeof(*s_download_segments),
                        MALLOC_CAP_SPIRAM |
                        MALLOC_CAP_8BIT
                    );

                if (segments == NULL) {
                    return ESP_ERR_NO_MEM;
                }

                s_download_segments = segments;
                segment_capacity = new_capacity;
            }

            s_download_segments[info->segment_count].address =
                block.address;
            s_download_segments[info->segment_count].size =
                block.size;
            info->segment_count++;
        } else {
            web_uds_firmware_segment_t *segment =
                &s_download_segments[info->segment_count - 1U];

            segment->size += block.size;
        }

        if (info->data_size > (UINT64_MAX - block.size)) {
            return ESP_ERR_INVALID_SIZE;
        }

        info->data_size += block.size;
        info->highest_address =
            block.address + block.size - 1U;
        info->block_count++;
    }

    if ((result != ESP_ERR_NOT_FOUND) ||
        !s_download_image_reader->terminated ||
        (info->block_count == 0U) ||
        (info->segment_count == 0U)) {

        return (result != ESP_ERR_NOT_FOUND)
            ? result
            : ESP_ERR_INVALID_RESPONSE;
    }

    info->entry_address =
        s_download_image_reader->entry_address;
    info->entry_address_valid =
        s_download_image_reader->entry_address_valid;
    s_download_segment_count = info->segment_count;
    result = firmware_image_open(
        s_download_image_reader,
        &config
    );
    s_download_image_offset = 0U;
    memset(
        &s_download_image_block,
        0,
        sizeof(s_download_image_block)
    );
    s_download_image_block_offset = 0U;
    s_download_stream_segment = 0U;
    s_download_stream_segment_offset = 0U;
    return result;
}

static uds_download_routine_result_t
web_uds_download_evaluate_routine_result(
    uint16_t routine_identifier,
    const uint8_t *status_record,
    size_t status_record_length,
    void *context
)
{
    (void)routine_identifier;

    const web_uds_routine_status_policy_t *policy = context;

    if ((policy == NULL) || !policy->enabled) {
        return UDS_DOWNLOAD_ROUTINE_RESULT_COMPLETE;
    }

    if ((status_record == NULL) ||
        (policy->offset >= status_record_length)) {

        return UDS_DOWNLOAD_ROUTINE_RESULT_ERROR;
    }

    const uint8_t status = status_record[policy->offset];

    if (status == policy->success_value) {
        return UDS_DOWNLOAD_ROUTINE_RESULT_COMPLETE;
    }

    return (status == policy->pending_value)
        ? UDS_DOWNLOAD_ROUTINE_RESULT_PENDING
        : UDS_DOWNLOAD_ROUTINE_RESULT_ERROR;
}

static void web_uds_download_close_file(void)
{
    if (s_download_file != NULL) {
        (void)storage_sd_service_close(&s_download_file);
    }

    heap_caps_free(s_download_image_reader);
    heap_caps_free(s_download_segments);
    s_download_image_reader = NULL;
    s_download_segments = NULL;
    s_download_segment_count = 0U;
    s_download_source_file_offset = 0U;
    s_download_image_offset = 0U;
    s_download_image_block_offset = 0U;
    s_download_stream_segment = 0U;
    s_download_stream_segment_offset = 0U;
    memset(
        &s_download_image_block,
        0,
        sizeof(s_download_image_block)
    );
}

static esp_err_t web_uds_download_write_journal(
    const char *format,
    ...
)
{
    if ((s_download_journal == NULL) || (format == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    char line[WEB_UDS_JOURNAL_LINE_MAX_SIZE];
    va_list arguments;
    va_start(arguments, format);
    const int length =
        vsnprintf(
            line,
            sizeof(line),
            format,
            arguments
        );
    va_end(arguments);

    if ((length < 0) || ((size_t)length >= sizeof(line))) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t written = 0U;
    const esp_err_t result =
        storage_sd_service_write(
            s_download_journal,
            line,
            (size_t)length,
            &written
        );

    if ((result == ESP_OK) && (written != (size_t)length)) {
        return ESP_ERR_INVALID_SIZE;
    }

    return result;
}

static esp_err_t web_uds_download_open_journal(
    const char *firmware_path,
    uint64_t firmware_size,
    uint64_t memory_address,
    uint8_t data_format_identifier
)
{
    esp_err_t result =
        storage_sd_service_ensure_directory(
            WEB_UDS_JOURNAL_DIRECTORY
        );

    if (result != ESP_OK) {
        return result;
    }

    char filename[64];
    result =
        time_service_format_filename(
            "uds",
            "log",
            filename,
            sizeof(filename)
        );

    if (result != ESP_OK) {
        const int length =
            snprintf(
                filename,
                sizeof(filename),
                "uds-boot-%" PRIu64 ".log",
                (uint64_t)esp_timer_get_time()
            );

        if ((length < 0) ||
            ((size_t)length >= sizeof(filename))) {

            return ESP_ERR_INVALID_SIZE;
        }
    } else {
        char *extension = strrchr(filename, '.');

        if (extension == NULL) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        const size_t prefix_length =
            (size_t)(extension - filename);
        const int length =
            snprintf(
                &filename[prefix_length],
                sizeof(filename) - prefix_length,
                "-%" PRIu64 ".log",
                (uint64_t)esp_timer_get_time()
            );

        if ((length < 0) ||
            ((size_t)length >=
             (sizeof(filename) - prefix_length))) {

            return ESP_ERR_INVALID_SIZE;
        }
    }

    const int path_length =
        snprintf(
            s_download_journal_path,
            sizeof(s_download_journal_path),
            "%s/%s",
            WEB_UDS_JOURNAL_DIRECTORY,
            filename
        );

    if ((path_length < 0) ||
        ((size_t)path_length >=
         sizeof(s_download_journal_path))) {

        return ESP_ERR_INVALID_SIZE;
    }

    result =
        storage_sd_service_open(
            s_download_journal_path,
            "w",
            &s_download_journal
        );

    if (result != ESP_OK) {
        return result;
    }

    s_download_started_at_us =
        (uint64_t)esp_timer_get_time();
    s_download_finished_at_us = 0U;
    s_download_logged_retries = 0U;
    s_download_journal_finalized = false;

    result =
        web_uds_download_write_journal(
            "event=start time_us=%" PRIu64
            " firmware=\"%s\" size=%" PRIu64
            " address=0x%" PRIX64 " data_format=0x%02X\n",
            s_download_started_at_us,
            firmware_path,
            firmware_size,
            memory_address,
            (unsigned int)data_format_identifier
        );

    if (result == ESP_OK) {
        result = storage_sd_service_flush(s_download_journal);
    }

    if (result != ESP_OK) {
        (void)storage_sd_service_close(&s_download_journal);
        s_download_journal_path[0] = '\0';
    }

    return result;
}

static void web_uds_download_log_progress(
    const uds_download_progress_t *progress
)
{
    if ((progress == NULL) ||
        (progress->retry_count == s_download_logged_retries)) {

        return;
    }

    if (web_uds_download_write_journal(
            "event=retry time_us=%" PRIu64
            " block=%" PRIu32 " sequence=%u"
            " retry=%u total_retries=%" PRIu32
            " nrc=0x%02X\n",
            (uint64_t)esp_timer_get_time(),
            progress->acknowledged_blocks + 1U,
            (unsigned int)progress->block_sequence_counter,
            (unsigned int)progress->current_block_retry,
            progress->retry_count,
            (unsigned int)progress->last_negative_response_code
        ) == ESP_OK) {

        s_download_logged_retries = progress->retry_count;
    }
}

static void web_uds_download_finalize_journal(
    const uds_download_progress_t *progress,
    const char *outcome
)
{
    if ((s_download_journal == NULL) ||
        s_download_journal_finalized ||
        (progress == NULL) ||
        (outcome == NULL)) {

        return;
    }

    s_download_journal_finalized = true;
    const uint64_t now_us =
        (uint64_t)esp_timer_get_time();
    s_download_finished_at_us = now_us;

    (void)web_uds_download_write_journal(
        "event=finish time_us=%" PRIu64
        " outcome=%s duration_us=%" PRIu64
        " transferred=%" PRIu64 " total=%" PRIu64
        " blocks=%" PRIu32 " retries=%" PRIu32
        " routine_polls=%" PRIu32
        " nrc=0x%02X result=%d\n",
        now_us,
        outcome,
        now_us - s_download_started_at_us,
        progress->transferred_size,
        progress->total_size,
        progress->acknowledged_blocks,
        progress->retry_count,
        progress->routine_poll_count,
        (unsigned int)progress->last_negative_response_code,
        (int)progress->last_result
    );

    (void)storage_sd_service_flush(s_download_journal);
    (void)storage_sd_service_sync(s_download_journal);
    (void)storage_sd_service_close(&s_download_journal);
}

static void web_uds_worker_task(
    void *context
)
{
    (void)context;

    while (true) {
        if (xSemaphoreTakeRecursive(
                s_lock,
                pdMS_TO_TICKS(WEB_UDS_LOCK_TIMEOUT_MS)
            ) != pdTRUE) {

            vTaskDelay(1U);
            continue;
        }

        const bool running = s_download_active;

        if (running) {
            (void)uds_download_poll(
                &s_download,
                esp_timer_get_time()
            );

            uds_download_progress_t progress = {0};
            (void)uds_download_get_progress(
                &s_download,
                &progress
            );
            web_uds_download_log_progress(&progress);

            if ((progress.state == UDS_DOWNLOAD_COMPLETE) ||
                (progress.state == UDS_DOWNLOAD_CANCELLED) ||
                (progress.state == UDS_DOWNLOAD_ERROR)) {

                const char *outcome =
                    (progress.state == UDS_DOWNLOAD_COMPLETE)
                        ? "completed"
                        : (progress.state == UDS_DOWNLOAD_CANCELLED)
                            ? "cancelled"
                            : "failed";

                web_uds_download_finalize_journal(
                    &progress,
                    outcome
                );
                web_uds_download_close_file();
                uds_security_provider_clear_manual_key(
                    &s_security_provider
                );
                web_uds_erase(
                    s_download_security_key,
                    sizeof(s_download_security_key)
                );
                web_uds_erase(
                    s_download_security_seed,
                    sizeof(s_download_security_seed)
                );
            }
        } else if (s_client.state != UDS_CLIENT_CLOSED) {
            const uint64_t now_us = esp_timer_get_time();

            (void)uds_client_poll(
                &s_client,
                now_us
            );
            web_uds_security_submit_key();
            web_uds_tester_present_poll(now_us);
        }

        xSemaphoreGiveRecursive(s_lock);

        vTaskDelay(
            pdMS_TO_TICKS(WEB_UDS_WORKER_PERIOD_MS)
        );
    }
}

static esp_err_t web_uds_download_start(
    const cJSON *root
)
{
    const cJSON *path =
        cJSON_GetObjectItemCaseSensitive(root, "path");
    uint32_t data_format = 0U;
    uint32_t address_length = 0U;
    uint32_t size_length = 0U;
    uint32_t programming_session =
        UDS_DIAGNOSTIC_SESSION_PROGRAMMING;
    uint32_t security_level = 0U;
    uint32_t erase_routine_identifier = 0U;
    uint32_t verify_routine_identifier = 0U;
    uint32_t reset_type = 0U;
    uint32_t routine_poll_interval_ms = 250U;
    uint32_t routine_poll_maximum = 240U;
    uint32_t erase_status_offset = 0U;
    uint32_t erase_pending_value = 0U;
    uint32_t erase_success_value = 1U;
    uint32_t verify_status_offset = 0U;
    uint32_t verify_pending_value = 0U;
    uint32_t verify_success_value = 1U;
    uint64_t address = 0U;
    bool security_enabled = false;
    bool erase_enabled = false;
    bool verify_enabled = false;
    bool reset_enabled = false;
    bool restore_default_session = true;
    bool erase_status_enabled = false;
    bool verify_status_enabled = false;
    const cJSON *security_key =
        cJSON_GetObjectItemCaseSensitive(root, "security_key");
    const cJSON *erase_record =
        cJSON_GetObjectItemCaseSensitive(root, "erase_record");
    const cJSON *verify_record =
        cJSON_GetObjectItemCaseSensitive(root, "verify_record");

    if (!s_client_config_valid ||
        s_download_active ||
        !cJSON_IsString(path) ||
        !web_uds_firmware_path_valid(path->valuestring) ||
        !web_uds_number(root, "data_format", UINT8_MAX, &data_format) ||
        !web_uds_number(root, "address_length", 8U, &address_length) ||
        (address_length == 0U) ||
        !web_uds_number(root, "size_length", 8U, &size_length) ||
        (size_length == 0U) ||
        !web_uds_number(
            root,
            "programming_session",
            0x7FU,
            &programming_session
        ) ||
        !web_uds_boolean(
            root,
            "security_enabled",
            false,
            &security_enabled
        ) ||
        !web_uds_boolean(
            root,
            "erase_enabled",
            false,
            &erase_enabled
        ) ||
        !web_uds_boolean(
            root,
            "verify_enabled",
            false,
            &verify_enabled
        ) ||
        !web_uds_boolean(
            root,
            "reset_enabled",
            false,
            &reset_enabled
        ) ||
        !web_uds_boolean(
            root,
            "restore_default_session",
            true,
            &restore_default_session
        ) ||
        !web_uds_number(
            root,
            "routine_poll_interval_ms",
            60000U,
            &routine_poll_interval_ms
        ) ||
        (routine_poll_interval_ms == 0U) ||
        !web_uds_number(
            root,
            "routine_poll_maximum",
            100000U,
            &routine_poll_maximum
        ) ||
        (routine_poll_maximum == 0U) ||
        !web_uds_hex_uint64(root, "address", &address)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (security_enabled &&
        (!web_uds_number(
            root,
            "security_level",
            UDS_SECURITY_ACCESS_LEVEL_MAX,
            &security_level
        ) ||
         ((security_level & 1U) == 0U) ||
         !cJSON_IsString(security_key))) {

        return ESP_ERR_INVALID_ARG;
    }

    if (erase_enabled &&
        (!web_uds_number(
            root,
            "erase_routine_id",
            UINT16_MAX,
            &erase_routine_identifier
        ) ||
         (erase_routine_identifier == 0U) ||
         !cJSON_IsString(erase_record))) {

        return ESP_ERR_INVALID_ARG;
    }

    if (erase_enabled &&
        (!web_uds_boolean(
            root,
            "erase_status_enabled",
            false,
            &erase_status_enabled
        ) ||
         !web_uds_number(
            root,
            "erase_status_offset",
            UINT8_MAX,
            &erase_status_offset
        ) ||
         !web_uds_number(
            root,
            "erase_status_pending",
            UINT8_MAX,
            &erase_pending_value
        ) ||
         !web_uds_number(
            root,
            "erase_status_success",
            UINT8_MAX,
            &erase_success_value
        ))) {

        return ESP_ERR_INVALID_ARG;
    }

    if (verify_enabled &&
        (!web_uds_number(
            root,
            "verify_routine_id",
            UINT16_MAX,
            &verify_routine_identifier
        ) ||
         (verify_routine_identifier == 0U) ||
         !cJSON_IsString(verify_record))) {

        return ESP_ERR_INVALID_ARG;
    }

    if (verify_enabled &&
        (!web_uds_boolean(
            root,
            "verify_status_enabled",
            false,
            &verify_status_enabled
        ) ||
         !web_uds_number(
            root,
            "verify_status_offset",
            UINT8_MAX,
            &verify_status_offset
        ) ||
         !web_uds_number(
            root,
            "verify_status_pending",
            UINT8_MAX,
            &verify_pending_value
        ) ||
         !web_uds_number(
            root,
            "verify_status_success",
            UINT8_MAX,
            &verify_success_value
        ))) {

        return ESP_ERR_INVALID_ARG;
    }

    if (reset_enabled &&
        (!web_uds_number(
            root,
            "reset_type",
            0x7FU,
            &reset_type
        ) ||
         (reset_type == 0U))) {

        return ESP_ERR_INVALID_ARG;
    }

    if ((erase_status_enabled &&
         (erase_pending_value == erase_success_value)) ||
        (verify_status_enabled &&
         (verify_pending_value == verify_success_value))) {

        return ESP_ERR_INVALID_ARG;
    }

    s_download_erase_record_length = 0U;
    s_download_verify_record_length = 0U;

    esp_err_t parse_result = ESP_OK;

    if (erase_enabled) {
        parse_result =
            web_uds_parse_hex(
                erase_record->valuestring,
                s_download_erase_record,
                sizeof(s_download_erase_record),
                &s_download_erase_record_length
            );
    }

    if ((parse_result == ESP_OK) && verify_enabled) {
        parse_result =
            web_uds_parse_hex(
                verify_record->valuestring,
                s_download_verify_record,
                sizeof(s_download_verify_record),
                &s_download_verify_record_length
            );
    }

    if (parse_result != ESP_OK) {
        return parse_result;
    }

    s_download_erase_status_policy =
        (web_uds_routine_status_policy_t) {
            .enabled = erase_status_enabled,
            .offset = erase_status_offset,
            .pending_value = (uint8_t)erase_pending_value,
            .success_value = (uint8_t)erase_success_value,
        };
    s_download_verify_status_policy =
        (web_uds_routine_status_policy_t) {
            .enabled = verify_status_enabled,
            .offset = verify_status_offset,
            .pending_value = (uint8_t)verify_pending_value,
            .success_value = (uint8_t)verify_success_value,
        };

    if (security_enabled) {
        uint8_t manual_key[
            UDS_SECURITY_PROVIDER_MANUAL_KEY_SIZE
        ];
        size_t manual_key_length = 0U;

        parse_result =
            web_uds_parse_hex(
                security_key->valuestring,
                manual_key,
                sizeof(manual_key),
                &manual_key_length
            );

        if ((parse_result == ESP_OK) &&
            (manual_key_length == 0U)) {

            parse_result = ESP_ERR_INVALID_ARG;
        }

        if (parse_result == ESP_OK) {
            parse_result =
                uds_security_provider_set_manual_key(
                    &s_security_provider,
                    manual_key,
                    manual_key_length
                );
        }

        web_uds_erase(manual_key, sizeof(manual_key));

        if (parse_result != ESP_OK) {
            return parse_result;
        }
    }

    struct stat information = {0};
    esp_err_t result =
        storage_sd_service_stat(
            path->valuestring,
            &information
        );

    if ((result != ESP_OK) ||
        !S_ISREG(information.st_mode) ||
        (information.st_size <= 0) ||
        ((uint64_t)information.st_size >
         WEB_UDS_FIRMWARE_MAX_SIZE)) {

        uds_security_provider_clear_manual_key(
            &s_security_provider
        );

        return (result != ESP_OK)
            ? result
            : ESP_ERR_INVALID_ARG;
    }

    result =
        storage_sd_service_open(
            path->valuestring,
            "rb",
            &s_download_file
        );

    if (result != ESP_OK) {
        uds_security_provider_clear_manual_key(
            &s_security_provider
        );
        return result;
    }

    firmware_image_info_t image_info = {0};
    result = web_uds_prepare_firmware_image(
        path->valuestring,
        (uint64_t)information.st_size,
        address,
        &image_info
    );

    const uint64_t maximum_address =
        (address_length == 8U)
            ? UINT64_MAX
            : (1ULL << (address_length * 8U)) - 1ULL;
    const uint64_t maximum_size =
        (size_length == 8U)
            ? UINT64_MAX
            : (1ULL << (size_length * 8U)) - 1ULL;

    for (uint32_t index = 0U;
         (result == ESP_OK) &&
         (index < s_download_segment_count);
         ++index) {

        const web_uds_firmware_segment_t *segment =
            &s_download_segments[index];

        if ((segment->address > maximum_address) ||
            (segment->size > maximum_size)) {

            result = ESP_ERR_INVALID_SIZE;
        }
    }

    if (result == ESP_OK) {
        result = web_uds_download_open_journal(
            path->valuestring,
            image_info.data_size,
            image_info.lowest_address,
            (uint8_t)data_format
        );
    }

    if (result != ESP_OK) {
        web_uds_download_close_file();
        uds_security_provider_clear_manual_key(
            &s_security_provider
        );
        return result;
    }

    if (s_client.state != UDS_CLIENT_CLOSED) {
        result = uds_client_close(&s_client);
    }

    if (result == ESP_OK) {
        const uds_download_config_t config = {
            .client = s_client_config,
            .read = web_uds_download_read,
            .transfer_buffer = s_download_transfer_buffer,
            .transfer_capacity = UDS_CLIENT_TRANSFER_DATA_MAX_LENGTH,
            .data_format_identifier = (uint8_t)data_format,
            .memory_address = address,
            .memory_address_length = (uint8_t)address_length,
            .memory_size = image_info.data_size,
            .memory_size_length = (uint8_t)size_length,
            .segment_count = s_download_segment_count,
            .segment = web_uds_download_segment,
            .maximum_block_retries = 3U,
            .operation_timeout_us = 600000000ULL,
            .programming_session_type =
                (uint8_t)programming_session,
            .security_level =
                security_enabled
                    ? (uint8_t)security_level
                    : 0U,
            .security_algorithm =
                security_enabled
                    ? uds_security_provider_calculate
                    : NULL,
            .security_algorithm_context =
                security_enabled
                    ? &s_security_provider
                    : NULL,
            .security_seed_buffer = s_download_security_seed,
            .security_seed_capacity =
                sizeof(s_download_security_seed),
            .security_key_buffer = s_download_security_key,
            .security_key_capacity =
                sizeof(s_download_security_key),
            .erase_routine_identifier =
                erase_enabled
                    ? (uint16_t)erase_routine_identifier
                    : 0U,
            .erase_option_record = s_download_erase_record,
            .erase_option_record_length =
                s_download_erase_record_length,
            .erase_result =
                web_uds_download_evaluate_routine_result,
            .erase_result_context =
                &s_download_erase_status_policy,
            .verify_routine_identifier =
                verify_enabled
                    ? (uint16_t)verify_routine_identifier
                    : 0U,
            .verify_option_record = s_download_verify_record,
            .verify_option_record_length =
                s_download_verify_record_length,
            .verify_result =
                web_uds_download_evaluate_routine_result,
            .verify_result_context =
                &s_download_verify_status_policy,
            .routine_poll_interval_us =
                (uint64_t)routine_poll_interval_ms * 1000ULL,
            .maximum_routine_polls = routine_poll_maximum,
            .reset_type =
                reset_enabled
                    ? (uint8_t)reset_type
                    : 0U,
            .restore_default_session =
                restore_default_session,
        };

        result = uds_download_open(&s_download, &config);
    }

    if (result == ESP_OK) {
        result =
            uds_download_start(
                &s_download,
                esp_timer_get_time()
            );
    }

    if (result == ESP_OK) {
        s_download_active = true;
    } else {
        uds_download_progress_t progress = {0};
        (void)uds_download_get_progress(
            &s_download,
            &progress
        );
        progress.last_result = result;
        web_uds_download_finalize_journal(
            &progress,
            "failed"
        );
        web_uds_download_close_file();
        uds_security_provider_clear_manual_key(
            &s_security_provider
        );
        web_uds_erase(
            s_download_security_key,
            sizeof(s_download_security_key)
        );
        web_uds_erase(
            s_download_security_seed,
            sizeof(s_download_security_seed)
        );
        (void)uds_client_open(&s_client, &s_client_config);
    }

    return result;
}

static bool web_uds_firmware_path_valid(
    const char *path
)
{
    if ((path == NULL) ||
        (strncmp(
            path,
            WEB_UDS_FIRMWARE_DIRECTORY,
            sizeof(WEB_UDS_FIRMWARE_DIRECTORY) - 1U
        ) != 0) ||
        (strstr(path, "..") != NULL) ||
        (strchr(
            &path[sizeof(WEB_UDS_FIRMWARE_DIRECTORY) - 1U],
            '/'
        ) != NULL)) {

        return false;
    }

    firmware_image_format_t format;
    return firmware_image_format_from_name(
        path,
        &format
    ) == ESP_OK;
}

static esp_err_t web_uds_profile_send_list(
    httpd_req_t *request,
    size_t offset
)
{
    uds_profile_summary_t *profiles = heap_caps_calloc(
        WEB_UDS_PROFILE_LIST_CAPACITY,
        sizeof(*profiles),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (profiles == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t count = 0U;
    bool has_more = false;
    esp_err_t result = uds_profile_service_list(
        offset,
        profiles,
        WEB_UDS_PROFILE_LIST_CAPACITY,
        &count,
        &has_more
    );
    cJSON *response = NULL;
    cJSON *items = NULL;

    if (result == ESP_OK) {
        response = cJSON_CreateObject();
        items = cJSON_CreateArray();

        if ((response == NULL) ||
            (items == NULL) ||
            !cJSON_AddItemToObject(
                response,
                "profiles",
                items
            )) {

            cJSON_Delete(response);
            cJSON_Delete(items);
            response = NULL;
            items = NULL;
            result = ESP_ERR_NO_MEM;
        }
    }

    for (size_t index = 0U;
         (result == ESP_OK) && (index < count);
         ++index) {

        cJSON *item = cJSON_CreateObject();

        if ((item == NULL) ||
            (cJSON_AddStringToObject(
                item,
                "file_name",
                profiles[index].file_name
            ) == NULL) ||
            (cJSON_AddStringToObject(
                item,
                "name",
                profiles[index].name
            ) == NULL) ||
            (cJSON_AddStringToObject(
                item,
                "description",
                profiles[index].description
            ) == NULL) ||
            !cJSON_AddItemToArray(items, item)) {

            cJSON_Delete(item);
            result = ESP_ERR_NO_MEM;
        }
    }

    if ((result == ESP_OK) &&
        ((cJSON_AddBoolToObject(
            response,
            "success",
            true
        ) == NULL) ||
         (cJSON_AddNumberToObject(
            response,
            "offset",
            offset
        ) == NULL) ||
         (cJSON_AddBoolToObject(
            response,
            "has_more",
            has_more
        ) == NULL))) {

        result = ESP_ERR_NO_MEM;
    }

    heap_caps_free(profiles);

    if (result != ESP_OK) {
        cJSON_Delete(response);
        return web_api_send_message(
            request,
            (result == ESP_ERR_INVALID_STATE)
                ? "409 Conflict"
                : "500 Internal Server Error",
            false,
            esp_err_to_name(result)
        );
    }

    result = web_api_send_json(request, response);
    cJSON_Delete(response);
    return result;
}

static esp_err_t web_uds_profile_send_one(
    httpd_req_t *request,
    const char *file_name
)
{
    char *json = NULL;
    esp_err_t result = uds_profile_service_load_json(
        file_name,
        &json
    );

    if (result != ESP_OK) {
        heap_caps_free(json);
        return web_api_send_message(
            request,
            (result == ESP_ERR_NOT_FOUND)
                ? "404 Not Found"
                : "400 Bad Request",
            false,
            esp_err_to_name(result)
        );
    }

    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    result = httpd_resp_send(
        request,
        json,
        HTTPD_RESP_USE_STRLEN
    );

    heap_caps_free(json);
    return result;
}

static esp_err_t web_uds_profile_get_handler(
    httpd_req_t *request
)
{
    const size_t query_length =
        httpd_req_get_url_query_len(request);

    if (query_length == 0U) {
        return ESP_ERR_NOT_FOUND;
    }

    if (query_length >= WEB_UDS_QUERY_MAX_SIZE) {
        return web_api_send_message(
            request,
            "400 Bad Request",
            false,
            "UDS query is too long"
        );
    }

    char query[WEB_UDS_QUERY_MAX_SIZE];

    if (httpd_req_get_url_query_str(
            request,
            query,
            sizeof(query)
        ) != ESP_OK) {

        return web_api_send_message(
            request,
            "400 Bad Request",
            false,
            "Invalid UDS query"
        );
    }

    char file_name[UDS_PROFILE_FILE_NAME_MAX_LENGTH];

    if (httpd_query_key_value(
            query,
            "profile",
            file_name,
            sizeof(file_name)
        ) == ESP_OK) {

        return web_uds_profile_send_one(
            request,
            file_name
        );
    }

    char profiles[4];

    if (httpd_query_key_value(
            query,
            "profiles",
            profiles,
            sizeof(profiles)
        ) == ESP_OK) {

        size_t offset = 0U;
        char offset_text[16];

        if (httpd_query_key_value(
                query,
                "offset",
                offset_text,
                sizeof(offset_text)
            ) == ESP_OK) {

            char *end = NULL;
            const unsigned long parsed =
                strtoul(offset_text, &end, 10);

            if ((end == offset_text) ||
                (*end != '\0') ||
                (parsed > SIZE_MAX)) {

                return web_api_send_message(
                    request,
                    "400 Bad Request",
                    false,
                    "Invalid profile-list offset"
                );
            }

            offset = (size_t)parsed;
        }

        return web_uds_profile_send_list(
            request,
            offset
        );
    }

    return ESP_ERR_NOT_FOUND;
}

static esp_err_t web_uds_profile_post_handler(
    httpd_req_t *request
)
{
    const size_t query_length =
        httpd_req_get_url_query_len(request);

    if ((query_length == 0U) ||
        (query_length >= WEB_UDS_QUERY_MAX_SIZE)) {

        return ESP_ERR_NOT_FOUND;
    }

    char query[WEB_UDS_QUERY_MAX_SIZE];
    char file_name[UDS_PROFILE_FILE_NAME_MAX_LENGTH];

    if ((httpd_req_get_url_query_str(
            request,
            query,
            sizeof(query)
        ) != ESP_OK) ||
        (httpd_query_key_value(
            query,
            "profile",
            file_name,
            sizeof(file_name)
        ) != ESP_OK)) {

        return ESP_ERR_NOT_FOUND;
    }

    char *json = NULL;
    esp_err_t result = web_uds_receive_profile_json(
        request,
        &json
    );

    if (result == ESP_OK) {
        result = uds_profile_service_save_json(
            file_name,
            json
        );
    }

    heap_caps_free(json);

    return web_api_send_message(
        request,
        (result == ESP_OK)
            ? "200 OK"
            : ((result == ESP_ERR_INVALID_ARG) ||
               (result == ESP_ERR_INVALID_SIZE))
                ? "400 Bad Request"
                : "409 Conflict",
        result == ESP_OK,
        (result == ESP_OK)
            ? "UDS profile updated"
            : esp_err_to_name(result)
    );
}

static esp_err_t web_uds_profile_remove(
    const cJSON *root
)
{
    const cJSON *file_name =
        cJSON_GetObjectItemCaseSensitive(root, "file_name");

    return cJSON_IsString(file_name)
        ? uds_profile_service_remove(file_name->valuestring)
        : ESP_ERR_INVALID_ARG;
}

static esp_err_t web_uds_did_catalog_send_list(
    httpd_req_t *request,
    size_t offset
)
{
    web_uds_log_did_catalog_memory("list", "before");

    uds_did_catalog_summary_t *catalogs = heap_caps_calloc(
        WEB_UDS_DID_CATALOG_LIST_CAPACITY,
        sizeof(*catalogs),
        MALLOC_CAP_SPIRAM |
        MALLOC_CAP_8BIT
    );

    if (catalogs == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t count = 0U;
    bool has_more = false;
    esp_err_t result = uds_did_catalog_service_list(
        offset,
        catalogs,
        WEB_UDS_DID_CATALOG_LIST_CAPACITY,
        &count,
        &has_more
    );
    cJSON *response = NULL;
    cJSON *items = NULL;

    if (result == ESP_OK) {
        response = cJSON_CreateObject();
        items = cJSON_CreateArray();

        if ((response == NULL) ||
            (items == NULL) ||
            !cJSON_AddItemToObject(
                response,
                "did_catalogs",
                items
            )) {

            cJSON_Delete(response);
            cJSON_Delete(items);
            response = NULL;
            items = NULL;
            result = ESP_ERR_NO_MEM;
        }
    }

    for (size_t index = 0U;
         (result == ESP_OK) && (index < count);
         ++index) {

        cJSON *item = cJSON_CreateObject();

        if ((item == NULL) ||
            (cJSON_AddStringToObject(
                item,
                "file_name",
                catalogs[index].file_name
            ) == NULL) ||
            (cJSON_AddStringToObject(
                item,
                "name",
                catalogs[index].name
            ) == NULL) ||
            (cJSON_AddStringToObject(
                item,
                "description",
                catalogs[index].description
            ) == NULL) ||
            (cJSON_AddNumberToObject(
                item,
                "definition_count",
                catalogs[index].definition_count
            ) == NULL) ||
            !cJSON_AddItemToArray(items, item)) {

            cJSON_Delete(item);
            result = ESP_ERR_NO_MEM;
        }
    }

    if ((result == ESP_OK) &&
        ((cJSON_AddBoolToObject(
            response,
            "success",
            true
        ) == NULL) ||
         (cJSON_AddNumberToObject(
            response,
            "offset",
            offset
        ) == NULL) ||
         (cJSON_AddBoolToObject(
            response,
            "has_more",
            has_more
        ) == NULL))) {

        result = ESP_ERR_NO_MEM;
    }

    heap_caps_free(catalogs);

    if (result != ESP_OK) {
        cJSON_Delete(response);
        web_uds_log_did_catalog_memory("list", "after");
        return web_api_send_message(
            request,
            (result == ESP_ERR_INVALID_STATE)
                ? "409 Conflict"
                : "500 Internal Server Error",
            false,
            esp_err_to_name(result)
        );
    }

    result = web_api_send_json(request, response);
    cJSON_Delete(response);
    web_uds_log_did_catalog_memory("list", "after");
    return result;
}

static esp_err_t web_uds_did_catalog_send_one(
    httpd_req_t *request,
    const char *file_name
)
{
    web_uds_log_did_catalog_memory("load", "before");

    char *json = NULL;
    esp_err_t result = uds_did_catalog_service_load_json(
        file_name,
        &json
    );

    if (result != ESP_OK) {
        heap_caps_free(json);
        web_uds_log_did_catalog_memory("load", "after");

        return web_api_send_message(
            request,
            (result == ESP_ERR_NOT_FOUND)
                ? "404 Not Found"
                : "400 Bad Request",
            false,
            esp_err_to_name(result)
        );
    }

    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    result = httpd_resp_send(
        request,
        json,
        HTTPD_RESP_USE_STRLEN
    );

    heap_caps_free(json);
    web_uds_log_did_catalog_memory("load", "after");
    return result;
}

static esp_err_t web_uds_did_catalog_get_handler(
    httpd_req_t *request
)
{
    const size_t query_length =
        httpd_req_get_url_query_len(request);

    if ((query_length == 0U) ||
        (query_length >= WEB_UDS_QUERY_MAX_SIZE)) {

        return ESP_ERR_NOT_FOUND;
    }

    char query[WEB_UDS_QUERY_MAX_SIZE];

    if (httpd_req_get_url_query_str(
            request,
            query,
            sizeof(query)
        ) != ESP_OK) {

        return ESP_ERR_NOT_FOUND;
    }

    char file_name[UDS_DID_CATALOG_FILE_NAME_MAX_LENGTH];

    if (httpd_query_key_value(
            query,
            "did_catalog",
            file_name,
            sizeof(file_name)
        ) == ESP_OK) {

        return web_uds_did_catalog_send_one(
            request,
            file_name
        );
    }

    char catalogs[4];

    if (httpd_query_key_value(
            query,
            "did_catalogs",
            catalogs,
            sizeof(catalogs)
        ) != ESP_OK) {

        return ESP_ERR_NOT_FOUND;
    }

    size_t offset = 0U;
    char offset_text[16];

    if (httpd_query_key_value(
            query,
            "offset",
            offset_text,
            sizeof(offset_text)
        ) == ESP_OK) {

        char *end = NULL;
        const unsigned long parsed =
            strtoul(offset_text, &end, 10);

        if ((end == offset_text) ||
            (*end != '\0') ||
            (parsed > SIZE_MAX)) {

            return web_api_send_message(
                request,
                "400 Bad Request",
                false,
                "Invalid DID catalog-list offset"
            );
        }

        offset = (size_t)parsed;
    }

    return web_uds_did_catalog_send_list(
        request,
        offset
    );
}

static esp_err_t web_uds_did_catalog_post_handler(
    httpd_req_t *request
)
{
    const size_t query_length =
        httpd_req_get_url_query_len(request);

    if ((query_length == 0U) ||
        (query_length >= WEB_UDS_QUERY_MAX_SIZE)) {

        return ESP_ERR_NOT_FOUND;
    }

    char query[WEB_UDS_QUERY_MAX_SIZE];
    char file_name[UDS_DID_CATALOG_FILE_NAME_MAX_LENGTH];

    if ((httpd_req_get_url_query_str(
            request,
            query,
            sizeof(query)
        ) != ESP_OK) ||
        (httpd_query_key_value(
            query,
            "did_catalog",
            file_name,
            sizeof(file_name)
        ) != ESP_OK)) {

        return ESP_ERR_NOT_FOUND;
    }

    web_uds_log_did_catalog_memory("save", "before");

    char *json = NULL;
    esp_err_t result = web_uds_receive_catalog_json(
        request,
        &json
    );

    if (result == ESP_OK) {
        result = uds_did_catalog_service_save_json(
            file_name,
            json
        );
    }

    heap_caps_free(json);
    web_uds_log_did_catalog_memory("save", "after");

    return web_api_send_message(
        request,
        (result == ESP_OK)
            ? "200 OK"
            : ((result == ESP_ERR_INVALID_ARG) ||
               (result == ESP_ERR_INVALID_SIZE))
                ? "400 Bad Request"
                : "409 Conflict",
        result == ESP_OK,
        (result == ESP_OK)
            ? "UDS DID catalog updated"
            : esp_err_to_name(result)
    );
}

static esp_err_t web_uds_did_catalog_remove(
    const cJSON *root
)
{
    const cJSON *file_name =
        cJSON_GetObjectItemCaseSensitive(root, "file_name");

    return cJSON_IsString(file_name)
        ? uds_did_catalog_service_remove(file_name->valuestring)
        : ESP_ERR_INVALID_ARG;
}

static esp_err_t web_uds_get_handler(
    httpd_req_t *request
)
{
    const esp_err_t did_catalog_result =
        web_uds_did_catalog_get_handler(request);

    if (did_catalog_result != ESP_ERR_NOT_FOUND) {
        return did_catalog_result;
    }

    const esp_err_t profile_result =
        web_uds_profile_get_handler(request);

    if (profile_result != ESP_ERR_NOT_FOUND) {
        return profile_result;
    }

    if (xSemaphoreTakeRecursive(
            s_lock,
            pdMS_TO_TICKS(WEB_UDS_LOCK_TIMEOUT_MS)
        ) != pdTRUE) {

        return web_api_send_message(
            request,
            "503 Service Unavailable",
            false,
            "UDS state is busy"
        );
    }

    uds_download_progress_t download_progress = {0};

    if (s_download_active) {
        (void)uds_download_get_progress(
            &s_download,
            &download_progress
        );

        if ((download_progress.state == UDS_DOWNLOAD_COMPLETE) ||
            (download_progress.state == UDS_DOWNLOAD_CANCELLED) ||
            (download_progress.state == UDS_DOWNLOAD_ERROR)) {

            web_uds_download_close_file();
            web_uds_download_finalize_journal(
                &download_progress,
                (download_progress.state ==
                 UDS_DOWNLOAD_COMPLETE)
                    ? "completed"
                    : (download_progress.state ==
                       UDS_DOWNLOAD_CANCELLED)
                        ? "cancelled"
                        : "failed"
            );
        }
    }

    const uint64_t now_us = esp_timer_get_time();
    const uint64_t tester_present_due_ms =
        s_tester_present_session_active &&
        (s_tester_present_deadline_us > now_us)
            ? (s_tester_present_deadline_us - now_us + 999ULL) /
              1000ULL
            : 0U;

    char *payload =
        web_uds_format_hex(
            s_response_buffer,
            s_response_size
        );
    const char *negative_name =
        uds_protocol_negative_response_name(
            s_negative_response_code
        );
    cJSON *response = cJSON_CreateObject();
    uds_client_t *active_client =
        s_download_active
            ? uds_download_client(&s_download)
            : &s_client;

    const bool valid =
        (payload != NULL) &&
        (response != NULL) &&
        (cJSON_AddBoolToObject(
            response,
            "open",
            active_client->state != UDS_CLIENT_CLOSED
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "state",
            active_client->state
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "sequence",
            s_sequence
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "event",
            s_last_event
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "result",
            s_last_result
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "transport_error",
            s_transport_error
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            response,
            "positive",
            s_positive
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "response_sid",
            s_response_service_id
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "request_sid",
            s_request_service_id
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "nrc",
            s_negative_response_code
        ) != NULL) &&
        (cJSON_AddStringToObject(
            response,
            "nrc_name",
            negative_name
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "payload_length",
            s_response_size
        ) != NULL) &&
        (cJSON_AddStringToObject(
            response,
            "payload",
            payload
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "security_level",
            s_security_level
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "security_state",
            s_security_state
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "security_request_generation",
            s_security_generation
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            response,
            "security_waiting_for_seed",
            s_security_state ==
                WEB_UDS_SECURITY_WAITING_SEED
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            response,
            "security_waiting_for_key",
            s_security_state ==
                WEB_UDS_SECURITY_SENDING_KEY
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            response,
            "security_unlocked",
            s_security_state ==
                WEB_UDS_SECURITY_UNLOCKED
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "security_result",
            s_security_result
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "security_nrc",
            s_security_negative_response_code
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            response,
            "tester_present_enabled",
            s_tester_present_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            response,
            "tester_present_active",
            s_tester_present_session_active
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "diagnostic_session",
            s_diagnostic_session
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "tester_present_interval_ms",
            s_tester_present_interval_us / 1000ULL
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "tester_present_due_ms",
            tester_present_due_ms
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "tester_present_sent",
            s_tester_present_sent
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "tester_present_deferred",
            s_tester_present_deferred_count
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "tester_present_result",
            s_tester_present_last_result
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            response,
            "download_active",
            s_download_active
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "download_state",
            download_progress.state
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "download_transferred",
            (double)download_progress.transferred_size
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "download_total",
            (double)download_progress.total_size
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "download_segment_index",
            download_progress.segment_index
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "download_segment_count",
            download_progress.segment_count
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "download_segment_transferred",
            (double)download_progress.segment_transferred_size
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "download_segment_total",
            (double)download_progress.segment_total_size
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "download_block_size",
            download_progress.block_data_capacity
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "download_result",
            download_progress.last_result
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            response,
            "download_security_unlocked",
            download_progress.security_unlocked
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            response,
            "download_default_session_restored",
            download_progress.default_session_restored
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "download_blocks",
            download_progress.acknowledged_blocks
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "download_retries",
            download_progress.retry_count
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "download_routine_polls",
            download_progress.routine_poll_count
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "download_block_retry",
            download_progress.current_block_retry
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "download_action_retry",
            download_progress.current_action_retry
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "download_nrc_action",
            download_progress.last_nrc_action
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "download_sequence_counter",
            download_progress.block_sequence_counter
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "download_elapsed_ms",
            (s_download_started_at_us != 0U)
                ? (double)(
                    ((s_download_finished_at_us != 0U
                        ? s_download_finished_at_us
                        : (uint64_t)esp_timer_get_time()) -
                     s_download_started_at_us) /
                    1000ULL
                )
                : 0.0
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "download_nrc",
            download_progress.last_negative_response_code
        ) != NULL) &&
        (cJSON_AddStringToObject(
            response,
            "download_journal",
            s_download_journal_path
        ) != NULL);

    free(payload);
    xSemaphoreGiveRecursive(s_lock);

    if (!valid) {
        cJSON_Delete(response);
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t result =
        web_api_send_json(request, response);

    cJSON_Delete(response);
    return result;
}

static esp_err_t web_uds_post_handler(
    httpd_req_t *request
)
{
    const esp_err_t profile_result =
        web_uds_profile_post_handler(request);

    if (profile_result != ESP_ERR_NOT_FOUND) {
        return profile_result;
    }

    const esp_err_t did_catalog_result =
        web_uds_did_catalog_post_handler(request);

    if (did_catalog_result != ESP_ERR_NOT_FOUND) {
        return did_catalog_result;
    }

    cJSON *root = NULL;
    esp_err_t result =
        web_uds_receive_json(request, &root);

    if (result != ESP_OK) {
        return web_api_send_message(
            request,
            "400 Bad Request",
            false,
            "Invalid JSON request"
        );
    }

    const cJSON *action =
        cJSON_GetObjectItemCaseSensitive(root, "action");

    if (cJSON_IsString(action) &&
        ((strcmp(action->valuestring, "profile_remove") == 0) ||
         (strcmp(action->valuestring, "did_catalog_remove") == 0))) {

        if (strcmp(
                action->valuestring,
                "profile_remove"
            ) == 0) {

            result = web_uds_profile_remove(root);
        } else {
            result = web_uds_did_catalog_remove(root);
        }

        cJSON_Delete(root);

        return web_api_send_message(
            request,
            (result == ESP_OK)
                ? "200 OK"
                : ((result == ESP_ERR_INVALID_ARG) ||
                   (result == ESP_ERR_INVALID_SIZE))
                    ? "400 Bad Request"
                    : "409 Conflict",
            result == ESP_OK,
            (result == ESP_OK)
                ? "UDS persistent data updated"
                : esp_err_to_name(result)
        );
    }

    if (xSemaphoreTakeRecursive(
            s_lock,
            pdMS_TO_TICKS(WEB_UDS_LOCK_TIMEOUT_MS)
        ) != pdTRUE) {

        cJSON_Delete(root);
        return web_api_send_message(
            request,
            "503 Service Unavailable",
            false,
            "UDS state is busy"
        );
    }

    if (!cJSON_IsString(action)) {
        result = ESP_ERR_INVALID_ARG;
    } else if (strcmp(action->valuestring, "configure") == 0) {
        result = s_download_active
            ? ESP_ERR_INVALID_STATE
            : web_uds_configure(root);
    } else if (strcmp(action->valuestring, "request") == 0) {
        result = s_download_active
            ? ESP_ERR_INVALID_STATE
            : web_uds_request(root);
    } else if (strcmp(action->valuestring, "download_start") == 0) {
        result = web_uds_download_start(root);
    } else if (strcmp(action->valuestring, "download_cancel") == 0) {
        result = s_download_active
            ? uds_download_cancel(&s_download)
            : ESP_ERR_INVALID_STATE;

        if (result == ESP_OK) {
            web_uds_download_close_file();
        }
    } else if (strcmp(
                   action->valuestring,
                   "security_cancel"
               ) == 0) {

        result = s_download_active
            ? ESP_ERR_INVALID_STATE
            : web_uds_security_cancel();
    } else if (strcmp(action->valuestring, "close") == 0) {
        web_uds_security_reset();
        web_uds_tester_present_stop();

        if (s_download_active) {
            uds_download_progress_t progress = {0};
            (void)uds_download_get_progress(
                &s_download,
                &progress
            );
            web_uds_download_finalize_journal(
                &progress,
                "cancelled"
            );
            web_uds_download_close_file();
            result = uds_download_close(&s_download);
            s_download_active = false;
        } else {
            result =
                (s_client.state == UDS_CLIENT_CLOSED)
                    ? ESP_OK
                    : uds_client_close(&s_client);
        }

        if (result == ESP_OK) {
            s_sequence++;
        }
    } else {
        result = ESP_ERR_INVALID_ARG;
    }

    xSemaphoreGiveRecursive(s_lock);
    cJSON_Delete(root);

    return web_api_send_message(
        request,
        (result == ESP_OK)
            ? "200 OK"
            : ((result == ESP_ERR_INVALID_ARG) ||
               (result == ESP_ERR_INVALID_SIZE))
                ? "400 Bad Request"
                : "409 Conflict",
        result == ESP_OK,
        (result == ESP_OK)
            ? "UDS command accepted"
            : esp_err_to_name(result)
    );
}

esp_err_t web_uds_api_register(
    httpd_handle_t server
)
{
    if ((server == NULL) ||
        !isotp_service_is_running()) {

        return ESP_ERR_INVALID_STATE;
    }

    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateRecursiveMutex();
        s_receive_buffer = heap_caps_malloc(
            WEB_UDS_BUFFER_SIZE,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
        s_transmit_buffer = heap_caps_malloc(
            WEB_UDS_BUFFER_SIZE,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
        s_response_buffer = heap_caps_malloc(
            WEB_UDS_BUFFER_SIZE,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
        s_manual_request_buffer = heap_caps_malloc(
            UDS_CLIENT_MEMORY_DATA_MAX_LENGTH,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
        s_download_transfer_buffer = heap_caps_malloc(
            UDS_CLIENT_TRANSFER_DATA_MAX_LENGTH,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );

        if ((s_lock == NULL) ||
            (s_receive_buffer == NULL) ||
            (s_transmit_buffer == NULL) ||
            (s_response_buffer == NULL) ||
            (s_manual_request_buffer == NULL) ||
            (s_download_transfer_buffer == NULL)) {

            heap_caps_free(s_receive_buffer);
            heap_caps_free(s_transmit_buffer);
            heap_caps_free(s_response_buffer);
            heap_caps_free(s_manual_request_buffer);
            heap_caps_free(s_download_transfer_buffer);
            s_receive_buffer = NULL;
            s_transmit_buffer = NULL;
            s_response_buffer = NULL;
            s_manual_request_buffer = NULL;
            s_download_transfer_buffer = NULL;

            if (s_lock != NULL) {
                vSemaphoreDelete(s_lock);
                s_lock = NULL;
            }

            return ESP_ERR_NO_MEM;
        }

        s_client.state = UDS_CLIENT_CLOSED;

        const esp_err_t provider_result =
            uds_security_provider_init(
                &s_security_provider
            );

        if (provider_result != ESP_OK) {
            return provider_result;
        }

        s_security_provider_initialized = true;

        const BaseType_t task_result =
            xTaskCreateWithCaps(
                web_uds_worker_task,
                "uds_worker",
                WEB_UDS_WORKER_TASK_STACK_SIZE,
                NULL,
                APP_TASK_PRIORITY_WEB_CAN,
                &s_worker_task,
                MALLOC_CAP_SPIRAM |
                MALLOC_CAP_8BIT
            );

        if (task_result != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }

    const httpd_uri_t get = {
        .uri = "/api/uds",
        .method = HTTP_GET,
        .handler = web_uds_get_handler,
    };
    const httpd_uri_t post = {
        .uri = "/api/uds",
        .method = HTTP_POST,
        .handler = web_uds_post_handler,
    };

    esp_err_t result =
        httpd_register_uri_handler(server, &get);

    if (result == ESP_OK) {
        result = httpd_register_uri_handler(server, &post);
    }

    return result;
}
