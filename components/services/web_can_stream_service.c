#include "web_can_stream_service.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "cJSON.h"

#include "can_frame.h"
#include "can_router.h"
#include "web_can_protocol.h"

#define WEB_CAN_STREAM_URI                 "/ws/can"

#define WEB_CAN_STREAM_TASK_STACK_SIZE     (4096U)
#define WEB_CAN_STREAM_TASK_PRIORITY       (4U)

#define WEB_CAN_STREAM_BATCH_MAX_EVENTS    (64U)
#define WEB_CAN_STREAM_BATCH_MAX_SIZE      (4096U)
#define WEB_CAN_STREAM_BATCH_TIMEOUT_MS    (10U)

#define WEB_CAN_STREAM_IDLE_WAIT_MS        (100U)
#define WEB_CAN_STREAM_STOP_TIMEOUT_MS     (2000U)
#define WEB_CAN_STREAM_CALLBACK_WAIT_MS    (100U)

#define WEB_CAN_STREAM_RX_BUFFER_SIZE      (256U)
#define WEB_CAN_STREAM_STATISTICS_SIZE     (512U)

static const char *TAG =
    "web_can_stream";

static httpd_handle_t s_server = NULL;

static QueueHandle_t s_event_queue = NULL;
static StaticQueue_t s_event_queue_control;
static uint8_t *s_event_queue_storage = NULL;

static uint8_t *s_batch_buffer = NULL;

static TaskHandle_t s_stream_task = NULL;
static SemaphoreHandle_t s_task_stopped = NULL;

static uint32_t s_router_subscription_id =
    CAN_ROUTER_SUBSCRIPTION_ID_NONE;

static web_can_stream_service_config_t s_config;

static atomic_bool s_running =
    ATOMIC_VAR_INIT(false);

static atomic_bool s_accepting_events =
    ATOMIC_VAR_INIT(false);

static atomic_bool s_stop_requested =
    ATOMIC_VAR_INIT(false);

static atomic_uint s_active_callbacks =
    ATOMIC_VAR_INIT(0U);

static atomic_int s_client_socket =
    ATOMIC_VAR_INIT(WEB_CAN_STREAM_CLIENT_NONE);

static atomic_uint_fast64_t s_queued_events =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast64_t s_sent_events =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast64_t s_dropped_events =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast64_t s_send_failures =
    ATOMIC_VAR_INIT(0U);

static atomic_uint s_queue_peak =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast64_t s_sent_batches =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast64_t s_sent_binary_bytes =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast64_t s_batch_events_total =
    ATOMIC_VAR_INIT(0U);

static atomic_uint s_batch_events_peak =
    ATOMIC_VAR_INIT(0U);

static atomic_bool s_primary_enabled =
    ATOMIC_VAR_INIT(true);

static atomic_bool s_secondary_enabled =
    ATOMIC_VAR_INIT(true);

static atomic_bool s_rx_enabled =
    ATOMIC_VAR_INIT(true);

static atomic_bool s_tx_enabled =
    ATOMIC_VAR_INIT(true);

static atomic_bool s_paused =
    ATOMIC_VAR_INIT(false);

static atomic_uint_fast64_t s_filtered_events =
    ATOMIC_VAR_INIT(0U);

static void web_can_stream_update_queue_peak(void)
{
    if (s_event_queue == NULL) {
        return;
    }

    const uint32_t current =
        (uint32_t)uxQueueMessagesWaiting(
            s_event_queue
        );

    uint32_t peak =
        atomic_load(
            &s_queue_peak
        );

    while ((current > peak) &&
           !atomic_compare_exchange_weak(
               &s_queue_peak,
               &peak,
               current
           )) {
    }
}

static void web_can_stream_update_batch_peak(
    uint32_t event_count
)
{
    uint32_t peak =
        atomic_load_explicit(
            &s_batch_events_peak,
            memory_order_relaxed
        );

    while ((event_count > peak) &&
           !atomic_compare_exchange_weak_explicit(
               &s_batch_events_peak,
               &peak,
               event_count,
               memory_order_relaxed,
               memory_order_relaxed
           )) {
    }
}

static void web_can_stream_clear_client(
    int socket
)
{
    int expected = socket;

    (void)atomic_compare_exchange_strong(
        &s_client_socket,
        &expected,
        WEB_CAN_STREAM_CLIENT_NONE
    );
}

