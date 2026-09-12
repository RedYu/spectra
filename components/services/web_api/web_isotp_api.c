/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "web_isotp_api.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "isotp_service.h"
#include "web_api_common.h"

#define WEB_ISOTP_BUFFER_SIZE       (1024U)
#define WEB_ISOTP_BODY_MAX_SIZE     (4096U)
#define WEB_ISOTP_LOCK_TIMEOUT_MS   (100U)

typedef enum
{
    WEB_ISOTP_STATE_CLOSED = 0,
    WEB_ISOTP_STATE_READY,
    WEB_ISOTP_STATE_TRANSMITTING,
    WEB_ISOTP_STATE_RECEIVED,
    WEB_ISOTP_STATE_ERROR,

} web_isotp_state_t;

static SemaphoreHandle_t s_lock = NULL;
static uint8_t *s_receive_buffer = NULL;
static uint8_t *s_transmit_buffer = NULL;
static uint8_t *s_result_buffer = NULL;
static uint32_t s_channel_id = ISOTP_SERVICE_CHANNEL_ID_NONE;
static uint32_t s_event_sequence = 0U;
static size_t s_result_size = 0U;
static web_isotp_state_t s_state = WEB_ISOTP_STATE_CLOSED;
static esp_err_t s_last_result = ESP_OK;
static isotp_session_error_t s_session_error =
    ISOTP_SESSION_ERROR_NONE;

static bool web_isotp_get_u32(
    const cJSON *root,
    const char *name,
    uint32_t maximum,
    uint32_t *value
);

static bool web_isotp_get_bool(
    const cJSON *root,
    const char *name,
    bool fallback,
    bool *value
);

static esp_err_t web_isotp_receive_body(
    httpd_req_t *request,
    cJSON **root
);

static esp_err_t web_isotp_parse_hex(
    const char *text,
    uint8_t *buffer,
    size_t capacity,
    size_t *size
);

static char *web_isotp_format_hex(
    const uint8_t *data,
    size_t size
);

static void web_isotp_event_callback(
    const isotp_service_event_t *event,
    void *context
);

static esp_err_t web_isotp_configure(
    const cJSON *root
);

static esp_err_t web_isotp_send(
    const cJSON *root
);

static esp_err_t web_isotp_get_handler(
    httpd_req_t *request
);

static esp_err_t web_isotp_post_handler(
    httpd_req_t *request
);

