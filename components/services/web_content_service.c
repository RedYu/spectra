#include "web_content_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

#include "esp_log.h"

#include "storage_service.h"

static const char *TAG =
    "web_content_service";

typedef struct
{
    httpd_req_t *request;

    size_t bytes_sent;
    bool transfer_started;

} web_content_stream_context_t;

static esp_err_t web_content_stream_callback(
    const void *data,
    size_t size,
    void *context
)
{
    if ((data == NULL) ||
        (size == 0U) ||
        (context == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    web_content_stream_context_t *stream_context =
        context;

    if (stream_context->request == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    stream_context->transfer_started = true;

    errno = 0;

    const esp_err_t result =
        httpd_resp_send_chunk(
            stream_context->request,
            (const char *)data,
            size
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to send content chunk: "
            "socket=%d, size=%u, sent=%u, "
            "errno=%d (%s), error=%s",
            httpd_req_to_sockfd(
                stream_context->request
            ),
            (unsigned int)size,
            (unsigned int)
                stream_context->bytes_sent,
            errno,
            strerror(errno),
            esp_err_to_name(result)
        );

        return result;
    }

    stream_context->bytes_sent += size;

    return ESP_OK;
}

static esp_err_t web_content_send_text_error(
    httpd_req_t *request,
    const char *status,
    const char *message
)
{
    if ((request == NULL) ||
        (status == NULL) ||
        (message == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        httpd_resp_set_status(
            request,
            status
        );

    if (result != ESP_OK) {
        return result;
    }

    result =
        httpd_resp_set_type(
            request,
            "text/plain; charset=utf-8"
        );

    if (result != ESP_OK) {
        return result;
    }

    return httpd_resp_send(
        request,
        message,
        HTTPD_RESP_USE_STRLEN
    );
}

static esp_err_t web_content_send_storage_file(
    httpd_req_t *request,
    const char *path,
    const char *content_type
)
{
    if ((request == NULL) ||
        (path == NULL) ||
        (content_type == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        httpd_resp_set_type(
            request,
            content_type
        );

    if (result != ESP_OK) {
        return result;
    }

    result =
        httpd_resp_set_hdr(
            request,
            "Cache-Control",
            "no-store"
        );

    if (result != ESP_OK) {
        return result;
    }

    /*
     * Prevent browsers from interpreting a resource as another
     * content type.
     */
    result =
        httpd_resp_set_hdr(
            request,
            "X-Content-Type-Options",
            "nosniff"
        );

    if (result != ESP_OK) {
        return result;
    }

    web_content_stream_context_t context = {
        .request = request,
        .bytes_sent = 0U,
        .transfer_started = false,
    };

    result =
        storage_service_stream_file(
            path,
            web_content_stream_callback,
            &context
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to stream web resource '%s' "
            "after %u bytes: %s",
            path,
            (unsigned int)context.bytes_sent,
            esp_err_to_name(result)
        );

        /*
         * A new HTTP error response can only be sent before the first
         * response chunk has been passed to the HTTP server.
         */
        if (context.transfer_started) {
            return result;
        }

        if (result == ESP_ERR_NOT_FOUND) {
            return httpd_resp_send_err(
                request,
                HTTPD_404_NOT_FOUND,
                "Resource not found"
            );
        }

        if (result == ESP_ERR_TIMEOUT) {
            return web_content_send_text_error(
                request,
                "503 Service Unavailable",
                "Internal storage is busy"
            );
        }

        if ((result == ESP_ERR_INVALID_ARG) ||
            (result == ESP_ERR_INVALID_SIZE)) {

            return httpd_resp_send_err(
                request,
                HTTPD_400_BAD_REQUEST,
                "Invalid resource path"
            );
        }

        return httpd_resp_send_err(
            request,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Failed to read web resource"
        );
    }

    /*
     * Complete the chunked response. This also correctly sends an
     * empty file when the stream callback was never invoked.
     */
    return httpd_resp_send_chunk(
        request,
        NULL,
        0U
    );
}

static esp_err_t web_content_root_handler(
    httpd_req_t *request
)
{
    return web_content_send_storage_file(
        request,
        "/storage/www/index.html",
        "text/html; charset=utf-8"
    );
}

static esp_err_t web_content_files_page_handler(
    httpd_req_t *request
)
{
    return web_content_send_storage_file(
        request,
        "/storage/www/files.html",
        "text/html; charset=utf-8"
    );
}

static esp_err_t web_content_files_script_handler(
    httpd_req_t *request
)
{
    return web_content_send_storage_file(
        request,
        "/storage/www/files.js",
        "application/javascript; charset=utf-8"
    );
}

static esp_err_t web_content_favicon_handler(
    httpd_req_t *request
)
{
    return web_content_send_storage_file(
        request,
        "/storage/www/favicon.ico",
        "image/x-icon"
    );
}

esp_err_t web_content_service_register(
    httpd_handle_t server
)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    static const httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler =
            web_content_root_handler,
        .user_ctx = NULL,
    };

    static const httpd_uri_t favicon_uri = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler =
            web_content_favicon_handler,
        .user_ctx = NULL,
    };

    static const httpd_uri_t files_page_uri = {
        .uri = "/files",
        .method = HTTP_GET,
        .handler =
            web_content_files_page_handler,
        .user_ctx = NULL,
    };

    static const httpd_uri_t files_script_uri = {
        .uri = "/files.js",
        .method = HTTP_GET,
        .handler =
            web_content_files_script_handler,
        .user_ctx = NULL,
    };

    esp_err_t result =
        httpd_register_uri_handler(
            server,
            &root_uri
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to register GET /: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        httpd_register_uri_handler(
            server,
            &favicon_uri
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to register GET /favicon.ico: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        httpd_register_uri_handler(
            server,
            &files_page_uri
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to register GET /files: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        httpd_register_uri_handler(
            server,
            &files_script_uri
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to register GET /files.js: %s",
            esp_err_to_name(result)
        );
    }

    return result;
}
