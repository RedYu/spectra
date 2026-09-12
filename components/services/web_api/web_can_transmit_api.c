/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "web_can_transmit_api.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "web_api_common.h"
#include "can_transmit_service.h"

static bool web_can_transmit_number(
    const cJSON *root,
    const char *key,
    uint32_t fallback,
    uint32_t maximum,
    uint32_t *value
)
{
    const cJSON *item =
        cJSON_GetObjectItemCaseSensitive(
            root,
            key
        );

    if (item == NULL) {
        *value = fallback;

        return true;
    }

    if (!cJSON_IsNumber(item) ||
        !isfinite(item->valuedouble) ||
        (item->valuedouble < 0.0) ||
        (item->valuedouble > maximum) ||
        (floor(item->valuedouble) != item->valuedouble)) {

        return false;
    }

    *value = (uint32_t)item->valuedouble;
    return true;
}

static bool web_can_transmit_boolean(
    const cJSON *root,
    const char *key,
    bool *value
)
{
    const cJSON *item =
        cJSON_GetObjectItemCaseSensitive(
            root,
            key
        );

    if ((item != NULL) &&
        !cJSON_IsBool(item)) {

        return false;
    }

    *value =
        cJSON_IsTrue(
            item
        );

    return true;
}

static bool web_can_transmit_parse_job(
    const cJSON *root,
    can_transmit_job_config_t *config
)
{
    uint32_t bus;
    uint32_t id;
    uint32_t dlc;
    uint32_t dlc_end;
    uint32_t offset;
    uint32_t width;

    bool extended;
    bool fd;
    bool brs;

    if (!cJSON_HasObjectItem(
            root,
            "bus"
        ) ||
        !cJSON_HasObjectItem(
            root,
            "id"
        ) ||
        !cJSON_HasObjectItem(
            root,
            "dlc"
        ) ||
        !cJSON_HasObjectItem(
            root,
            "count"
        ) ||
        !web_can_transmit_number(
            root,
            "bus",
            0U,
            1U,
            &bus
        ) ||
        !web_can_transmit_number(
            root,
            "id",
            0U,
            CAN_FRAME_EXTENDED_ID_MAX,
            &id
        ) ||
        !web_can_transmit_number(
            root,
            "dlc",
            0U,
            15U,
            &dlc
        ) ||
        !web_can_transmit_boolean(
            root,
            "extended",
            &extended
        ) ||
        !web_can_transmit_boolean(
            root,
            "fd",
            &fd
        ) ||
        !web_can_transmit_boolean(
            root,
            "brs",
            &brs
        ) ||
        !web_can_transmit_number(
            root,
            "interval_ms",
            100U,
            3600000U,
            &config->interval_ms
        ) ||
        !web_can_transmit_number(
            root,
            "count",
            1U,
            1000000U,
            &config->count
        ) ||
        !web_can_transmit_number(
            root,
            "id_step",
            0U,
            CAN_FRAME_EXTENDED_ID_MAX,
            &config->id_step
        ) ||
        !web_can_transmit_number(
            root,
            "id_end",
            id,
            CAN_FRAME_EXTENDED_ID_MAX,
            &config->id_end
        ) ||
        !web_can_transmit_boolean(
            root,
            "increment_dlc",
            &config->increment_dlc
        ) ||
        !web_can_transmit_number(
            root,
            "dlc_end",
            dlc,
            15U,
            &dlc_end
        ) ||
        !web_can_transmit_number(
            root,
            "data_offset",
            0U,
            63U,
            &offset
        ) ||
        !web_can_transmit_number(
            root,
            "data_width",
            0U,
            8U,
            &width
        ) ||
        !web_can_transmit_number(
            root,
            "data_step",
            1U,
            UINT32_MAX,
            &config->data_step
        ) ||
        !web_can_transmit_boolean(
            root,
            "data_big_endian",
            &config->data_big_endian
        ) ||
        !web_can_transmit_boolean(
            root,
            "increment_data_bytes",
            &config->increment_data_bytes
        )) {

        return false;
    }

    config->frame.bus = (can_bus_id_t)bus;
    config->frame.identifier = id;
    config->frame.dlc = dlc;
    config->frame.flags =
        (extended ? CAN_FRAME_FLAG_EXTENDED_ID : 0U) |
        (fd ? CAN_FRAME_FLAG_FD : 0U) |
        (brs ? CAN_FRAME_FLAG_BRS : 0U);
    config->dlc_end = dlc_end;
    config->data_offset = offset;
    config->data_width = width;

    if (can_frame_dlc_to_length(
            dlc,
            fd,
            &config->frame.data_length
        ) != ESP_OK) {

        return false;
    }

    const cJSON *data =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "data"
        );

    if (!cJSON_IsArray(data) ||
        (cJSON_GetArraySize(data) != config->frame.data_length)) {

        return false;
    }

    for (uint8_t i = 0U; i < config->frame.data_length; ++i) {
        const cJSON *byte =
            cJSON_GetArrayItem(
                data,
                i
            );

        if (!cJSON_IsNumber(byte) ||
            !isfinite(byte->valuedouble) ||
            (byte->valuedouble < 0.0) ||
            (byte->valuedouble > UINT8_MAX) ||
            (floor(byte->valuedouble) != byte->valuedouble)) {

            return false;
        }

        config->frame.data[i] = (uint8_t)byte->valuedouble;
    }

    if (config->increment_data_bytes) {
        const cJSON *mask =
            cJSON_GetObjectItemCaseSensitive(
                root,
                "data_byte_mask"
            );

        if (!cJSON_IsArray(mask) ||
            (cJSON_GetArraySize(mask) !=
             config->frame.data_length)) {

            return false;
        }

        for (uint8_t i = 0U;
             i < config->frame.data_length;
             ++i) {

            const cJSON *byte_mask =
                cJSON_GetArrayItem(
                    mask,
                    i
                );

            if (!cJSON_IsNumber(byte_mask) ||
                !isfinite(byte_mask->valuedouble) ||
                (byte_mask->valuedouble < 0.0) ||
                (byte_mask->valuedouble > UINT8_MAX) ||
                (floor(byte_mask->valuedouble) !=
                 byte_mask->valuedouble)) {

                return false;
            }

            config->data_byte_masks[i] =
                (uint8_t)byte_mask->valuedouble;
        }
    }

    return
        can_transmit_job_validate(
            config
        ) == ESP_OK;
}