static bool web_can_stream_event_allowed(
    const can_event_t *event
)
{
    if ((event == NULL) ||
        atomic_load(&s_paused)) {

        return false;
    }

    if ((event->frame.bus == CAN_BUS_PRIMARY) &&
        !atomic_load(&s_primary_enabled)) {

        return false;
    }

    if ((event->frame.bus == CAN_BUS_SECONDARY) &&
        !atomic_load(&s_secondary_enabled)) {

        return false;
    }

    if ((event->direction == CAN_FRAME_DIRECTION_RX) &&
        !atomic_load(&s_rx_enabled)) {

        return false;
    }

    if ((event->direction == CAN_FRAME_DIRECTION_TX) &&
        !atomic_load(&s_tx_enabled)) {

        return false;
    }

    return true;
}

static void web_can_stream_router_callback(
    const can_event_t *event,
    void *context
)
{
    (void)context;

    atomic_fetch_add(
        &s_active_callbacks,
        1U
    );

    if ((event == NULL) ||
        !atomic_load(&s_accepting_events) ||
        (atomic_load(&s_client_socket) ==
         WEB_CAN_STREAM_CLIENT_NONE) ||
        (s_event_queue == NULL)) {

        atomic_fetch_sub(
            &s_active_callbacks,
            1U
        );

        return;
    }

    if (!web_can_stream_event_allowed(
            event
        )) {

        atomic_fetch_add(
            &s_filtered_events,
            1U
        );

        atomic_fetch_sub(
            &s_active_callbacks,
            1U
        );

        return;
    }

    if (xQueueSend(
            s_event_queue,
            event,
            0U
        ) == pdPASS) {

        atomic_fetch_add(
            &s_queued_events,
            1U
        );

    } else {
        atomic_fetch_add(
            &s_dropped_events,
            1U
        );
    }

    atomic_fetch_sub(
        &s_active_callbacks,
        1U
    );
}

static esp_err_t web_can_stream_send_frame(
    int socket,
    httpd_ws_type_t type,
    uint8_t *payload,
    size_t length
)
{
    if ((s_server == NULL) ||
        (socket == WEB_CAN_STREAM_CLIENT_NONE) ||
        ((payload == NULL) && (length > 0U))) {

        return ESP_ERR_INVALID_ARG;
    }

    httpd_ws_frame_t frame = {
        .final =
            true,

        .fragmented =
            false,

        .type =
            type,

        .payload =
            payload,

        .len =
            length,
    };

    const esp_err_t result =
        httpd_ws_send_data(
            s_server,
            socket,
            &frame
        );

    if (result != ESP_OK) {
        atomic_fetch_add(
            &s_send_failures,
            1U
        );

        web_can_stream_clear_client(
            socket
        );

        ESP_LOGW(
            TAG,
            "WebSocket send failed: %s",
            esp_err_to_name(result)
        );
    }

    return result;
}

