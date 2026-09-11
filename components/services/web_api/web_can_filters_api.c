/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "web_can_filters_api.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "web_api_common.h"
#include "can_service.h"
#include "can_fd_service.h"
#include "soc/soc_caps.h"

#define WEB_CAN_FILTER_BODY_LIMIT  (4096U)

static bool web_can_filter_number(
    const cJSON *object,
    const char *name,
    uint32_t maximum,
    uint32_t *value
)
{
    const cJSON *item =
        cJSON_GetObjectItemCaseSensitive(
            object,
            name
        );

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

static bool web_can_filter_append(
    cJSON *array,
    const can_fd_mcp2518fd_filter_t *filter
)
{
    cJSON *item =
        cJSON_CreateObject();

    if (item == NULL) {
        return false;
    }

    const bool valid =
        cJSON_AddNumberToObject(
            item,
            "index",
            filter->index
        ) &&
        cJSON_AddNumberToObject(
            item,
            "id",
            filter->identifier
        ) &&
        cJSON_AddNumberToObject(
            item,
            "mask",
            filter->mask
        ) &&
        cJSON_AddBoolToObject(
            item,
            "extended",
            filter->extended
        );

    if (!valid ||
        !cJSON_AddItemToArray(
            array,
            item
        )) {

        cJSON_Delete(
            item
        );

        return false;
    }

    return true;
}

static esp_err_t web_can_filters_get(
    httpd_req_t *request
)
{
    cJSON *root =
        cJSON_CreateObject();

    cJSON *buses =
        (root != NULL)
            ? cJSON_AddArrayToObject(
                root,
                "buses"
            )
            : NULL;

    can_fd_mcp2518fd_filter_bank_t *bank =
        calloc(
            1U,
            sizeof(*bank)
        );

    bool valid =
        (buses != NULL) &&
        (bank != NULL);

    for (uint32_t bus = 0U;
         valid && (bus < 2U);
         ++bus) {

        memset(
            bank,
            0,
            sizeof(*bank)
        );

        esp_err_t result;

        if (bus == 0U) {
            can_twai_acceptance_filter_t filter;
            result =
                can_service_get_acceptance_filter(
                    &filter
                );

            if (result == ESP_OK) {
                bank->accept_all =
                    (filter.mask == 0U) && (filter.identifier == 0U);
                bank->count =
                    bank->accept_all
                        ? 0U
                        : 1U;
                bank->filters[0].identifier = filter.identifier;
                bank->filters[0].mask = filter.mask;
                bank->filters[0].extended = filter.extended;
            }
        } else {
            result =
                can_fd_service_get_filter_bank(
                    bank
                );
        }

        cJSON *item =
            cJSON_CreateObject();

        if ((item == NULL) ||
            !cJSON_AddItemToArray(
                buses,
                item
            )) {

            cJSON_Delete(
                item
            );

            valid = false;
            break;
        }

        cJSON *filters =
            cJSON_AddArrayToObject(
                item,
                "filters"
            );

        valid =
            (filters != NULL) &&
                cJSON_AddNumberToObject(
                    item,
                    "bus",
                    bus
                ) &&
                cJSON_AddNumberToObject(
                    item,
                    "capacity",
                    (bus == 0U)
                        ? SOC_TWAI_MASK_FILTER_NUM
                        : CAN_FD_MCP2518FD_FILTER_COUNT
                ) &&
                cJSON_AddBoolToObject(
                    item,
                    "available",
                    result == ESP_OK
                ) &&
                cJSON_AddBoolToObject(
                    item,
                    "accept_all",
                    bank->accept_all
                ) &&
                cJSON_AddBoolToObject(
                    item,
                    "reject_all_supported",
                    bus == 1U
                ) &&
                cJSON_AddBoolToObject(
                    item,
                    "persistent",
                    false
                ) &&
                cJSON_AddStringToObject(
                    item,
                    "error",
                    esp_err_to_name(result)
                );

        for (size_t index = 0U;
             valid && (result == ESP_OK) && (index < bank->count);
             ++index) {
            valid =
                web_can_filter_append(
                    filters,
                    &bank->filters[index]
                );
        }
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

    free(
        bank
    );

    cJSON_Delete(
        root
    );

    return result;
}

static esp_err_t web_can_filters_apply(
    const cJSON *root
)
{
    uint32_t bus = 0U;

    const cJSON *all =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "accept_all"
        );

    const cJSON *filters =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "filters"
        );

    if (!cJSON_IsObject(root) ||
        !web_can_filter_number(
            root,
            "bus",
            1U,
            &bus
        ) ||
        !cJSON_IsBool(all) ||
        !cJSON_IsArray(filters)) {

        return ESP_ERR_INVALID_ARG;
    }

    const size_t count =
        (size_t)cJSON_GetArraySize(
            filters
        );

    const size_t capacity =
        (bus == 0U)
            ? SOC_TWAI_MASK_FILTER_NUM
            : CAN_FD_MCP2518FD_FILTER_COUNT;

    const bool accept_all =
        cJSON_IsTrue(
            all
        );

    if ((count > capacity) || (accept_all && (count != 0U)) ||
        (!accept_all && (bus == 0U) && (count != 1U))) {

        return ESP_ERR_INVALID_ARG;
    }

    can_fd_mcp2518fd_filter_bank_t *bank =
        calloc(
            1U,
            sizeof(*bank)
        );

    if (bank == NULL) {
        return ESP_ERR_NO_MEM;
    }

    bank->accept_all = accept_all;
    bank->count = count;

    esp_err_t result = ESP_OK;

    for (size_t index = 0U; index < count; ++index) {
        const cJSON *item =
            cJSON_GetArrayItem(
                filters,
                (int)index
            );

        const cJSON *extended =
            cJSON_GetObjectItemCaseSensitive(
                item,
                "extended"
            );

        can_fd_mcp2518fd_filter_t *filter =
            &bank->filters[index];

        filter->index = (uint8_t)index;

        filter->extended =
            cJSON_IsTrue(
                extended
            );

        const uint32_t maximum =
            filter->extended
                ? 0x1FFFFFFFU
                : 0x7FFU;

        if (!cJSON_IsBool(extended) ||
            !web_can_filter_number(
                item,
                "id",
                maximum,
                &filter->identifier
            ) ||
            !web_can_filter_number(
                item,
                "mask",
                maximum,
                &filter->mask
            )) {

            result = ESP_ERR_INVALID_ARG;
            break;
        }
    }

    if (result == ESP_OK) {
        if (bus == 0U) {
            const can_twai_acceptance_filter_t filter = {
                .identifier = bank->filters[0].identifier,
                .mask = bank->filters[0].mask,
                .extended = bank->filters[0].extended,
            };

            result =
                can_service_set_acceptance_filter(
                    &filter
                );
        } else {
            result =
                can_fd_service_set_filter_bank(
                    bank
                );
        }
    }

    free(
        bank
    );

    return result;
}