static esp_err_t web_can_transmit_get(
    httpd_req_t *request
)
{
    static const char *states[] = {
        "idle",
        "active",
        "stopped",
        "complete",
        "error",
        "unknown",
    };

    cJSON *root =
        cJSON_CreateObject();

    cJSON *jobs =
        (root != NULL)
            ? cJSON_AddArrayToObject(
                root,
                "jobs"
            )
            : NULL;

    bool valid =
        jobs != NULL;

    for (uint32_t slot = 0U;
         valid && (slot < CAN_TRANSMIT_JOB_COUNT);
         ++slot) {

        can_transmit_job_info_t info;

        const esp_err_t result =
            can_transmit_job_get(
                slot,
                &info
            );

        if (result != ESP_OK) {
            cJSON_Delete(
                root
            );

            return web_api_send_message(
                request,
                "503 Service Unavailable",
                false,
                esp_err_to_name(result)
            );
        }
        cJSON *item =
            cJSON_CreateObject();

        if (item == NULL) {
            valid = false;
            break;
        }

        if (!cJSON_AddItemToArray(
                jobs,
                item
            )) {

            cJSON_Delete(
                item
            );

            valid = false;
            break;
        }
        valid =
            cJSON_AddNumberToObject(
                item,
                "slot",
                slot
            ) &&
            cJSON_AddStringToObject(
                item,
                "state",
                states[info.state]
            ) &&
            cJSON_AddNumberToObject(
                item,
                "bus",
                info.config.frame.bus
            ) &&
            cJSON_AddNumberToObject(
                item,
                "next_id",
                info.next_frame.identifier
            ) &&
            cJSON_AddNumberToObject(
                item,
                "next_dlc",
                info.next_frame.dlc
            ) &&
            cJSON_AddNumberToObject(
                item,
                "count",
                info.config.count
            ) &&
            cJSON_AddNumberToObject(
                item,
                "interval_ms",
                info.config.interval_ms
            ) &&
            cJSON_AddNumberToObject(
                item,
                "attempts",
                info.attempts
            ) &&
            cJSON_AddNumberToObject(
                item,
                "queued",
                info.queued
            ) &&
            cJSON_AddNumberToObject(
                item,
                "completed",
                info.completed
            ) &&
            cJSON_AddNumberToObject(
                item,
                "failed",
                info.failed
            ) &&
            cJSON_AddNumberToObject(
                item,
                "aborted",
                info.aborted
            ) &&
            cJSON_AddNumberToObject(
                item,
                "unknown",
                info.unknown
            ) &&
            cJSON_AddNumberToObject(
                item,
                "pending",
                info.pending_transaction
            ) &&
            cJSON_AddStringToObject(
                item,
                "last_error",
                esp_err_to_name(info.last_error)
            );
    }

    const esp_err_t result =
        valid
            ? web_api_send_json(
                request,
                root
            )
            : web_api_send_message(
                request,
                "500 Internal Server Error",
                false,
                "Out of memory"
            );

    cJSON_Delete(
        root
    );

    return result;
}