static void web_can_stream_send_statistics(
    int socket
)
{
    char message[
        WEB_CAN_STREAM_STATISTICS_SIZE
    ];

    const uint32_t queue_current =
        (s_event_queue != NULL)
            ? (uint32_t)uxQueueMessagesWaiting(
                  s_event_queue
              )
            : 0U;

    const uint32_t queue_peak =
        (uint32_t)atomic_load(
            &s_queue_peak
        );

    const uint64_t sent_batches =
        atomic_load_explicit(
            &s_sent_batches,
            memory_order_relaxed
        );

    const uint64_t batch_events_total =
        atomic_load_explicit(
            &s_batch_events_total,
            memory_order_relaxed
        );

    const uint32_t batch_events_average =
        (sent_batches > 0U)
            ? (uint32_t)(
                batch_events_total /
                sent_batches
            )
            : 0U;

    const int length =
        snprintf(
            message,
            sizeof(message),
            "{"
                "\"type\":\"stream_statistics\","
                "\"queued_events\":%" PRIuFAST64 ","
                "\"sent_events\":%" PRIuFAST64 ","
                "\"dropped_events\":%" PRIuFAST64 ","
                "\"send_failures\":%" PRIuFAST64 ","
                "\"filtered_events\":%" PRIuFAST64 ","
                "\"queue_current\":%" PRIu32 ","
                "\"queue_peak\":%" PRIu32 ","
                "\"queue_capacity\":%" PRIu32 ","
                "\"sent_batches\":%" PRIu64 ","
                "\"sent_binary_bytes\":%" PRIuFAST64 ","
                "\"batch_events_total\":%" PRIu64 ","
                "\"batch_events_peak\":%" PRIu32 ","
                "\"batch_events_average\":%" PRIu32
            "}",
            atomic_load(&s_queued_events),
            atomic_load(&s_sent_events),
            atomic_load(&s_dropped_events),
            atomic_load(&s_send_failures),
            atomic_load(&s_filtered_events),
            queue_current,
            queue_peak,
            s_config.queue_depth,
            sent_batches,
            atomic_load_explicit(
                &s_sent_binary_bytes,
                memory_order_relaxed
            ),
            batch_events_total,
            (uint32_t)atomic_load_explicit(
                &s_batch_events_peak,
                memory_order_relaxed
            ),
            batch_events_average
        );

    if ((length <= 0) ||
        ((size_t)length >= sizeof(message))) {

        return;
    }

    const esp_err_t result =
        web_can_stream_send_frame(
            socket,
            HTTPD_WS_TYPE_TEXT,
            (uint8_t *)message,
            (size_t)length
        );

    if (result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to send stream statistics: %s",
            esp_err_to_name(result)
        );

        return;
    }

    ESP_LOGD(
        TAG,
        "Stream statistics sent: socket=%d, size=%d",
        socket,
        length
    );
}

