#pragma once

#include <stdbool.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Send an existing JSON object.
 *
 * The function serializes the object without formatting, configures
 * the response as non-cacheable JSON and sends it to the client.
 * Ownership of the JSON object remains with the caller.
 *
 * @param[in] request HTTP request.
 * @param[in] response JSON object to send.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid
 * arguments, ESP_ERR_NO_MEM if serialization fails, otherwise an
 * ESP HTTP server error code.
 */
esp_err_t web_api_send_json(
    httpd_req_t *request,
    const cJSON *response
);

/**
 * @brief Send a standard JSON status message.
 *
 * The generated response contains the fields "success" and "message".
 *
 * @param[in] request HTTP request.
 * @param[in] status HTTP status string.
 * @param[in] success Operation result.
 * @param[in] message Human-readable response message.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid
 * arguments, ESP_ERR_NO_MEM if JSON creation fails, otherwise an
 * ESP HTTP server error code.
 */
esp_err_t web_api_send_message(
    httpd_req_t *request,
    const char *status,
    bool success,
    const char *message
);

#ifdef __cplusplus
}
#endif
