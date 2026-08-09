#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register system-information API handlers.
 *
 * Registers the GET /api/system endpoint.
 *
 * @param[in] server HTTP server handle.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if server is NULL,
 * otherwise an ESP HTTP server error code.
 */
esp_err_t web_system_api_register(
    httpd_handle_t server
);

#ifdef __cplusplus
}
#endif