static bool web_isotp_get_u32(
    const cJSON *root,
    const char *name,
    uint32_t maximum,
    uint32_t *value
)
{
    const cJSON *item =
        cJSON_GetObjectItemCaseSensitive(
            root,
            name
        );

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

static bool web_isotp_get_bool(
    const cJSON *root,
    const char *name,
    bool fallback,
    bool *value
)
{
    const cJSON *item =
        cJSON_GetObjectItemCaseSensitive(
            root,
            name
        );

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

static esp_err_t web_isotp_receive_body(
    httpd_req_t *request,
    cJSON **root
)
{
    if ((request == NULL) ||
        (root == NULL) ||
        (request->content_len == 0U) ||
        (request->content_len > WEB_ISOTP_BODY_MAX_SIZE)) {

        return ESP_ERR_INVALID_SIZE;
    }

    char *body =
        malloc(request->content_len + 1U);

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

static esp_err_t web_isotp_parse_hex(
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
    int high_nibble = -1;

    while (*text != '\0') {
        if (isspace((unsigned char)*text) ||
            (*text == ',') ||
            (*text == ':') ||
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

        if (high_nibble < 0) {
            high_nibble = value;
        } else {
            if (*size >= capacity) {
                return ESP_ERR_INVALID_SIZE;
            }

            buffer[*size] =
                (uint8_t)((high_nibble << 4) | value);
            (*size)++;
            high_nibble = -1;
        }

        ++text;
    }

    return ((*size != 0U) && (high_nibble < 0))
        ? ESP_OK
        : ESP_ERR_INVALID_ARG;
}

static char *web_isotp_format_hex(
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

static void web_isotp_event_callback(
    const isotp_service_event_t *event,
    void *context
)
{
    (void)context;

    if ((event == NULL) || (s_lock == NULL)) {
        return;
    }

    if (xSemaphoreTake(
            s_lock,
            pdMS_TO_TICKS(WEB_ISOTP_LOCK_TIMEOUT_MS)
        ) != pdTRUE) {

        return;
    }

    s_event_sequence++;
    s_last_result = event->result;
    s_session_error = event->session_error;

    switch (event->type) {
        case ISOTP_SERVICE_EVENT_RECEIVED:
            s_result_size =
                (event->payload_length < WEB_ISOTP_BUFFER_SIZE)
                    ? event->payload_length
                    : WEB_ISOTP_BUFFER_SIZE;
            memcpy(
                s_result_buffer,
                event->payload,
                s_result_size
            );
            s_state = WEB_ISOTP_STATE_RECEIVED;
            break;

        case ISOTP_SERVICE_EVENT_TRANSMITTED:
            s_state = WEB_ISOTP_STATE_READY;
            break;

        case ISOTP_SERVICE_EVENT_ERROR:
            s_state = WEB_ISOTP_STATE_ERROR;
            break;

        default:
            break;
    }

    xSemaphoreGive(s_lock);
}

static esp_err_t web_isotp_configure(
    const cJSON *root
)
{
    uint32_t bus = 0U;
    uint32_t receive_identifier = 0U;
    uint32_t transmit_identifier = 0U;
    uint32_t link_data_length = 0U;
    uint32_t block_size = 0U;
    uint32_t st_min = 0U;
    uint32_t timeout_ms = 0U;
    bool extended = false;
    bool can_fd = false;
    bool brs = false;

    if (!web_isotp_get_u32(root, "bus", 1U, &bus) ||
        !web_isotp_get_u32(
            root,
            "rx_id",
            CAN_FRAME_EXTENDED_ID_MAX,
            &receive_identifier
        ) ||
        !web_isotp_get_u32(
            root,
            "tx_id",
            CAN_FRAME_EXTENDED_ID_MAX,
            &transmit_identifier
        ) ||
        !web_isotp_get_u32(
            root,
            "link_data_length",
            CAN_FRAME_FD_DATA_MAX_LENGTH,
            &link_data_length
        ) ||
        !web_isotp_get_u32(
            root,
            "block_size",
            UINT8_MAX,
            &block_size
        ) ||
        !web_isotp_get_u32(
            root,
            "st_min",
            UINT8_MAX,
            &st_min
        ) ||
        !web_isotp_get_u32(
            root,
            "timeout_ms",
            60000U,
            &timeout_ms
        ) ||
        !web_isotp_get_bool(root, "extended", false, &extended) ||
        !web_isotp_get_bool(root, "fd", false, &can_fd) ||
        !web_isotp_get_bool(root, "brs", false, &brs) ||
        (timeout_ms == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (s_channel_id != ISOTP_SERVICE_CHANNEL_ID_NONE) {
        const esp_err_t close_result =
            isotp_service_close_channel(s_channel_id);

        if (close_result != ESP_OK) {
            return close_result;
        }

        s_channel_id = ISOTP_SERVICE_CHANNEL_ID_NONE;
    }

    const isotp_service_channel_config_t config = {
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
                (uint64_t)timeout_ms * 1000ULL,
            .consecutive_frame_timeout_us =
                (uint64_t)timeout_ms * 1000ULL,
        },
        .receive_buffer = s_receive_buffer,
        .receive_capacity = WEB_ISOTP_BUFFER_SIZE,
        .transmit_buffer = s_transmit_buffer,
        .transmit_capacity = WEB_ISOTP_BUFFER_SIZE,
        .callback = web_isotp_event_callback,
        .callback_context = NULL,
    };

    const esp_err_t result =
        isotp_service_open_channel(
            &config,
            &s_channel_id
        );

    if (result == ESP_OK) {
        s_state = WEB_ISOTP_STATE_READY;
        s_last_result = ESP_OK;
        s_session_error = ISOTP_SESSION_ERROR_NONE;
        s_result_size = 0U;
        s_event_sequence++;
    }

    return result;
}

static esp_err_t web_isotp_send(
    const cJSON *root
)
{
    const cJSON *data =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "data"
        );

    if (!cJSON_IsString(data) ||
        (s_channel_id == ISOTP_SERVICE_CHANNEL_ID_NONE)) {

        return ESP_ERR_INVALID_STATE;
    }

    uint8_t payload[WEB_ISOTP_BUFFER_SIZE];
    size_t payload_size = 0U;
    const esp_err_t parse_result =
        web_isotp_parse_hex(
            data->valuestring,
            payload,
            sizeof(payload),
            &payload_size
        );

    if (parse_result != ESP_OK) {
        return parse_result;
    }

    const esp_err_t result =
        isotp_service_send(
            s_channel_id,
            payload,
            payload_size
        );

    if (result == ESP_OK) {
        s_state = WEB_ISOTP_STATE_TRANSMITTING;
        s_last_result = ESP_OK;
        s_session_error = ISOTP_SESSION_ERROR_NONE;
        s_event_sequence++;
    }

    return result;
}

static esp_err_t web_isotp_get_handler(
    httpd_req_t *request
)
{
    if (xSemaphoreTake(
            s_lock,
            pdMS_TO_TICKS(WEB_ISOTP_LOCK_TIMEOUT_MS)
        ) != pdTRUE) {

        return web_api_send_message(
            request,
            "503 Service Unavailable",
            false,
            "ISO-TP state is busy"
        );
    }

    char *payload =
        web_isotp_format_hex(
            s_result_buffer,
            s_result_size
        );

    cJSON *response = cJSON_CreateObject();

    const bool valid =
        (payload != NULL) &&
        (response != NULL) &&
        (cJSON_AddBoolToObject(
            response,
            "open",
            s_channel_id != ISOTP_SERVICE_CHANNEL_ID_NONE
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "state",
            s_state
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "sequence",
            s_event_sequence
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "result",
            s_last_result
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "session_error",
            s_session_error
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "payload_length",
            s_result_size
        ) != NULL) &&
        (cJSON_AddStringToObject(
            response,
            "payload",
            payload
        ) != NULL);

    free(payload);
    xSemaphoreGive(s_lock);

    if (!valid) {
        cJSON_Delete(response);
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t result =
        web_api_send_json(request, response);

    cJSON_Delete(response);
    return result;
}

static esp_err_t web_isotp_post_handler(
    httpd_req_t *request
)
{
    cJSON *root = NULL;
    esp_err_t result =
        web_isotp_receive_body(
            request,
            &root
        );

    if (result != ESP_OK) {
        return web_api_send_message(
            request,
            "400 Bad Request",
            false,
            "Invalid JSON request"
        );
    }

    const cJSON *action =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "action"
        );

    if (!cJSON_IsString(action)) {
        result = ESP_ERR_INVALID_ARG;
    } else if (strcmp(action->valuestring, "configure") == 0) {
        if (xSemaphoreTake(
                s_lock,
                pdMS_TO_TICKS(WEB_ISOTP_LOCK_TIMEOUT_MS)
            ) == pdTRUE) {

            result = web_isotp_configure(root);
            xSemaphoreGive(s_lock);
        } else {
            result = ESP_ERR_TIMEOUT;
        }
    } else if (strcmp(action->valuestring, "send") == 0) {
        if (xSemaphoreTake(
                s_lock,
                pdMS_TO_TICKS(WEB_ISOTP_LOCK_TIMEOUT_MS)
            ) == pdTRUE) {

            result = web_isotp_send(root);
            xSemaphoreGive(s_lock);
        } else {
            result = ESP_ERR_TIMEOUT;
        }
    } else if (strcmp(action->valuestring, "close") == 0) {
        if (xSemaphoreTake(
                s_lock,
                pdMS_TO_TICKS(WEB_ISOTP_LOCK_TIMEOUT_MS)
            ) == pdTRUE) {

            result =
                (s_channel_id == ISOTP_SERVICE_CHANNEL_ID_NONE)
                    ? ESP_OK
                    : isotp_service_close_channel(s_channel_id);

            if (result == ESP_OK) {
                s_channel_id = ISOTP_SERVICE_CHANNEL_ID_NONE;
                s_state = WEB_ISOTP_STATE_CLOSED;
                s_event_sequence++;
            }

            xSemaphoreGive(s_lock);
        } else {
            result = ESP_ERR_TIMEOUT;
        }
    } else {
        result = ESP_ERR_INVALID_ARG;
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
            ? "ISO-TP command accepted"
            : esp_err_to_name(result)
    );
}

esp_err_t web_isotp_api_register(
    httpd_handle_t server
)
{
    if ((server == NULL) ||
        !isotp_service_is_running()) {

        return ESP_ERR_INVALID_STATE;
    }

    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();

        s_receive_buffer = heap_caps_malloc(
            WEB_ISOTP_BUFFER_SIZE,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
        s_transmit_buffer = heap_caps_malloc(
            WEB_ISOTP_BUFFER_SIZE,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
        s_result_buffer = heap_caps_malloc(
            WEB_ISOTP_BUFFER_SIZE,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );

        if ((s_lock == NULL) ||
            (s_receive_buffer == NULL) ||
            (s_transmit_buffer == NULL) ||
            (s_result_buffer == NULL)) {

            heap_caps_free(s_receive_buffer);
            heap_caps_free(s_transmit_buffer);
            heap_caps_free(s_result_buffer);

            s_receive_buffer = NULL;
            s_transmit_buffer = NULL;
            s_result_buffer = NULL;

            if (s_lock != NULL) {
                vSemaphoreDelete(s_lock);
                s_lock = NULL;
            }

            return ESP_ERR_NO_MEM;
        }
    }

    const httpd_uri_t get = {
        .uri = "/api/isotp",
        .method = HTTP_GET,
        .handler = web_isotp_get_handler,
    };
    const httpd_uri_t post = {
        .uri = "/api/isotp",
        .method = HTTP_POST,
        .handler = web_isotp_post_handler,
    };

    esp_err_t result =
        httpd_register_uri_handler(server, &get);

    if (result == ESP_OK) {
        result = httpd_register_uri_handler(server, &post);
    }

    return result;
}
