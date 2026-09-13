/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "web_xcp_api.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "web_api_common.h"
#include "xcp_commands.h"
#include "xcp_protocol.h"
#include "xcp_service.h"

#define WEB_XCP_BODY_MAX_SIZE      (4096U)
#define WEB_XCP_LOCK_TIMEOUT_MS    (100U)
#define WEB_XCP_DEFAULT_TIMEOUT_MS (1000U)
#define WEB_XCP_RESPONSE_MAX_SIZE  (512U)

static SemaphoreHandle_t s_lock = NULL;
static uint32_t s_session_id = XCP_SERVICE_SESSION_ID_NONE;
static uint8_t s_response[WEB_XCP_RESPONSE_MAX_SIZE];
static size_t s_response_size = 0U;
static esp_err_t s_last_result = ESP_OK;
static uint32_t s_sequence = 0U;
static char s_operation[24] = "none";
static xcp_get_status_response_t s_status;
static xcp_get_communication_mode_info_response_t s_communication;
static xcp_get_id_response_t s_identification;
static char s_identification_text[
    WEB_XCP_RESPONSE_MAX_SIZE + 1U
] = {0};

static bool web_xcp_number(
    const cJSON *root,
    const char *name,
    uint32_t maximum,
    uint32_t *value
);

static bool web_xcp_optional_number(
    const cJSON *root,
    const char *name,
    uint32_t maximum,
    uint32_t fallback,
    uint32_t *value
);

static bool web_xcp_boolean(
    const cJSON *root,
    const char *name,
    bool fallback,
    bool *value
);

static esp_err_t web_xcp_receive_json(
    httpd_req_t *request,
    cJSON **root
);

static esp_err_t web_xcp_parse_hex(
    const char *text,
    uint8_t *buffer,
    size_t capacity,
    size_t *size
);

static char *web_xcp_format_hex(
    const uint8_t *data,
    size_t size
);

static esp_err_t web_xcp_configure(
    const cJSON *root
);

static esp_err_t web_xcp_execute(
    const cJSON *root
);

static esp_err_t web_xcp_execute_command(
    const char *operation,
    const uint8_t *command,
    size_t command_size
);

static esp_err_t web_xcp_discovery(
    const cJSON *root,
    const char *action
);

static esp_err_t web_xcp_write_memory(
    const cJSON *root
);