static esp_err_t web_can_stream_send_batch(
    int socket,
    const can_event_t *first_event
)
{
    if ((first_event == NULL) ||
        (s_batch_buffer == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    size_t used =
        WEB_CAN_BATCH_HEADER_SIZE;

    uint16_t event_count = 0U;

    can_event_t current_event =
        *first_event;

    const int64_t deadline_us =
        esp_timer_get_time() +
        ((int64_t)WEB_CAN_STREAM_BATCH_TIMEOUT_MS *
         1000LL);

    while (event_count <
           WEB_CAN_STREAM_BATCH_MAX_EVENTS) {

        if (!web_can_stream_event_allowed(
                &current_event
            )) {

            atomic_fetch_add(
                &s_filtered_events,
                1U
            );

        } else {
            size_t encoded_size = 0U;

            const esp_err_t encode_result =
                web_can_protocol_encode_event(
                    &current_event,
                    &s_batch_buffer[used],
                    WEB_CAN_STREAM_BATCH_MAX_SIZE - used,
                    &encoded_size
                );

            if (encode_result != ESP_OK) {
                atomic_fetch_add(
                    &s_dropped_events,
                    1U
                );
            } else {
                used += encoded_size;
                ++event_count;
            }
        }

        if (event_count >=
            WEB_CAN_STREAM_BATCH_MAX_EVENTS) {

            break;
        }

        const size_t remaining =
            WEB_CAN_STREAM_BATCH_MAX_SIZE -
            used;

        if (remaining <
            WEB_CAN_EVENT_MAX_ENCODED_SIZE) {

            break;
        }

        if (xQueueReceive(
                s_event_queue,
                &current_event,
                0U
            ) == pdPASS) {

            continue;
        }

        const int64_t now_us =
            esp_timer_get_time();

        if (now_us >= deadline_us) {
            break;
        }

        const uint32_t remaining_ms =
            (uint32_t)(
                (deadline_us - now_us + 999LL) /
                1000LL
            );

        if (xQueueReceive(
                s_event_queue,
                &current_event,
                pdMS_TO_TICKS(
                    remaining_ms
                )
            ) != pdPASS) {

            break;
        }
    }

    if (event_count == 0U) {
        return ESP_OK;
    }

    const size_t payload_size =
        used -
        WEB_CAN_BATCH_HEADER_SIZE;

    const esp_err_t header_result =
        web_can_protocol_write_batch_header(
            s_batch_buffer,
            WEB_CAN_BATCH_HEADER_SIZE,
            event_count,
            payload_size
        );

    if (header_result != ESP_OK) {
        return header_result;
    }

    const esp_err_t send_result =
        web_can_stream_send_frame(
            socket,
            HTTPD_WS_TYPE_BINARY,
            s_batch_buffer,
            used
        );

    if (send_result == ESP_OK) {
        atomic_fetch_add_explicit(
            &s_sent_events,
            event_count,
            memory_order_relaxed
        );

        atomic_fetch_add_explicit(
            &s_sent_batches,
            1U,
            memory_order_relaxed
        );

        atomic_fetch_add_explicit(
            &s_sent_binary_bytes,
            used,
            memory_order_relaxed
        );

        atomic_fetch_add_explicit(
            &s_batch_events_total,
            event_count,
            memory_order_relaxed
        );

        web_can_stream_update_batch_peak(
            event_count
        );
    }

    return send_result;
}

static esp_err_t web_can_stream_handle_subscribe(
    httpd_req_t *request,
    const uint8_t *payload,
    size_t length
)
{
    if ((request == NULL) ||
        (payload == NULL) ||
        (length == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root =
        cJSON_ParseWithLength(
            (const char *)payload,
            length
        );

    if (root == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const cJSON *command =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "command"
        );

    const cJSON *primary =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "primary"
        );

    const cJSON *secondary =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "secondary"
        );

    const cJSON *rx =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "rx"
        );

    const cJSON *tx =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "tx"
        );

    const cJSON *paused =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "paused"
        );

    const bool valid =
        cJSON_IsString(command) &&
        (strcmp(command->valuestring, "subscribe") == 0) &&
        cJSON_IsBool(primary) &&
        cJSON_IsBool(secondary) &&
        cJSON_IsBool(rx) &&
        cJSON_IsBool(tx) &&
        cJSON_IsBool(paused);

    if (!valid) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    const bool requested_paused =
        cJSON_IsTrue(paused);

    /*
     * Prevent the router callback from observing a partially updated
     * subscription.
     */
    atomic_store(
        &s_paused,
        true
    );

    atomic_store(
        &s_primary_enabled,
        cJSON_IsTrue(primary)
    );

    atomic_store(
        &s_secondary_enabled,
        cJSON_IsTrue(secondary)
    );

    atomic_store(
        &s_rx_enabled,
        cJSON_IsTrue(rx)
    );

    atomic_store(
        &s_tx_enabled,
        cJSON_IsTrue(tx)
    );

    /*
     * Publish the complete subscription.
     */
    atomic_store(
        &s_paused,
        requested_paused
    );

    cJSON_Delete(root);

    char response[192];

    const int response_length =
        snprintf(
            response,
            sizeof(response),
            "{"
                "\"type\":\"subscription\","
                "\"primary\":%s,"
                "\"secondary\":%s,"
                "\"rx\":%s,"
                "\"tx\":%s,"
                "\"paused\":%s"
            "}",
            atomic_load(&s_primary_enabled)
                ? "true" : "false",
            atomic_load(&s_secondary_enabled)
                ? "true" : "false",
            atomic_load(&s_rx_enabled)
                ? "true" : "false",
            atomic_load(&s_tx_enabled)
                ? "true" : "false",
            atomic_load(&s_paused)
                ? "true" : "false"
        );

    if ((response_length <= 0) ||
        ((size_t)response_length >=
         sizeof(response))) {

        return ESP_ERR_INVALID_SIZE;
    }

    httpd_ws_frame_t response_frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)response,
        .len = (size_t)response_length,
    };

    return httpd_ws_send_frame(
        request,
        &response_frame
    );
}

static void web_can_stream_task(
    void *argument
)
{
    (void)argument;

    int64_t next_statistics_us =
        esp_timer_get_time();

    if (s_config.statistics_interval_ms > 0U) {
        next_statistics_us +=
            (int64_t)s_config.statistics_interval_ms *
            1000LL;
    }

    while (!atomic_load(
               &s_stop_requested
           )) {

        can_event_t event;

        if (xQueueReceive(
                s_event_queue,
                &event,
                pdMS_TO_TICKS(
                    WEB_CAN_STREAM_IDLE_WAIT_MS
                )
            ) == pdPASS) {

            web_can_stream_update_queue_peak();

            const int socket =
                atomic_load(
                    &s_client_socket
                );

            if (socket !=
                WEB_CAN_STREAM_CLIENT_NONE) {

                (void)web_can_stream_send_batch(
                    socket,
                    &event
                );
            }
        }

        if (s_config.statistics_interval_ms > 0U) {
            const int64_t now_us =
                esp_timer_get_time();

            if (now_us >= next_statistics_us) {
                const int socket =
                    atomic_load(
                        &s_client_socket
                    );

                if (socket !=
                    WEB_CAN_STREAM_CLIENT_NONE) {

                    web_can_stream_send_statistics(
                        socket
                    );
                }

                next_statistics_us =
                    now_us +
                    ((int64_t)
                         s_config.statistics_interval_ms *
                     1000LL);
            }
        }
    }

    if (s_task_stopped != NULL) {
        (void)xSemaphoreGive(
            s_task_stopped
        );
    }

    vTaskDelete(
        NULL
    );
}

