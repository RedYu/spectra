/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "web_uds_api.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_task_priorities.h"
#include "uds_client.h"
#include "uds_download.h"
#include "storage_sd_service.h"
#include "web_api_common.h"

#define WEB_UDS_BUFFER_SIZE       (1024U)
#define WEB_UDS_BODY_MAX_SIZE     (4096U)
#define WEB_UDS_LOCK_TIMEOUT_MS   (100U)
#define WEB_UDS_DOWNLOAD_TASK_STACK_SIZE (4096U)

static SemaphoreHandle_t s_lock = NULL;
static uint8_t *s_receive_buffer = NULL;
static uint8_t *s_transmit_buffer = NULL;
static uint8_t *s_response_buffer = NULL;
static size_t s_response_size = 0U;
static uds_client_t s_client;
static uds_client_config_t s_client_config;
static bool s_client_config_valid = false;
static uds_download_t s_download;
static bool s_download_active = false;
static FILE *s_download_file = NULL;
static uint64_t s_download_file_offset = 0U;
static uint8_t *s_download_transfer_buffer = NULL;
static TaskHandle_t s_download_task = NULL;
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

static char *web_uds_format_hex(
    const uint8_t *data,
    size_t size
);

static void web_uds_client_callback(
    const uds_client_event_t *event,
    void *context
);

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

static void web_uds_download_close_file(void);

static esp_err_t web_uds_download_start(
    const cJSON *root
);

static void web_uds_download_task(
    void *context
);

static esp_err_t web_uds_get_handler(
    httpd_req_t *request
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

    char *body = malloc(request->content_len + 1U);

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
            free(body);
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

    free(body);

    return (*root != NULL)
        ? ESP_OK
        : ESP_ERR_INVALID_ARG;
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

    s_sequence++;
    xSemaphoreGiveRecursive(s_lock);
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
    bool extended = false;
    bool can_fd = false;
    bool brs = false;

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
        s_last_result = ESP_OK;
        s_response_size = 0U;
        s_negative_response_code = 0U;
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

    if (strcmp(kind->valuestring, "request_download") == 0) {
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

        return uds_client_request_download(
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

        return uds_client_diagnostic_session_control(
            &s_client,
            (uint8_t)value,
            suppress,
            now_us
        );
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

        uint8_t parameters[WEB_UDS_BUFFER_SIZE - 1U];
        size_t parameter_size = 0U;
        const esp_err_t parse_result =
            web_uds_parse_hex(
                data->valuestring,
                parameters,
                sizeof(parameters),
                &parameter_size
            );

        if (parse_result != ESP_OK) {
            return parse_result;
        }

        return uds_client_request(
            &s_client,
            (uint8_t)value,
            parameters,
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

    if ((s_download_file == NULL) ||
        (offset != s_download_file_offset)) {

        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result =
        storage_sd_service_read(
            s_download_file,
            buffer,
            capacity,
            read_size
        );

    if (result == ESP_OK) {
        s_download_file_offset += *read_size;
    }

    return result;
}

static void web_uds_download_close_file(void)
{
    if (s_download_file != NULL) {
        (void)storage_sd_service_close(&s_download_file);
    }
}

static void web_uds_download_task(
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

            if ((progress.state == UDS_DOWNLOAD_COMPLETE) ||
                (progress.state == UDS_DOWNLOAD_CANCELLED) ||
                (progress.state == UDS_DOWNLOAD_ERROR)) {

                web_uds_download_close_file();
                s_download_task = NULL;
            }
        }

        const bool finished =
            !running || (s_download_task == NULL);

        if (finished) {
            s_download_task = NULL;
        }

        xSemaphoreGiveRecursive(s_lock);

        if (finished) {
            break;
        }

        vTaskDelay(1U);
    }

    vTaskDelete(NULL);
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
    uint64_t address = 0U;

    if (!s_client_config_valid ||
        s_download_active ||
        !cJSON_IsString(path) ||
        (path->valuestring[0] == '\0') ||
        (strstr(path->valuestring, "..") != NULL) ||
        !web_uds_number(root, "data_format", UINT8_MAX, &data_format) ||
        !web_uds_number(root, "address_length", 8U, &address_length) ||
        !web_uds_number(root, "size_length", 8U, &size_length) ||
        !web_uds_hex_uint64(root, "address", &address)) {

        return ESP_ERR_INVALID_ARG;
    }

    struct stat information = {0};
    esp_err_t result =
        storage_sd_service_stat(
            path->valuestring,
            &information
        );

    if ((result != ESP_OK) ||
        !S_ISREG(information.st_mode) ||
        (information.st_size <= 0)) {

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
            .memory_size = (uint64_t)information.st_size,
            .memory_size_length = (uint8_t)size_length,
        };

        result = uds_download_open(&s_download, &config);
    }

    if (result == ESP_OK) {
        s_download_file_offset = 0U;
        result =
            uds_download_start(
                &s_download,
                esp_timer_get_time()
            );
    }

    if (result == ESP_OK) {
        s_download_active = true;
        const BaseType_t task_result =
            xTaskCreate(
                web_uds_download_task,
                "uds_download",
                WEB_UDS_DOWNLOAD_TASK_STACK_SIZE,
                NULL,
                APP_TASK_PRIORITY_WEB_CAN,
                &s_download_task
            );

        if (task_result != pdPASS) {
            s_download_active = false;
            result = ESP_ERR_NO_MEM;
            (void)uds_download_close(&s_download);
            web_uds_download_close_file();
            (void)uds_client_open(&s_client, &s_client_config);
        }
    } else {
        web_uds_download_close_file();
        (void)uds_client_open(&s_client, &s_client_config);
    }

    return result;
}

static esp_err_t web_uds_get_handler(
    httpd_req_t *request
)
{
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
        }
    } else if (s_client.state != UDS_CLIENT_CLOSED) {
        (void)uds_client_poll(
            &s_client,
            esp_timer_get_time()
        );
    }

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
            "download_block_size",
            download_progress.block_data_capacity
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "download_result",
            download_progress.last_result
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
    } else if (strcmp(action->valuestring, "close") == 0) {
        if (s_download_active) {
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
        s_download_transfer_buffer = heap_caps_malloc(
            UDS_CLIENT_TRANSFER_DATA_MAX_LENGTH,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );

        if ((s_lock == NULL) ||
            (s_receive_buffer == NULL) ||
            (s_transmit_buffer == NULL) ||
            (s_response_buffer == NULL) ||
            (s_download_transfer_buffer == NULL)) {

            heap_caps_free(s_receive_buffer);
            heap_caps_free(s_transmit_buffer);
            heap_caps_free(s_response_buffer);
            heap_caps_free(s_download_transfer_buffer);
            s_receive_buffer = NULL;
            s_transmit_buffer = NULL;
            s_response_buffer = NULL;
            s_download_transfer_buffer = NULL;

            if (s_lock != NULL) {
                vSemaphoreDelete(s_lock);
                s_lock = NULL;
            }

            return ESP_ERR_NO_MEM;
        }

        s_client.state = UDS_CLIENT_CLOSED;
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