static esp_err_t web_xcp_write_memory(
    const cJSON *root
)
{
    const cJSON *data_item =
        cJSON_GetObjectItemCaseSensitive(root, "data");
    uint32_t extension = 0U;
    uint32_t address = 0U;
    uint32_t range_start = 0U;
    uint32_t range_end = 0U;
    bool confirmed = false;

    if (!cJSON_IsString(data_item) ||
        !web_xcp_boolean(root, "confirmed", false, &confirmed) ||
        !confirmed ||
        !web_xcp_optional_number(
            root,
            "address_extension",
            UINT8_MAX,
            0U,
            &extension
        ) ||
        !web_xcp_number(root, "address", UINT32_MAX, &address) ||
        !web_xcp_number(root, "range_start", UINT32_MAX, &range_start) ||
        !web_xcp_number(root, "range_end", UINT32_MAX, &range_end) ||
        (range_start > range_end) ||
        (address < range_start) ||
        (address > range_end)) {

        return ESP_ERR_INVALID_ARG;
    }

    xcp_service_session_info_t info = {0};
    esp_err_t result =
        xcp_service_get_session_info(s_session_id, &info);

    if ((result != ESP_OK) || !info.connected ||
        (info.slave.address_granularity > 2U) ||
        (info.slave.maximum_cto < 3U)) {

        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[XCP_CAN_FD_CTO_MAX_SIZE - 2U] = {0};
    size_t data_size = 0U;
    result = web_xcp_parse_hex(
        data_item->valuestring,
        data,
        sizeof(data),
        &data_size
    );

    const size_t granularity =
        (size_t)1U << info.slave.address_granularity;
    const size_t maximum_data_size =
        info.slave.maximum_cto - 2U;
    const uint64_t last_address =
        (uint64_t)address + data_size - 1U;

    if ((result != ESP_OK) ||
        (data_size > maximum_data_size) ||
        ((data_size % granularity) != 0U) ||
        ((data_size / granularity) > UINT8_MAX) ||
        (last_address > range_end) ||
        (last_address > UINT32_MAX)) {

        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t command[XCP_CAN_FD_CTO_MAX_SIZE] = {0};
    size_t command_size = 0U;
    result = xcp_command_encode_set_mta(
        (uint8_t)extension,
        address,
        info.slave.byte_order_big_endian,
        command,
        sizeof(command),
        &command_size
    );

    if (result == ESP_OK) {
        result = web_xcp_execute_command(
            "set_mta",
            command,
            command_size
        );
    }

    if (result == ESP_OK) {
        result = xcp_command_encode_download(
            (uint8_t)(data_size / granularity),
            data,
            data_size,
            command,
            sizeof(command),
            &command_size
        );
    }

    if (result == ESP_OK) {
        result = web_xcp_execute_command(
            "write_memory",
            command,
            command_size
        );
    }

    s_last_result = result;
    return result;
}

static esp_err_t web_xcp_get_handler(
    httpd_req_t *request
);

static esp_err_t web_xcp_post_handler(
    httpd_req_t *request
);

static bool web_xcp_number(
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

static bool web_xcp_optional_number(
    const cJSON *root,
    const char *name,
    uint32_t maximum,
    uint32_t fallback,
    uint32_t *value
)
{
    if (cJSON_GetObjectItemCaseSensitive(root, name) == NULL) {
        *value = fallback;
        return true;
    }

    return web_xcp_number(root, name, maximum, value);
}

static bool web_xcp_boolean(
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

static esp_err_t web_xcp_receive_json(
    httpd_req_t *request,
    cJSON **root
)
{
    if ((request == NULL) ||
        (root == NULL) ||
        (request->content_len == 0U) ||
        (request->content_len > WEB_XCP_BODY_MAX_SIZE)) {

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

static esp_err_t web_xcp_parse_hex(
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

static char *web_xcp_format_hex(
    const uint8_t *data,
    size_t size
)
{
    static const char digits[] = "0123456789ABCDEF";
    const size_t capacity = (size * 3U) + 1U;
    char *text = malloc(capacity);

    if (text == NULL) {
        return NULL;
    }

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

static esp_err_t web_xcp_configure(
    const cJSON *root
)
{
    uint32_t bus = 0U;
    uint32_t command_identifier = 0U;
    uint32_t response_identifier = 0U;
    uint32_t transmit_data_length = 8U;
    uint32_t padding_byte = 0U;
    uint32_t timeout_ms = WEB_XCP_DEFAULT_TIMEOUT_MS;
    bool extended = false;
    bool can_fd = false;
    bool brs = false;

    if (!web_xcp_number(root, "bus", CAN_BUS_SECONDARY, &bus) ||
        !web_xcp_number(
            root,
            "command_identifier",
            CAN_FRAME_EXTENDED_ID_MAX,
            &command_identifier
        ) ||
        !web_xcp_number(
            root,
            "response_identifier",
            CAN_FRAME_EXTENDED_ID_MAX,
            &response_identifier
        ) ||
        !web_xcp_optional_number(
            root,
            "transmit_data_length",
            CAN_FRAME_FD_DATA_MAX_LENGTH,
            8U,
            &transmit_data_length
        ) ||
        !web_xcp_optional_number(
            root,
            "padding_byte",
            UINT8_MAX,
            0U,
            &padding_byte
        ) ||
        !web_xcp_optional_number(
            root,
            "timeout_ms",
            UINT32_MAX,
            WEB_XCP_DEFAULT_TIMEOUT_MS,
            &timeout_ms
        ) ||
        !web_xcp_boolean(root, "extended", false, &extended) ||
        !web_xcp_boolean(root, "can_fd", false, &can_fd) ||
        !web_xcp_boolean(root, "brs", false, &brs) ||
        (transmit_data_length == 0U) ||
        (timeout_ms == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (s_session_id != XCP_SERVICE_SESSION_ID_NONE) {
        const esp_err_t close_result =
            xcp_service_close_session(s_session_id);

        if (close_result != ESP_OK) {
            return close_result;
        }

        s_session_id = XCP_SERVICE_SESSION_ID_NONE;
    }

    const xcp_service_session_config_t config = {
        .bus = (can_bus_id_t)bus,
        .command_identifier = command_identifier,
        .response_identifier = response_identifier,
        .extended_identifier = extended,
        .can_fd = can_fd,
        .bit_rate_switch = brs,
        .transmit_data_length = (uint8_t)transmit_data_length,
        .padding_byte = (uint8_t)padding_byte,
        .response_timeout_ms = timeout_ms,
        .callback = NULL,
        .callback_context = NULL,
    };

    const esp_err_t result =
        xcp_service_open_session(
            &config,
            &s_session_id
        );

    if (result == ESP_OK) {
        s_response_size = 0U;
        s_last_result = ESP_OK;
        s_sequence++;
    }

    return result;
}

static esp_err_t web_xcp_execute(
    const cJSON *root
)
{
    const cJSON *command =
        cJSON_GetObjectItemCaseSensitive(root, "command");

    if (!cJSON_IsString(command) ||
        (s_session_id == XCP_SERVICE_SESSION_ID_NONE)) {

        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[XCP_SERVICE_RESPONSE_MAX_SIZE];
    size_t data_size = 0U;
    const esp_err_t parse_result =
        web_xcp_parse_hex(
            command->valuestring,
            data,
            sizeof(data),
            &data_size
        );

    if (parse_result != ESP_OK) {
        return parse_result;
    }

    s_response_size = 0U;
    const esp_err_t result =
        xcp_service_execute_cto(
            s_session_id,
            data,
            data_size,
            s_response,
            sizeof(s_response),
            &s_response_size
        );

    s_last_result = result;
    strcpy(s_operation, "execute");
    s_sequence++;
    return result;
}

static esp_err_t web_xcp_execute_command(
    const char *operation,
    const uint8_t *command,
    size_t command_size
)
{
    if (s_session_id == XCP_SERVICE_SESSION_ID_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    s_response_size = 0U;
    const esp_err_t result =
        xcp_service_execute_cto(
            s_session_id,
            command,
            command_size,
            s_response,
            sizeof(s_response),
            &s_response_size
        );

    s_last_result = result;
    strncpy(s_operation, operation, sizeof(s_operation) - 1U);
    s_operation[sizeof(s_operation) - 1U] = '\0';
    s_sequence++;
    return result;
}

static esp_err_t web_xcp_discovery(
    const cJSON *root,
    const char *action
)
{
    uint8_t command[XCP_CAN_CLASSIC_CTO_MAX_SIZE] = {0};
    size_t command_size = 0U;
    esp_err_t result = ESP_ERR_INVALID_ARG;

    if (strcmp(action, "get_status") == 0) {
        result = xcp_command_encode_get_status(
            command,
            sizeof(command),
            &command_size
        );
    } else if (strcmp(action, "get_comm_mode_info") == 0) {
        result = xcp_command_encode_get_communication_mode_info(
            command,
            sizeof(command),
            &command_size
        );
    } else if (strcmp(action, "get_id") == 0) {
        uint32_t type = 0U;

        if (!web_xcp_optional_number(
                root,
                "type",
                UINT8_MAX,
                0U,
                &type
            )) {

            return ESP_ERR_INVALID_ARG;
        }

        result = xcp_command_encode_get_id(
            (uint8_t)type,
            command,
            sizeof(command),
            &command_size
        );
    } else if (strcmp(action, "set_mta") == 0) {
        uint32_t extension = 0U;
        uint32_t address = 0U;
        xcp_service_session_info_t info = {0};

        if (!web_xcp_optional_number(
                root,
                "address_extension",
                UINT8_MAX,
                0U,
                &extension
            ) ||
            !web_xcp_number(root, "address", UINT32_MAX, &address) ||
            (xcp_service_get_session_info(s_session_id, &info) != ESP_OK)) {

            return ESP_ERR_INVALID_ARG;
        }

        result = xcp_command_encode_set_mta(
            (uint8_t)extension,
            address,
            info.slave.byte_order_big_endian,
            command,
            sizeof(command),
            &command_size
        );
    } else if (strcmp(action, "upload") == 0) {
        uint32_t count = 0U;

        if (!web_xcp_number(root, "count", UINT8_MAX, &count) ||
            (count == 0U)) {

            return ESP_ERR_INVALID_ARG;
        }

        result = xcp_command_encode_upload(
            (uint8_t)count,
            command,
            sizeof(command),
            &command_size
        );
    }

    if (result != ESP_OK) {
        return result;
    }

    result = web_xcp_execute_command(action, command, command_size);

    if (result != ESP_OK) {
        return result;
    }

    xcp_packet_t packet = {0};
    result = xcp_protocol_decode_packet(
        s_response,
        s_response_size,
        &packet
    );

    if (result != ESP_OK) {
        return result;
    }

    xcp_service_session_info_t info = {0};
    result = xcp_service_get_session_info(s_session_id, &info);

    if (result != ESP_OK) {
        return result;
    }

    if (strcmp(action, "get_status") == 0) {
        result = xcp_command_decode_get_status_response(
            &packet,
            info.slave.byte_order_big_endian,
            &s_status
        );
    } else if (strcmp(action, "get_comm_mode_info") == 0) {
        result = xcp_command_decode_get_communication_mode_info_response(
            &packet,
            &s_communication
        );
    } else if (strcmp(action, "get_id") == 0) {
        result = xcp_command_decode_get_id_response(
            &packet,
            info.slave.byte_order_big_endian,
            &s_identification
        );

        if (result == ESP_OK) {
            const size_t requested =
                s_identification.length < WEB_XCP_RESPONSE_MAX_SIZE
                    ? s_identification.length
                    : WEB_XCP_RESPONSE_MAX_SIZE;
            size_t copied = 0U;

            if (s_identification.transfer_mode == 0U) {
                const uint8_t granularity =
                    (uint8_t)(1U << info.slave.address_granularity);
                const size_t maximum_payload =
                    (info.slave.maximum_cto > 1U)
                        ? info.slave.maximum_cto - 1U
                        : 0U;
                const size_t maximum_elements =
                    maximum_payload / granularity;

                if (maximum_elements == 0U) {
                    result = ESP_ERR_INVALID_RESPONSE;
                }

                while ((result == ESP_OK) &&
                       (copied < requested)) {

                    const size_t remaining = requested - copied;
                    size_t elements =
                        (remaining + granularity - 1U) /
                        granularity;

                    if (elements > maximum_elements) {
                        elements = maximum_elements;
                    }

                    if (elements > UINT8_MAX) {
                        elements = UINT8_MAX;
                    }

                    uint8_t upload_command[2] = {0};
                    size_t upload_command_size = 0U;
                    uint8_t upload_response[XCP_CAN_FD_CTO_MAX_SIZE] = {0};
                    size_t upload_response_size = 0U;

                    result = xcp_command_encode_upload(
                        (uint8_t)elements,
                        upload_command,
                        sizeof(upload_command),
                        &upload_command_size
                    );

                    if (result == ESP_OK) {
                        result = xcp_service_execute_cto(
                            s_session_id,
                            upload_command,
                            upload_command_size,
                            upload_response,
                            sizeof(upload_response),
                            &upload_response_size
                        );
                    }

                    xcp_packet_t upload_packet = {0};

                    if (result == ESP_OK) {
                        result = xcp_protocol_decode_packet(
                            upload_response,
                            upload_response_size,
                            &upload_packet
                        );
                    }

                    if ((result == ESP_OK) &&
                        (upload_packet.type != XCP_PACKET_RESPONSE)) {

                        result = ESP_ERR_INVALID_RESPONSE;
                    }

                    if (result == ESP_OK) {
                        size_t copy_size = upload_packet.payload_length;

                        if (copy_size > remaining) {
                            copy_size = remaining;
                        }

                        if (copy_size == 0U) {
                            result = ESP_ERR_INVALID_RESPONSE;
                        } else {
                            memcpy(
                                &s_response[copied],
                                upload_packet.payload,
                                copy_size
                            );
                            copied += copy_size;
                        }
                    }
                }
            } else if ((s_identification.transfer_mode == 1U) &&
                       (packet.payload_length > 7U)) {

                copied = packet.payload_length - 7U;

                if (copied > requested) {
                    copied = requested;
                }

                memcpy(
                    s_response,
                    &packet.payload[7],
                    copied
                );
            }

            s_response_size = copied;

            for (size_t index = 0U; index < copied; ++index) {
                const uint8_t value = s_response[index];
                s_identification_text[index] =
                    ((value >= 0x20U) && (value <= 0x7EU))
                        ? (char)value
                        : '.';
            }

            s_identification_text[copied] = '\0';
        }
    }

    s_last_result = result;
    return result;
}

static esp_err_t web_xcp_get_handler(
    httpd_req_t *request
)
{
    if (xSemaphoreTake(
            s_lock,
            pdMS_TO_TICKS(WEB_XCP_LOCK_TIMEOUT_MS)
        ) != pdTRUE) {

        return web_api_send_message(
            request,
            "503 Service Unavailable",
            false,
            "XCP state is busy"
        );
    }

    xcp_service_session_info_t info = {0};
    const bool open =
        s_session_id != XCP_SERVICE_SESSION_ID_NONE;
    const esp_err_t info_result = open
        ? xcp_service_get_session_info(s_session_id, &info)
        : ESP_OK;
    char *response_text =
        web_xcp_format_hex(s_response, s_response_size);
    cJSON *response = cJSON_CreateObject();

    const bool valid =
        (info_result == ESP_OK) &&
        (response_text != NULL) &&
        (response != NULL) &&
        (cJSON_AddBoolToObject(response, "open", open) != NULL) &&
        (cJSON_AddBoolToObject(
            response,
            "connected",
            open && info.connected
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "session_id",
            s_session_id
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "state",
            open ? info.state : XCP_SERVICE_SESSION_CLOSED
        ) != NULL) &&
        (cJSON_AddNumberToObject(response, "sequence", s_sequence) != NULL) &&
        (cJSON_AddNumberToObject(response, "result", s_last_result) != NULL) &&
        (cJSON_AddStringToObject(response, "operation", s_operation) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "xcp_error",
            open ? info.last_xcp_error : 0U
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "maximum_cto",
            open ? info.slave.maximum_cto : 0U
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "maximum_dto",
            open ? info.slave.maximum_dto : 0U
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "response_length",
            s_response_size
        ) != NULL) &&
        (cJSON_AddStringToObject(
            response,
            "response",
            response_text
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "session_status",
            s_status.session_status
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "resource_protection",
            s_status.resource_protection_status
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "session_configuration_id",
            s_status.session_configuration_id
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "communication_mode_optional",
            s_communication.communication_mode_optional
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "maximum_block_size",
            s_communication.maximum_block_size
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "minimum_separation_time",
            s_communication.minimum_separation_time
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "communication_queue_size",
            s_communication.queue_size
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "driver_version",
            s_communication.driver_version
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "identification_transfer_mode",
            s_identification.transfer_mode
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            response,
            "identification_length",
            s_identification.length
        ) != NULL) &&
        (cJSON_AddStringToObject(
            response,
            "identification_text",
            s_identification_text
        ) != NULL);

    free(response_text);
    xSemaphoreGive(s_lock);

    if (!valid) {
        cJSON_Delete(response);
        return (info_result != ESP_OK)
            ? info_result
            : ESP_ERR_NO_MEM;
    }

    const esp_err_t result = web_api_send_json(request, response);
    cJSON_Delete(response);
    return result;
}

static esp_err_t web_xcp_post_handler(
    httpd_req_t *request
)
{
    cJSON *root = NULL;
    esp_err_t result = web_xcp_receive_json(request, &root);

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

    if (xSemaphoreTake(
            s_lock,
            pdMS_TO_TICKS(WEB_XCP_LOCK_TIMEOUT_MS)
        ) != pdTRUE) {

        cJSON_Delete(root);
        return web_api_send_message(
            request,
            "503 Service Unavailable",
            false,
            "XCP state is busy"
        );
    }

    if (!cJSON_IsString(action)) {
        result = ESP_ERR_INVALID_ARG;
    } else if (strcmp(action->valuestring, "configure") == 0) {
        result = web_xcp_configure(root);
    } else if (strcmp(action->valuestring, "connect") == 0) {
        uint32_t mode = 0U;

        if (s_session_id == XCP_SERVICE_SESSION_ID_NONE) {
            result = ESP_ERR_INVALID_STATE;
        } else if (!web_xcp_optional_number(
                root,
                "mode",
                UINT8_MAX,
                0U,
                &mode
            )) {

            result = ESP_ERR_INVALID_ARG;
        } else {
            result =
                xcp_service_connect(
                    s_session_id,
                    (uint8_t)mode
                );
        }

        s_last_result = result;
        s_sequence++;
    } else if (strcmp(action->valuestring, "execute") == 0) {
        result = web_xcp_execute(root);
    } else if ((strcmp(action->valuestring, "get_status") == 0) ||
               (strcmp(action->valuestring, "get_comm_mode_info") == 0) ||
               (strcmp(action->valuestring, "get_id") == 0) ||
               (strcmp(action->valuestring, "set_mta") == 0) ||
               (strcmp(action->valuestring, "upload") == 0)) {

        result = web_xcp_discovery(root, action->valuestring);
    } else if (strcmp(action->valuestring, "write_memory") == 0) {
        result = web_xcp_write_memory(root);
    } else if (strcmp(action->valuestring, "disconnect") == 0) {
        result = (s_session_id != XCP_SERVICE_SESSION_ID_NONE)
            ? xcp_service_disconnect(s_session_id)
            : ESP_ERR_INVALID_STATE;
        s_last_result = result;
        s_sequence++;
    } else if (strcmp(action->valuestring, "cancel") == 0) {
        result = (s_session_id != XCP_SERVICE_SESSION_ID_NONE)
            ? xcp_service_cancel(s_session_id)
            : ESP_ERR_INVALID_STATE;
    } else if (strcmp(action->valuestring, "close") == 0) {
        result = (s_session_id == XCP_SERVICE_SESSION_ID_NONE)
            ? ESP_OK
            : xcp_service_close_session(s_session_id);

        if (result == ESP_OK) {
            s_session_id = XCP_SERVICE_SESSION_ID_NONE;
            s_response_size = 0U;
            s_last_result = ESP_OK;
            s_sequence++;
        }
    } else {
        result = ESP_ERR_INVALID_ARG;
    }

    xSemaphoreGive(s_lock);
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
            ? "XCP command completed"
            : esp_err_to_name(result)
    );
}

esp_err_t web_xcp_api_register(
    httpd_handle_t server
)
{
    if ((server == NULL) ||
        !xcp_service_is_running()) {

        return ESP_ERR_INVALID_STATE;
    }

    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();

        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    const httpd_uri_t get = {
        .uri = "/api/xcp",
        .method = HTTP_GET,
        .handler = web_xcp_get_handler,
    };
    const httpd_uri_t post = {
        .uri = "/api/xcp",
        .method = HTTP_POST,
        .handler = web_xcp_post_handler,
    };

    esp_err_t result = httpd_register_uri_handler(server, &get);

    if (result == ESP_OK) {
        result = httpd_register_uri_handler(server, &post);
    }

    return result;
}