static esp_err_t web_can_stream_receive_frame(
    httpd_req_t *request,
    int socket
)
{
    httpd_ws_frame_t frame;

    memset(
        &frame,
        0,
        sizeof(frame)
    );

    esp_err_t result =
        httpd_ws_recv_frame(
            request,
            &frame,
            0U
        );

    if (result != ESP_OK) {
        return result;
    }

    if (frame.len >
        WEB_CAN_STREAM_RX_BUFFER_SIZE) {

        ESP_LOGW(
            TAG,
            "WebSocket message is too large: %u",
            (unsigned int)frame.len
        );

        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t payload[
        WEB_CAN_STREAM_RX_BUFFER_SIZE + 1U
    ];

    memset(
        payload,
        0,
        sizeof(payload)
    );

    frame.payload =
        payload;

    if (frame.len > 0U) {
        result =
            httpd_ws_recv_frame(
                request,
                &frame,
                frame.len
            );

        if (result != ESP_OK) {
            return result;
        }
    }

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        web_can_stream_clear_client(
            socket
        );

        ESP_LOGI(
            TAG,
            "CAN WebSocket client disconnected"
        );

        return ESP_OK;
    }

    if (frame.type == HTTPD_WS_TYPE_PING) {
        frame.type =
            HTTPD_WS_TYPE_PONG;

        return httpd_ws_send_frame(
            request,
            &frame
        );
    }

    if (frame.type == HTTPD_WS_TYPE_TEXT) {
        const esp_err_t command_result =
            web_can_stream_handle_subscribe(
                request,
                payload,
                frame.len
            );

        if (command_result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Invalid CAN WebSocket command: %s",
                esp_err_to_name(command_result)
            );

            static uint8_t response[] =
                "{\"type\":\"error\","
                "\"code\":\"invalid_command\"}";

            httpd_ws_frame_t response_frame = {
                .final = true,
                .fragmented = false,
                .type = HTTPD_WS_TYPE_TEXT,
                .payload = response,
                .len = sizeof(response) - 1U,
            };

            return httpd_ws_send_frame(
                request,
                &response_frame
            );
        }

        return ESP_OK;
    }

    /*
     * JSON control commands will be added later.
     */
    return ESP_OK;
}

static esp_err_t web_can_stream_websocket_handler(
    httpd_req_t *request
)
{
    if (request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!atomic_load(
            &s_running
        )) {

        return ESP_ERR_INVALID_STATE;
    }

    const int socket =
        httpd_req_to_sockfd(
            request
        );

    if (socket < 0) {
        return ESP_FAIL;
    }

    int current_socket =
        atomic_load(
            &s_client_socket
        );

    /*
     * ESP-IDF may invoke this handler for the first time only after
     * completing the WebSocket handshake. Register the client on its
     * first handler invocation instead of relying only on HTTP_GET.
     */
    if (current_socket ==
        WEB_CAN_STREAM_CLIENT_NONE) {

        /*
         * Clear data left by a previously disconnected client before
         * publishing the new socket.
         */
        if (s_event_queue != NULL) {
            (void)xQueueReset(
                s_event_queue
            );
        }

        atomic_store(
            &s_primary_enabled,
            true
        );

        atomic_store(
            &s_secondary_enabled,
            true
        );

        atomic_store(
            &s_rx_enabled,
            true
        );

        atomic_store(
            &s_tx_enabled,
            true
        );

        atomic_store(
            &s_paused,
            false
        );

        int expected =
            WEB_CAN_STREAM_CLIENT_NONE;

        if (atomic_compare_exchange_strong(
                &s_client_socket,
                &expected,
                socket
            )) {

            current_socket =
                socket;

            ESP_LOGI(
                TAG,
                "CAN WebSocket client registered: socket=%d",
                socket
            );
        } else {
            current_socket =
                expected;
        }
    }

    /*
     * Only one CAN stream client is supported. Reject an additional
     * socket without disturbing the active client.
     */
    if (current_socket != socket) {
        ESP_LOGW(
            TAG,
            "Rejecting additional CAN WebSocket client: "
            "socket=%d, active=%d",
            socket,
            current_socket
        );

        if (s_server != NULL) {
            (void)httpd_sess_trigger_close(
                s_server,
                socket
            );
        }

        return ESP_OK;
    }

    /*
     * Compatibility with ESP-IDF configurations that invoke the URI
     * handler during the initial handshake.
     */
    if (request->method == HTTP_GET) {
        return ESP_OK;
    }

    return web_can_stream_receive_frame(
        request,
        socket
    );
}