static esp_err_t web_can_transmit_post(
    httpd_req_t *request
)
{
    char content_type[64];

    if (httpd_req_get_hdr_value_str(
            request,
            "Content-Type",
            content_type,
            sizeof(content_type)
        ) != ESP_OK ||
        ((strcmp(content_type, "application/json") != 0) &&
         (strcmp(
             content_type,
             "application/json; charset=utf-8"
          ) != 0))) {

        return web_api_send_message(
            request,
            "415 Unsupported Media Type",
            false,
            "Use application/json"
        );
    }

    if ((request->content_len == 0U) ||
        (request->content_len > 2048U)) {

        return web_api_send_message(
            request,
            "413 Payload Too Large",
            false,
            "Body must be 1..2048 bytes"
        );
    }

    char *body =
        malloc(
            request->content_len + 1U
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
            free(
                body
            );

            return ESP_FAIL;
        }

        received += (size_t)count;
    }

    body[received] = '\0';

    cJSON *root =
        (memchr(
            body,
            '\0',
            received
        ) == NULL)
            ? cJSON_ParseWithLengthOpts(
                body,
                received + 1U,
                NULL,
                true
            )
            : NULL;

    free(
        body
    );

    const cJSON *action =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "action"
        );

    uint32_t slot = 0U;
    esp_err_t result = ESP_ERR_INVALID_ARG;

    if (cJSON_IsObject(root) &&
        cJSON_IsString(action)) {

        if (strcmp(
                action->valuestring,
                "stop_all"
            ) == 0) {

            result = ESP_OK;

            for (uint32_t i = 0U;
                 i < CAN_TRANSMIT_JOB_COUNT;
                 ++i) {

                const esp_err_t stopped =
                    can_transmit_job_stop(
                        i
                    );

                if (stopped != ESP_OK) {
                    result = stopped;
                }
            }
        } else if (cJSON_HasObjectItem(
                       root,
                       "slot"
                   ) &&
                   web_can_transmit_number(
                       root,
                       "slot",
                       0U,
                       CAN_TRANSMIT_JOB_COUNT - 1U,
                       &slot
                   )) {
            if (strcmp(
                    action->valuestring,
                    "stop"
                ) == 0) {

                result =
                    can_transmit_job_stop(
                        slot
                    );
            } else if (strcmp(
                           action->valuestring,
                           "start"
                       ) == 0) {

                can_transmit_job_config_t config = {0};

                if (web_can_transmit_parse_job(
                        root,
                        &config
                    )) {

                    result =
                        can_transmit_job_start(
                            slot,
                            &config
                        );
                }
            }
        }
    }

    cJSON_Delete(
        root
    );

    return web_api_send_message(
        request,
        result == ESP_OK
            ? "200 OK"
            : (result == ESP_ERR_INVALID_ARG ? "400 Bad Request"
                                             : "409 Conflict"),
        result == ESP_OK,
        (result == ESP_OK)
            ? "Command accepted; poll job status for "
              "transmission results"
            : esp_err_to_name(result)
    );
}

esp_err_t web_can_transmit_api_register(
    httpd_handle_t server
)
{
    const httpd_uri_t get = {
        .uri = "/api/can/transmit",
        .method = HTTP_GET,
        .handler = web_can_transmit_get,
    };
    const httpd_uri_t post = {
        .uri = "/api/can/transmit",
        .method = HTTP_POST,
        .handler = web_can_transmit_post,
    };

    esp_err_t result =
        httpd_register_uri_handler(
            server,
            &get
        );

    if (result == ESP_OK) {
        result =
            httpd_register_uri_handler(
                server,
                &post
            );
    }

    return result;
}
