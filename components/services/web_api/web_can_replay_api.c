/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "web_can_replay_api.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "can_replay_service.h"
#include "web_api_common.h"

#define WEB_CAN_REPLAY_BODY_MAX_SIZE (2048U)

static bool web_can_replay_number(
    const cJSON *root,
    const char *name,
    uint32_t maximum,
    uint32_t fallback,
    uint32_t *value
)
{
    const cJSON *item =
        cJSON_GetObjectItemCaseSensitive(root, name);

    if (item == NULL) {
        *value = fallback;
        return true;
    }

    if (!cJSON_IsNumber(item) ||
        (item->valuedouble < 0.0) ||
        (item->valuedouble > (double)maximum) ||
        (item->valuedouble !=
         (double)(uint32_t)item->valuedouble)) {

        return false;
    }

    *value = (uint32_t)item->valuedouble;
    return true;
}

static bool web_can_replay_bool(
    const cJSON *root,
    const char *name,
    bool fallback
)
{
    const cJSON *item =
        cJSON_GetObjectItemCaseSensitive(root, name);

    return cJSON_IsBool(item)
        ? cJSON_IsTrue(item)
        : fallback;
}

static esp_err_t web_can_replay_get(
    httpd_req_t *request
)
{
    can_replay_info_t info;
    const esp_err_t result =
        can_replay_service_get_info(&info);

    if (result != ESP_OK) {
        return web_api_send_message(
            request,
            "503 Service Unavailable",
            false,
            esp_err_to_name(result)
        );
    }

    cJSON *response = cJSON_CreateObject();
    const bool valid =
        (response != NULL) &&
        (cJSON_AddNumberToObject(response, "state", info.state) != NULL) &&
        (cJSON_AddStringToObject(response, "path", info.config.path) != NULL) &&
        (cJSON_AddNumberToObject(response, "file_size", (double)info.file_size) != NULL) &&
        (cJSON_AddNumberToObject(response, "file_position", (double)info.file_position) != NULL) &&
        (cJSON_AddNumberToObject(response, "replay_time_us", (double)info.replay_time_us) != NULL) &&
        (cJSON_AddNumberToObject(response, "elapsed_us", (double)info.elapsed_us) != NULL) &&
        (cJSON_AddNumberToObject(response, "current_lag_us", (double)info.current_lag_us) != NULL) &&
        (cJSON_AddNumberToObject(response, "maximum_lag_us", (double)info.maximum_lag_us) != NULL) &&
        (cJSON_AddNumberToObject(response, "repeat", info.current_repeat) != NULL) &&
        (cJSON_AddNumberToObject(response, "records", info.records_read) != NULL) &&
        (cJSON_AddNumberToObject(response, "selected", info.frames_selected) != NULL) &&
        (cJSON_AddNumberToObject(response, "submitted", info.frames_submitted) != NULL) &&
        (cJSON_AddNumberToObject(response, "completed", info.frames_completed) != NULL) &&
        (cJSON_AddNumberToObject(response, "failed", info.frames_failed) != NULL) &&
        (cJSON_AddNumberToObject(response, "dropped", info.frames_dropped) != NULL) &&
        (cJSON_AddNumberToObject(response, "skipped", info.frames_skipped) != NULL) &&
        (cJSON_AddNumberToObject(response, "pending_transaction", info.pending_transaction) != NULL) &&
        (cJSON_AddNumberToObject(response, "result", info.last_error) != NULL);

    if (!valid) {
        cJSON_Delete(response);
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t send_result =
        web_api_send_json(request, response);
    cJSON_Delete(response);
    return send_result;
}

static esp_err_t web_can_replay_post(
    httpd_req_t *request
)
{
    if ((request == NULL) ||
        (request->content_len <= 0) ||
        (request->content_len > WEB_CAN_REPLAY_BODY_MAX_SIZE)) {

        return web_api_send_message(
            request,
            "400 Bad Request",
            false,
            "Invalid replay request"
        );
    }

    char *body = calloc(1U, (size_t)request->content_len + 1U);

    if (body == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t received = 0U;

    while (received < (size_t)request->content_len) {
        const int result = httpd_req_recv(
            request,
            body + received,
            (size_t)request->content_len - received
        );

        if (result <= 0) {
            free(body);
            return ESP_FAIL;
        }

        received += (size_t)result;
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    const cJSON *action = (root != NULL)
        ? cJSON_GetObjectItemCaseSensitive(root, "action")
        : NULL;
    esp_err_t result = ESP_ERR_INVALID_ARG;

    if (cJSON_IsString(action) &&
        (strcmp(action->valuestring, "pause") == 0)) {
        result = can_replay_service_pause();
    } else if (cJSON_IsString(action) &&
               (strcmp(action->valuestring, "resume") == 0)) {
        result = can_replay_service_resume();
    } else if (cJSON_IsString(action) &&
               (strcmp(action->valuestring, "stop") == 0)) {
        result = can_replay_service_stop();
    } else if (cJSON_IsString(action) &&
               (strcmp(action->valuestring, "start") == 0)) {
        const cJSON *path =
            cJSON_GetObjectItemCaseSensitive(root, "path");
        uint32_t target_bus = 0U;
        uint32_t speed_numerator = 1U;
        uint32_t speed_denominator = 1U;
        uint32_t repeat_count = 1U;
        uint32_t start_delay_ms = 0U;
        uint32_t maximum_lag_ms = 100U;
        uint32_t late_policy = CAN_REPLAY_LATE_WAIT;
        uint32_t identifier_min = 0U;
        uint32_t identifier_max = 0x1FFFFFFFU;
        uint32_t time_start_ms = 0U;
        uint32_t time_end_ms = UINT32_MAX;

        if (cJSON_IsString(path) &&
            (strlen(path->valuestring) < CAN_REPLAY_PATH_MAX_LENGTH) &&
            web_can_replay_number(root, "target_bus", 1U, 0U, &target_bus) &&
            web_can_replay_number(root, "speed_numerator", 1000U, 1U, &speed_numerator) &&
            web_can_replay_number(root, "speed_denominator", 1000U, 1U, &speed_denominator) &&
            web_can_replay_number(root, "repeat_count", UINT16_MAX, 1U, &repeat_count) &&
            web_can_replay_number(root, "start_delay_ms", 600000U, 0U, &start_delay_ms) &&
            web_can_replay_number(root, "maximum_lag_ms", 60000U, 100U, &maximum_lag_ms) &&
            web_can_replay_number(root, "late_policy", CAN_REPLAY_LATE_STOP, CAN_REPLAY_LATE_WAIT, &late_policy) &&
            web_can_replay_number(root, "identifier_min", 0x1FFFFFFFU, 0U, &identifier_min) &&
            web_can_replay_number(root, "identifier_max", 0x1FFFFFFFU, 0x1FFFFFFFU, &identifier_max) &&
            web_can_replay_number(root, "time_start_ms", UINT32_MAX, 0U, &time_start_ms) &&
            web_can_replay_number(root, "time_end_ms", UINT32_MAX, UINT32_MAX, &time_end_ms)) {

            can_replay_config_t config = {
                .primary_enabled = web_can_replay_bool(root, "primary", true),
                .secondary_enabled = web_can_replay_bool(root, "secondary", true),
                .replay_rx_events = web_can_replay_bool(root, "rx", true),
                .replay_tx_events = web_can_replay_bool(root, "tx", false),
                .preserve_bus = web_can_replay_bool(root, "preserve_bus", true),
                .target_bus = (can_bus_id_t)target_bus,
                .identifier_filter_enabled = web_can_replay_bool(root, "identifier_filter", false),
                .identifier_min = identifier_min,
                .identifier_max = identifier_max,
                .time_range_enabled = web_can_replay_bool(root, "time_range", false),
                .time_start_us = (uint64_t)time_start_ms * 1000ULL,
                .time_end_us = (uint64_t)time_end_ms * 1000ULL,
                .maximum_speed = web_can_replay_bool(root, "maximum_speed", false),
                .speed_numerator = speed_numerator,
                .speed_denominator = speed_denominator,
                .repeat_count = repeat_count,
                .start_delay_ms = start_delay_ms,
                .maximum_lag_ms = maximum_lag_ms,
                .late_policy = (can_replay_late_policy_t)late_policy,
                .skip_remote_frames = web_can_replay_bool(root, "skip_remote", false),
                .stop_on_error = web_can_replay_bool(root, "stop_on_error", true),
            };
            strcpy(config.path, path->valuestring);
            result = can_replay_service_start(&config);
        }
    }

    cJSON_Delete(root);
    return web_api_send_message(
        request,
        result == ESP_OK ? "200 OK" : "409 Conflict",
        result == ESP_OK,
        result == ESP_OK
            ? "Replay command accepted"
            : esp_err_to_name(result)
    );
}

esp_err_t web_can_replay_api_register(
    httpd_handle_t server
)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const httpd_uri_t get = {
        .uri = "/api/can/replay",
        .method = HTTP_GET,
        .handler = web_can_replay_get,
    };
    const httpd_uri_t post = {
        .uri = "/api/can/replay",
        .method = HTTP_POST,
        .handler = web_can_replay_post,
    };
    esp_err_t result = httpd_register_uri_handler(server, &get);

    if (result == ESP_OK) {
        result = httpd_register_uri_handler(server, &post);
    }

    return result;
}
