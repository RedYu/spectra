#include "web_api_common.h"

#include <stddef.h>

esp_err_t web_api_send_json(
    httpd_req_t *request,
    const cJSON *response
)
{
    if ((request == NULL) ||
        (response == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    char *json =
        cJSON_PrintUnformatted(
            response
        );

    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    httpd_resp_set_type(
        request,
        "application/json"
    );

    httpd_resp_set_hdr(
        request,
        "Cache-Control",
        "no-store"
    );

    const esp_err_t result =
        httpd_resp_send(
            request,
            json,
            HTTPD_RESP_USE_STRLEN
        );

    cJSON_free(json);

    return result;
}

esp_err_t web_api_send_message(
    httpd_req_t *request,
    const char *status,
    bool success,
    const char *message
)
{
    if ((request == NULL) ||
        (status == NULL) ||
        (message == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    cJSON *response =
        cJSON_CreateObject();

    if (response == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const bool valid =
        (cJSON_AddBoolToObject(
            response,
            "success",
            success
        ) != NULL) &&
        (cJSON_AddStringToObject(
            response,
            "message",
            message
        ) != NULL);

    if (!valid) {
        cJSON_Delete(response);
        return ESP_ERR_NO_MEM;
    }

    httpd_resp_set_status(
        request,
        status
    );

    const esp_err_t result =
        web_api_send_json(
            request,
            response
        );

    cJSON_Delete(response);

    return result;
}