static void web_can_stream_release_resources(void)
{
    if (s_event_queue != NULL) {
        vQueueDelete(
            s_event_queue
        );

        s_event_queue = NULL;
    }

    if (s_event_queue_storage != NULL) {
        heap_caps_free(
            s_event_queue_storage
        );

        s_event_queue_storage = NULL;
    }

    if (s_batch_buffer != NULL) {
        heap_caps_free(
            s_batch_buffer
        );

        s_batch_buffer = NULL;
    }

    if (s_task_stopped != NULL) {
        vSemaphoreDelete(
            s_task_stopped
        );

        s_task_stopped = NULL;
    }
}

esp_err_t web_can_stream_service_start(
    httpd_handle_t server,
    const web_can_stream_service_config_t *config
)
{
    if ((server == NULL) ||
        (config == NULL) ||
        (config->queue_depth == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    bool expected = false;

    if (!atomic_compare_exchange_strong(
            &s_running,
            &expected,
            true
        )) {

        return ESP_ERR_INVALID_STATE;
    }

    if (!can_router_is_running()) {
        atomic_store(
            &s_running,
            false
        );

        return ESP_ERR_INVALID_STATE;
    }

    s_server =
        server;

    s_config =
        *config;

    atomic_store(
        &s_stop_requested,
        false
    );

    atomic_store(
        &s_accepting_events,
        false
    );

    atomic_store(
        &s_filtered_events,
        0U
    );

    atomic_store(
        &s_client_socket,
        WEB_CAN_STREAM_CLIENT_NONE
    );

    atomic_store(
        &s_queue_peak,
        0U
    );

    atomic_store(
        &s_sent_batches,
        0U
    );

    atomic_store(
        &s_sent_binary_bytes,
        0U
    );

    atomic_store(
        &s_batch_events_total,
        0U
    );

    atomic_store(
        &s_batch_events_peak,
        0U
    );

    atomic_store(
        &s_active_callbacks,
        0U
    );

    atomic_store(
        &s_queued_events,
        0U
    );

    atomic_store(
        &s_sent_events,
        0U
    );

    atomic_store(
        &s_dropped_events,
        0U
    );

    atomic_store(
        &s_send_failures,
        0U
    );

    const size_t queue_storage_size =
        (size_t)config->queue_depth *
        sizeof(can_event_t);

    if ((queue_storage_size /
         sizeof(can_event_t)) !=
        config->queue_depth) {

        atomic_store(
            &s_running,
            false
        );

        return ESP_ERR_INVALID_SIZE;
    }

    s_event_queue_storage =
        heap_caps_calloc(
            1U,
            queue_storage_size,
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

    s_batch_buffer =
        heap_caps_malloc(
            WEB_CAN_STREAM_BATCH_MAX_SIZE,
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

    s_task_stopped =
        xSemaphoreCreateBinary();

    if ((s_event_queue_storage == NULL) ||
        (s_batch_buffer == NULL) ||
        (s_task_stopped == NULL)) {

        web_can_stream_release_resources();

        s_server = NULL;

        atomic_store(
            &s_running,
            false
        );

        return ESP_ERR_NO_MEM;
    }

    s_event_queue =
        xQueueCreateStatic(
            config->queue_depth,
            sizeof(can_event_t),
            s_event_queue_storage,
            &s_event_queue_control
        );

    if (s_event_queue == NULL) {
        web_can_stream_release_resources();

        s_server = NULL;

        atomic_store(
            &s_running,
            false
        );

        return ESP_ERR_NO_MEM;
    }

    const httpd_uri_t websocket_uri = {
        .uri =
            WEB_CAN_STREAM_URI,

        .method =
            HTTP_GET,

        .handler =
            web_can_stream_websocket_handler,

        .user_ctx =
            NULL,

        .is_websocket =
            true,

        .handle_ws_control_frames =
            true,
    };

    esp_err_t result =
        httpd_register_uri_handler(
            server,
            &websocket_uri
        );

    if (result != ESP_OK) {
        web_can_stream_release_resources();

        s_server = NULL;

        atomic_store(
            &s_running,
            false
        );

        return result;
    }

    const can_router_subscription_t subscription = {
        .bus_mask =
            CAN_ROUTER_ALL_BUSES_MASK,

        .event_mask =
            CAN_ROUTER_ALL_EVENTS_MASK,

        .callback =
            web_can_stream_router_callback,

        .context =
            NULL,
    };

    result =
        can_router_subscribe(
            &subscription,
            &s_router_subscription_id
        );

    if (result != ESP_OK) {
        (void)httpd_unregister_uri_handler(
            server,
            WEB_CAN_STREAM_URI,
            HTTP_GET
        );

        web_can_stream_release_resources();

        s_server = NULL;

        atomic_store(
            &s_running,
            false
        );

        return result;
    }

    const BaseType_t task_result =
        xTaskCreate(
            web_can_stream_task,
            "web_can_stream",
            WEB_CAN_STREAM_TASK_STACK_SIZE,
            NULL,
            WEB_CAN_STREAM_TASK_PRIORITY,
            &s_stream_task
        );

    if (task_result != pdPASS) {
        (void)can_router_unsubscribe(
            s_router_subscription_id
        );

        s_router_subscription_id =
            CAN_ROUTER_SUBSCRIPTION_ID_NONE;

        (void)httpd_unregister_uri_handler(
            server,
            WEB_CAN_STREAM_URI,
            HTTP_GET
        );

        web_can_stream_release_resources();

        s_server = NULL;

        atomic_store(
            &s_running,
            false
        );

        return ESP_ERR_NO_MEM;
    }

    atomic_store(
        &s_accepting_events,
        true
    );

    ESP_LOGI(
        TAG,
        "CAN WebSocket stream started: queue=%" PRIu32
        ", batch=%u, timeout=%u ms",
        config->queue_depth,
        WEB_CAN_STREAM_BATCH_MAX_EVENTS,
        WEB_CAN_STREAM_BATCH_TIMEOUT_MS
    );

    return ESP_OK;
}

esp_err_t web_can_stream_service_stop(void)
{
    if (!atomic_load(
            &s_running
        )) {

        return ESP_ERR_INVALID_STATE;
    }

    atomic_store(
        &s_accepting_events,
        false
    );

    if (s_router_subscription_id !=
        CAN_ROUTER_SUBSCRIPTION_ID_NONE) {

        const esp_err_t unsubscribe_result =
            can_router_unsubscribe(
                s_router_subscription_id
            );

        if ((unsubscribe_result != ESP_OK) &&
            (unsubscribe_result != ESP_ERR_NOT_FOUND) &&
            (unsubscribe_result != ESP_ERR_INVALID_STATE)) {

            ESP_LOGW(
                TAG,
                "Failed to unsubscribe CAN stream: %s",
                esp_err_to_name(unsubscribe_result)
            );
        }

        s_router_subscription_id =
            CAN_ROUTER_SUBSCRIPTION_ID_NONE;
    }

    atomic_store(
        &s_stop_requested,
        true
    );

    const TickType_t callback_wait_start =
        xTaskGetTickCount();

    const TickType_t callback_wait_timeout =
        pdMS_TO_TICKS(
            WEB_CAN_STREAM_CALLBACK_WAIT_MS
        );

    while (atomic_load(
            &s_active_callbacks
        ) != 0U) {

        const TickType_t elapsed =
            xTaskGetTickCount() -
            callback_wait_start;

        if (elapsed >= callback_wait_timeout) {
            ESP_LOGW(
                TAG,
                "Timed out waiting for CAN callbacks"
            );

            /*
            * Resources must remain allocated because a callback may
            * still access the event queue.
            */
            return ESP_ERR_TIMEOUT;
        }

        vTaskDelay(
            1U
        );
    }

    const int socket =
        atomic_exchange(
            &s_client_socket,
            WEB_CAN_STREAM_CLIENT_NONE
        );

    if ((socket !=
        WEB_CAN_STREAM_CLIENT_NONE) &&
        (s_server != NULL)) {

        const esp_err_t close_result =
            httpd_sess_trigger_close(
                s_server,
                socket
            );

        if (close_result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Failed to close CAN WebSocket client: %s",
                esp_err_to_name(close_result)
            );
        }
    }

    if ((s_stream_task != NULL) &&
        (s_task_stopped != NULL)) {

        if (xSemaphoreTake(
                s_task_stopped,
                pdMS_TO_TICKS(
                    WEB_CAN_STREAM_STOP_TIMEOUT_MS
                )
            ) != pdTRUE) {

            /*
            * The task may still be using the HTTP server and batch
            * buffer. Resources must remain allocated.
            */
            return ESP_ERR_TIMEOUT;
        }
    }

    s_stream_task = NULL;

    if (s_server != NULL) {
        (void)httpd_unregister_uri_handler(
            s_server,
            WEB_CAN_STREAM_URI,
            HTTP_GET
        );
    }

    web_can_stream_release_resources();

    s_server = NULL;

    atomic_store(
        &s_running,
        false
    );

    ESP_LOGI(
        TAG,
        "CAN WebSocket stream stopped"
    );

    return ESP_OK;
}

esp_err_t web_can_stream_service_get_statistics(
    web_can_stream_service_statistics_t *statistics
)
{
    if (statistics == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!atomic_load(
            &s_running
        )) {

        return ESP_ERR_INVALID_STATE;
    }

    memset(
        statistics,
        0,
        sizeof(*statistics)
    );

    statistics->queued_events =
        atomic_load(
            &s_queued_events
        );

    statistics->sent_events =
        atomic_load(
            &s_sent_events
        );

    statistics->dropped_events =
        atomic_load(
            &s_dropped_events
        );

    statistics->send_failures =
        atomic_load(
            &s_send_failures
        );

    statistics->queue_current =
        (s_event_queue != NULL)
            ? (uint32_t)uxQueueMessagesWaiting(
                  s_event_queue
              )
            : 0U;

    statistics->queue_peak =
        atomic_load(
            &s_queue_peak
        );

    statistics->sent_batches =
        atomic_load_explicit(
            &s_sent_batches,
            memory_order_relaxed
        );

    statistics->sent_binary_bytes =
        atomic_load_explicit(
            &s_sent_binary_bytes,
            memory_order_relaxed
        );

    statistics->batch_events_total =
        atomic_load_explicit(
            &s_batch_events_total,
            memory_order_relaxed
        );

    statistics->batch_events_peak =
        atomic_load_explicit(
            &s_batch_events_peak,
            memory_order_relaxed
        );

    statistics->batch_events_average =
        (statistics->sent_batches > 0U)
            ? (uint32_t)(
                statistics->batch_events_total /
                statistics->sent_batches
            )
            : 0U;

    statistics->queue_capacity =
        s_config.queue_depth;

    statistics->client_connected =
        atomic_load(
            &s_client_socket
        ) != WEB_CAN_STREAM_CLIENT_NONE;

    statistics->filtered_events =
        atomic_load(
            &s_filtered_events
        );

    return ESP_OK;
}

esp_err_t web_can_stream_service_reset_statistics(void)
{
    if (!atomic_load(
            &s_running
        )) {

        return ESP_ERR_INVALID_STATE;
    }

    atomic_store(
        &s_queued_events,
        0U
    );

    atomic_store(
        &s_sent_events,
        0U
    );

    atomic_store(
        &s_dropped_events,
        0U
    );

    atomic_store(
        &s_send_failures,
        0U
    );

    atomic_store(
        &s_filtered_events,
        0U
    );

    atomic_store(
        &s_queue_peak,
        (s_event_queue != NULL)
            ? (uint32_t)uxQueueMessagesWaiting(
                  s_event_queue
              )
            : 0U
    );

    atomic_store_explicit(
        &s_sent_batches,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_sent_binary_bytes,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_batch_events_total,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_batch_events_peak,
        0U,
        memory_order_relaxed
    );

    return ESP_OK;
}

bool web_can_stream_service_is_running(void)
{
    return atomic_load(
        &s_running
    );
}