static esp_err_t web_can_filters_post(
    httpd_req_t *request
)
{
    char content_type[64];

    if ((httpd_req_get_hdr_value_str(
             request,
             "Content-Type",
             content_type,
             sizeof(content_type)
         ) != ESP_OK) ||
        ((strcmp(content_type, "application/json") != 0) &&
         (strcmp(content_type, "application/json; charset=utf-8") != 0)
        )) {

        return web_api_send_message(
            request,
            "415 Unsupported Media Type",
            false,
            "Use application/json"
        );
    }

    if ((request->content_len == 0U) ||
        (request->content_len > WEB_CAN_FILTER_BODY_LIMIT)) {

        return web_api_send_message(
            request,
            "413 Payload Too Large",
            false,
            "Body must be 1..4096 bytes"
        );
    }

    char *body =
        malloc(
            request->content_len + 1U
        );

    if (body == NULL) {
        return web_api_send_message(
            request,
            "500 Internal Server Error",
            false,
            "Out of memory"
        );
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

    const esp_err_t result =
        web_can_filters_apply(
            root
        );

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
            ? "Hardware RX filters applied"
            : esp_err_to_name(result)
    );
}

esp_err_t web_can_filters_api_register(
    httpd_handle_t server
)
{
    const httpd_uri_t get = {
        .uri = "/api/can/filters",
        .method = HTTP_GET,
        .handler = web_can_filters_get,
    };
    const httpd_uri_t post = {
        .uri = "/api/can/filters",
        .method = HTTP_POST,
        .handler = web_can_filters_post,
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
