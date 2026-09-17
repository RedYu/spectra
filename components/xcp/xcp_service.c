/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "xcp_service.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "app_task_priorities.h"
#include "can_router.h"
#include "xcp_commands.h"

#define XCP_SERVICE_TASK_STACK_SIZE          (6144U)
#define XCP_SERVICE_STOP_TIMEOUT_MS          (2000U)
#define XCP_SERVICE_LOCK_TIMEOUT_MS          (100U)
#define XCP_SERVICE_DEFAULT_QUEUE_DEPTH      (32U)
#define XCP_SERVICE_DEFAULT_TX_TIMEOUT_MS    (20U)
#define XCP_SERVICE_DEFAULT_RESPONSE_MS      (1000U)
#define XCP_SERVICE_WAIT_MARGIN_MS           (500U)

typedef enum
{
    XCP_SERVICE_COMMAND_CAN_EVENT = 0,
    XCP_SERVICE_COMMAND_EXECUTE,
    XCP_SERVICE_COMMAND_CANCEL,
    XCP_SERVICE_COMMAND_STOP,

} xcp_service_command_type_t;

typedef struct
{
    xcp_service_command_type_t type;
    uint32_t session_id;
    can_event_t can_event;

} xcp_service_command_t;

typedef struct
{
    bool open;
    bool connected;
    bool operation_reserved;
    uint32_t id;
    uint32_t pending_transaction_id;
    uint64_t response_deadline_us;

    xcp_service_session_state_t state;
    xcp_service_session_config_t config;
    xcp_connect_response_t slave;

    uint8_t command[XCP_CAN_FD_CTO_MAX_SIZE];
    size_t command_size;
    uint8_t response[XCP_SERVICE_RESPONSE_MAX_SIZE];
    size_t response_size;
    esp_err_t operation_result;
    uint8_t last_xcp_error;
    esp_err_t last_error;

    uint64_t transmitted_commands;
    uint64_t received_responses;
    uint64_t received_errors;
    uint64_t received_events;
    uint64_t timeouts;
    uint64_t unexpected_packets;

    SemaphoreHandle_t completion;

} xcp_service_session_t;

static const char *TAG = "xcp_service";

static xcp_service_session_t s_sessions[
    XCP_SERVICE_SESSION_COUNT
];

static SemaphoreHandle_t s_lock = NULL;
static QueueHandle_t s_queue = NULL;
static TaskHandle_t s_task = NULL;
static uint32_t s_subscription_id =
    CAN_ROUTER_SUBSCRIPTION_ID_NONE;
static uint32_t s_next_session_id = 1U;
static uint32_t s_transmit_timeout_ms =
    XCP_SERVICE_DEFAULT_TX_TIMEOUT_MS;
static uint32_t s_queue_capacity = 0U;
static atomic_bool s_running = false;
static atomic_uint_fast32_t s_queue_peak =
    ATOMIC_VAR_INIT(0U);
static atomic_uint_fast64_t s_dropped_commands =
    ATOMIC_VAR_INIT(0U);

static bool xcp_service_session_config_valid(
    const xcp_service_session_config_t *config
);

static xcp_service_session_t *xcp_service_find_session(
    uint32_t session_id
);

static bool xcp_service_identifiers_available(
    const xcp_service_session_config_t *config
);

static void xcp_service_router_callback(
    const can_event_t *event,
    void *context
);

static bool xcp_service_event_relevant(
    const can_event_t *event
);

static bool xcp_service_frame_matches_session(
    const can_frame_t *frame,
    const xcp_service_session_t *session
);

static void xcp_service_update_queue_peak(void);

static void xcp_service_task(void *context);

static void xcp_service_process_execute(
    uint32_t session_id
);

static void xcp_service_process_can_event(
    const can_event_t *event
);

static void xcp_service_process_receive(
    xcp_service_session_t *session,
    const can_frame_t *frame
);

static void xcp_service_process_confirmation(
    xcp_service_session_t *session,
    const can_event_t *event
);

static void xcp_service_process_timeouts(void);

static void xcp_service_complete(
    xcp_service_session_t *session,
    esp_err_t result
);

static void xcp_service_notify(
    xcp_service_session_t *session,
    const xcp_packet_t *packet
);

static esp_err_t xcp_service_execute_internal(
    uint32_t session_id,
    const uint8_t *command,
    size_t command_size,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_size,
    bool allow_disconnected
);

esp_err_t xcp_service_start(
    const xcp_service_config_t *config
)
{
    if (atomic_load(&s_running) ||
        !can_router_is_running()) {

        return ESP_ERR_INVALID_STATE;
    }

    const uint32_t queue_depth =
        ((config != NULL) &&
         (config->queue_depth != 0U))
            ? config->queue_depth
            : XCP_SERVICE_DEFAULT_QUEUE_DEPTH;

    if (queue_depth < 4U) {
        return ESP_ERR_INVALID_ARG;
    }

    s_transmit_timeout_ms =
        ((config != NULL) &&
         (config->transmit_timeout_ms != 0U))
            ? config->transmit_timeout_ms
            : XCP_SERVICE_DEFAULT_TX_TIMEOUT_MS;

    s_lock = xSemaphoreCreateMutex();

    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_queue = xQueueCreate(
        queue_depth,
        sizeof(xcp_service_command_t)
    );

    if (s_queue == NULL) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }

    memset(
        s_sessions,
        0,
        sizeof(s_sessions)
    );

    for (size_t index = 0U;
         index < XCP_SERVICE_SESSION_COUNT;
         ++index) {

        s_sessions[index].completion =
            xSemaphoreCreateBinary();

        if (s_sessions[index].completion == NULL) {
            for (size_t cleanup = 0U;
                 cleanup < index;
                 ++cleanup) {

                vSemaphoreDelete(
                    s_sessions[cleanup].completion
                );
                s_sessions[cleanup].completion = NULL;
            }

            vQueueDelete(s_queue);
            vSemaphoreDelete(s_lock);
            s_queue = NULL;
            s_lock = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    s_queue_capacity = queue_depth;
    atomic_store(&s_queue_peak, 0U);
    atomic_store(&s_dropped_commands, 0U);
    atomic_store(&s_running, true);

    const BaseType_t task_result =
        xTaskCreate(
            xcp_service_task,
            "xcp_service",
            XCP_SERVICE_TASK_STACK_SIZE,
            NULL,
            APP_TASK_PRIORITY_XCP,
            &s_task
        );

    if (task_result != pdPASS) {
        atomic_store(&s_running, false);

        for (size_t index = 0U;
             index < XCP_SERVICE_SESSION_COUNT;
             ++index) {

            vSemaphoreDelete(s_sessions[index].completion);
            s_sessions[index].completion = NULL;
        }

        vQueueDelete(s_queue);
        vSemaphoreDelete(s_lock);
        s_queue = NULL;
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }

    const can_router_subscription_t subscription = {
        .bus_mask = CAN_ROUTER_ALL_BUSES_MASK,
        .event_mask =
            CAN_ROUTER_EVENT_MASK(CAN_EVENT_RX) |
            CAN_ROUTER_EVENT_MASK(CAN_EVENT_TX_COMPLETED) |
            CAN_ROUTER_EVENT_MASK(CAN_EVENT_TX_FAILED) |
            CAN_ROUTER_EVENT_MASK(CAN_EVENT_TX_ABORTED),
        .callback = xcp_service_router_callback,
        .context = NULL,
    };

    const esp_err_t result =
        can_router_subscribe(
            &subscription,
            &s_subscription_id
        );

    if (result != ESP_OK) {
        (void)xcp_service_stop();
        return result;
    }

    ESP_LOGI(
        TAG,
        "XCP service started: sessions=%u, queue=%" PRIu32,
        XCP_SERVICE_SESSION_COUNT,
        queue_depth
    );

    return ESP_OK;
}

esp_err_t xcp_service_stop(void)
{
    if (!atomic_load(&s_running)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_subscription_id !=
        CAN_ROUTER_SUBSCRIPTION_ID_NONE) {

        (void)can_router_unsubscribe(s_subscription_id);
        s_subscription_id =
            CAN_ROUTER_SUBSCRIPTION_ID_NONE;
    }

    const xcp_service_command_t command = {
        .type = XCP_SERVICE_COMMAND_STOP,
    };

    if (xQueueSend(
            s_queue,
            &command,
            pdMS_TO_TICKS(XCP_SERVICE_STOP_TIMEOUT_MS)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    const TickType_t started = xTaskGetTickCount();

    while (atomic_load(&s_running)) {
        if ((xTaskGetTickCount() - started) >=
            pdMS_TO_TICKS(XCP_SERVICE_STOP_TIMEOUT_MS)) {

            return ESP_ERR_TIMEOUT;
        }

        vTaskDelay(pdMS_TO_TICKS(10U));
    }

    for (size_t index = 0U;
         index < XCP_SERVICE_SESSION_COUNT;
         ++index) {

        vSemaphoreDelete(s_sessions[index].completion);
        s_sessions[index].completion = NULL;
    }

    vQueueDelete(s_queue);
    vSemaphoreDelete(s_lock);
    s_queue = NULL;
    s_lock = NULL;
    s_task = NULL;
    s_queue_capacity = 0U;

    ESP_LOGI(TAG, "XCP service stopped");
    return ESP_OK;
}

bool xcp_service_is_running(void)
{
    return atomic_load(&s_running);
}

esp_err_t xcp_service_open_session(
    const xcp_service_session_config_t *config,
    uint32_t *session_id
)
{
    if ((config == NULL) ||
        (session_id == NULL) ||
        !xcp_service_session_config_valid(config)) {

        return ESP_ERR_INVALID_ARG;
    }

    *session_id = XCP_SERVICE_SESSION_ID_NONE;

    if (!atomic_load(&s_running)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_lock,
            pdMS_TO_TICKS(XCP_SERVICE_LOCK_TIMEOUT_MS)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    if (!xcp_service_identifiers_available(config)) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    xcp_service_session_t *session = NULL;

    for (size_t index = 0U;
         index < XCP_SERVICE_SESSION_COUNT;
         ++index) {

        if (!s_sessions[index].open) {
            session = &s_sessions[index];
            break;
        }
    }

    if (session == NULL) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NO_MEM;
    }

    const SemaphoreHandle_t completion =
        session->completion;

    memset(session, 0, sizeof(*session));
    session->completion = completion;
    session->config = *config;
    session->id = s_next_session_id++;

    if (session->id == XCP_SERVICE_SESSION_ID_NONE) {
        session->id = s_next_session_id++;
    }

    session->open = true;
    session->state =
        XCP_SERVICE_SESSION_DISCONNECTED;
    session->last_error = ESP_OK;
    *session_id = session->id;

    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t xcp_service_close_session(
    uint32_t session_id
)
{
    if ((session_id == XCP_SERVICE_SESSION_ID_NONE) ||
        !atomic_load(&s_running)) {

        return ESP_ERR_INVALID_ARG;
    }

    xcp_service_session_info_t info = {0};
    esp_err_t result =
        xcp_service_get_session_info(
            session_id,
            &info
        );

    if (result != ESP_OK) {
        return result;
    }

    if (info.connected) {
        result = xcp_service_disconnect(session_id);

        if (result != ESP_OK) {
            return result;
        }
    }

    if (xSemaphoreTake(
            s_lock,
            pdMS_TO_TICKS(XCP_SERVICE_LOCK_TIMEOUT_MS)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    xcp_service_session_t *session =
        xcp_service_find_session(session_id);

    if (session == NULL) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }

    if (session->operation_reserved ||
        (session->pending_transaction_id !=
         CAN_TRANSACTION_ID_NONE)) {

        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    const SemaphoreHandle_t completion =
        session->completion;

    memset(session, 0, sizeof(*session));
    session->completion = completion;

    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t xcp_service_connect(
    uint32_t session_id,
    uint8_t mode
)
{
    uint8_t command[XCP_CAN_CLASSIC_CTO_MAX_SIZE];
    size_t command_size = 0U;

    esp_err_t result =
        xcp_command_encode_connect(
            mode,
            command,
            sizeof(command),
            &command_size
        );

    if (result != ESP_OK) {
        return result;
    }

    uint8_t response[XCP_SERVICE_RESPONSE_MAX_SIZE];
    size_t response_size = 0U;

    result = xcp_service_execute_internal(
        session_id,
        command,
        command_size,
        response,
        sizeof(response),
        &response_size,
        true
    );

    if (result != ESP_OK) {
        return result;
    }

    xcp_packet_t packet = {0};
    xcp_connect_response_t slave = {0};

    result = xcp_protocol_decode_packet(
        response,
        response_size,
        &packet
    );

    if (result == ESP_OK) {
        result = xcp_protocol_decode_connect_response(
            &packet,
            &slave
        );
    }

    if (xSemaphoreTake(
            s_lock,
            pdMS_TO_TICKS(XCP_SERVICE_LOCK_TIMEOUT_MS)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    xcp_service_session_t *session =
        xcp_service_find_session(session_id);

    if (session == NULL) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }

    if (result == ESP_OK) {
        session->slave = slave;
        session->connected = true;
        session->state = XCP_SERVICE_SESSION_CONNECTED;
        session->last_error = ESP_OK;
    } else {
        session->state = XCP_SERVICE_SESSION_ERROR;
        session->last_error = result;
    }

    xSemaphoreGive(s_lock);
    return result;
}

esp_err_t xcp_service_disconnect(
    uint32_t session_id
)
{
    uint8_t command[XCP_CAN_CLASSIC_CTO_MAX_SIZE];
    size_t command_size = 0U;

    esp_err_t result =
        xcp_command_encode_disconnect(
            command,
            sizeof(command),
            &command_size
        );

    if (result != ESP_OK) {
        return result;
    }

    uint8_t response[XCP_SERVICE_RESPONSE_MAX_SIZE];
    size_t response_size = 0U;

    result = xcp_service_execute_internal(
        session_id,
        command,
        command_size,
        response,
        sizeof(response),
        &response_size,
        false
    );

    if (xSemaphoreTake(
            s_lock,
            pdMS_TO_TICKS(XCP_SERVICE_LOCK_TIMEOUT_MS)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    xcp_service_session_t *session =
        xcp_service_find_session(session_id);

    if (session != NULL) {
        if (result == ESP_OK) {
            session->connected = false;
            memset(&session->slave, 0, sizeof(session->slave));
            session->state =
                XCP_SERVICE_SESSION_DISCONNECTED;
        }

        session->last_error = result;
    }

    xSemaphoreGive(s_lock);
    return session != NULL
        ? result
        : ESP_ERR_NOT_FOUND;
}

esp_err_t xcp_service_execute_cto(
    uint32_t session_id,
    const uint8_t *command,
    size_t command_size,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_size
)
{
    return xcp_service_execute_internal(
        session_id,
        command,
        command_size,
        response,
        response_capacity,
        response_size,
        false
    );
}

esp_err_t xcp_service_cancel(
    uint32_t session_id
)
{
    if ((session_id == XCP_SERVICE_SESSION_ID_NONE) ||
        !atomic_load(&s_running)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(
            s_lock,
            pdMS_TO_TICKS(XCP_SERVICE_LOCK_TIMEOUT_MS)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    xcp_service_session_t *session =
        xcp_service_find_session(session_id);

    if (session == NULL) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }

    if (!session->operation_reserved) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    const xcp_service_command_t command = {
        .type = XCP_SERVICE_COMMAND_CANCEL,
        .session_id = session_id,
    };

    if (xQueueSend(s_queue, &command, 0U) != pdTRUE) {
        atomic_fetch_add(&s_dropped_commands, 1U);
        xSemaphoreGive(s_lock);
        return ESP_ERR_TIMEOUT;
    }

    xcp_service_update_queue_peak();
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t xcp_service_transmit_dto(
    uint32_t session_id,
    const uint8_t *data,
    size_t data_size,
    uint32_t *transaction_id
)
{
    if ((session_id == XCP_SERVICE_SESSION_ID_NONE) ||
        (data == NULL) ||
        (data_size == 0U) ||
        (data_size > CAN_FRAME_FD_DATA_MAX_LENGTH) ||
        (transaction_id == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(
            s_lock,
            pdMS_TO_TICKS(XCP_SERVICE_LOCK_TIMEOUT_MS)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    xcp_service_session_t *session =
        xcp_service_find_session(session_id);

    if ((session == NULL) ||
        !session->connected ||
        (data_size > session->config.transmit_data_length) ||
        ((session->slave.maximum_dto != 0U) &&
         (data_size > session->slave.maximum_dto))) {

        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    can_frame_t frame = {
        .bus = session->config.bus,
        .identifier = session->config.stim_identifier != 0U
            ? session->config.stim_identifier
            : session->config.command_identifier,
        .flags =
            (session->config.extended_identifier
                ? CAN_FRAME_FLAG_EXTENDED_ID
                : 0U) |
            (session->config.can_fd
                ? CAN_FRAME_FLAG_FD
                : 0U) |
            (session->config.bit_rate_switch
                ? CAN_FRAME_FLAG_BRS
                : 0U),
        .data_length = session->config.transmit_data_length,
        .timestamp_source = CAN_TIMESTAMP_SOURCE_NONE,
    };

    memset(
        frame.data,
        session->config.padding_byte,
        frame.data_length
    );
    memcpy(frame.data, data, data_size);

    uint8_t encoded_length = 0U;
    esp_err_t result =
        can_frame_length_to_dlc(
            frame.data_length,
            session->config.can_fd,
            &frame.dlc,
            &encoded_length
        );

    if ((result == ESP_OK) &&
        (encoded_length == frame.data_length)) {

        result = can_router_transmit(
            &frame,
            s_transmit_timeout_ms,
            transaction_id
        );

        if (result == ESP_OK) {
            session->transmitted_commands++;
        }
    }

    xSemaphoreGive(s_lock);
    return result;
}

esp_err_t xcp_service_transmit_cto(
    uint32_t session_id,
    const uint8_t *data,
    size_t data_size,
    uint32_t *transaction_id
)
{
    if ((session_id == XCP_SERVICE_SESSION_ID_NONE) ||
        (data == NULL) ||
        (data_size == 0U) ||
        (data_size > XCP_CAN_FD_CTO_MAX_SIZE) ||
        (data[0] < 0xC0U) ||
        (transaction_id == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(
            s_lock,
            pdMS_TO_TICKS(XCP_SERVICE_LOCK_TIMEOUT_MS)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    xcp_service_session_t *session =
        xcp_service_find_session(session_id);

    if ((session == NULL) ||
        !session->connected ||
        session->operation_reserved ||
        (data_size > session->config.transmit_data_length) ||
        ((session->slave.maximum_cto != 0U) &&
         (data_size > session->slave.maximum_cto))) {

        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    can_frame_t frame = {
        .bus = session->config.bus,
        .identifier = session->config.command_identifier,
        .flags =
            (session->config.extended_identifier
                ? CAN_FRAME_FLAG_EXTENDED_ID
                : 0U) |
            (session->config.can_fd
                ? CAN_FRAME_FLAG_FD
                : 0U) |
            (session->config.bit_rate_switch
                ? CAN_FRAME_FLAG_BRS
                : 0U),
        .data_length = session->config.transmit_data_length,
        .timestamp_source = CAN_TIMESTAMP_SOURCE_NONE,
    };

    memset(
        frame.data,
        session->config.padding_byte,
        frame.data_length
    );
    memcpy(frame.data, data, data_size);

    uint8_t encoded_length = 0U;
    esp_err_t result =
        can_frame_length_to_dlc(
            frame.data_length,
            session->config.can_fd,
            &frame.dlc,
            &encoded_length
        );

    if ((result == ESP_OK) &&
        (encoded_length == frame.data_length)) {

        result = can_router_transmit(
            &frame,
            s_transmit_timeout_ms,
            transaction_id
        );

        if (result == ESP_OK) {
            session->transmitted_commands++;
        }
    }

    xSemaphoreGive(s_lock);
    return result;
}

esp_err_t xcp_service_get_session_info(
    uint32_t session_id,
    xcp_service_session_info_t *info
)
{
    if ((session_id == XCP_SERVICE_SESSION_ID_NONE) ||
        (info == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (!atomic_load(&s_running)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_lock,
            pdMS_TO_TICKS(XCP_SERVICE_LOCK_TIMEOUT_MS)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    const xcp_service_session_t *session =
        xcp_service_find_session(session_id);

    if (session == NULL) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }

    *info = (xcp_service_session_info_t) {
        .open = session->open,
        .connected = session->connected,
        .state = session->state,
        .config = session->config,
        .slave = session->slave,
        .pending_transaction_id =
            session->pending_transaction_id,
        .last_xcp_error = session->last_xcp_error,
        .last_error = session->last_error,
        .transmitted_commands =
            session->transmitted_commands,
        .received_responses =
            session->received_responses,
        .received_errors =
            session->received_errors,
        .received_events =
            session->received_events,
        .timeouts = session->timeouts,
        .unexpected_packets =
            session->unexpected_packets,
    };

    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t xcp_service_get_queue_statistics(
    xcp_service_queue_statistics_t *statistics
)
{
    if (statistics == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!atomic_load(&s_running) ||
        (s_queue == NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    statistics->current =
        (uint32_t)uxQueueMessagesWaiting(s_queue);
    statistics->peak =
        (uint32_t)atomic_load(&s_queue_peak);
    statistics->capacity = s_queue_capacity;
    statistics->dropped =
        (uint64_t)atomic_load(&s_dropped_commands);

    return ESP_OK;
}

static bool xcp_service_session_config_valid(
    const xcp_service_session_config_t *config
)
{
    if ((config == NULL) ||
        (config->bus >= CAN_BUS_COUNT) ||
        (config->command_identifier ==
         config->response_identifier) ||
        (config->transmit_data_length == 0U) ||
        ((config->bus == CAN_BUS_PRIMARY) && config->can_fd) ||
        (config->bit_rate_switch && !config->can_fd)) {

        return false;
    }

    const uint32_t maximum_identifier =
        config->extended_identifier
            ? CAN_FRAME_EXTENDED_ID_MAX
            : CAN_FRAME_STANDARD_ID_MAX;

    if ((config->command_identifier > maximum_identifier) ||
        (config->response_identifier > maximum_identifier) ||
        ((config->stim_identifier != 0U) &&
         ((config->stim_identifier > maximum_identifier) ||
          (config->stim_identifier ==
           config->response_identifier)))) {

        return false;
    }

    uint8_t dlc = 0U;
    uint8_t encoded_length = 0U;

    return
        (can_frame_length_to_dlc(
            config->transmit_data_length,
            config->can_fd,
            &dlc,
            &encoded_length
        ) == ESP_OK) &&
        (encoded_length == config->transmit_data_length);
}

static xcp_service_session_t *xcp_service_find_session(
    uint32_t session_id
)
{
    for (size_t index = 0U;
         index < XCP_SERVICE_SESSION_COUNT;
         ++index) {

        if (s_sessions[index].open &&
            (s_sessions[index].id == session_id)) {

            return &s_sessions[index];
        }
    }

    return NULL;
}

static bool xcp_service_identifiers_available(
    const xcp_service_session_config_t *config
)
{
    const uint32_t requested_stim =
        config->stim_identifier != 0U
            ? config->stim_identifier
            : config->command_identifier;

    for (size_t index = 0U;
         index < XCP_SERVICE_SESSION_COUNT;
         ++index) {

        const xcp_service_session_t *session =
            &s_sessions[index];

        if (!session->open ||
            (session->config.bus != config->bus) ||
            (session->config.extended_identifier !=
             config->extended_identifier)) {

            continue;
        }

        if ((session->config.command_identifier ==
             config->command_identifier) ||
            (session->config.command_identifier ==
             config->response_identifier) ||
            (session->config.response_identifier ==
             config->command_identifier) ||
            (session->config.response_identifier ==
             config->response_identifier)) {

            return false;
        }

        const uint32_t existing_stim =
            session->config.stim_identifier != 0U
                ? session->config.stim_identifier
                : session->config.command_identifier;

        if ((requested_stim == session->config.command_identifier) ||
            (requested_stim == session->config.response_identifier) ||
            (requested_stim == existing_stim) ||
            (existing_stim == config->command_identifier) ||
            (existing_stim == config->response_identifier)) {

            return false;
        }
    }

    return true;
}

static void xcp_service_router_callback(
    const can_event_t *event,
    void *context
)
{
    (void)context;

    if ((event == NULL) ||
        !atomic_load(&s_running) ||
        (s_queue == NULL) ||
        !xcp_service_event_relevant(event)) {

        return;
    }

    const xcp_service_command_t command = {
        .type = XCP_SERVICE_COMMAND_CAN_EVENT,
        .can_event = *event,
    };

    if (xQueueSend(s_queue, &command, 0U) != pdTRUE) {
        atomic_fetch_add(&s_dropped_commands, 1U);
        return;
    }

    xcp_service_update_queue_peak();
}

static bool xcp_service_event_relevant(
    const can_event_t *event
)
{
    for (size_t index = 0U;
         index < XCP_SERVICE_SESSION_COUNT;
         ++index) {

        const xcp_service_session_t *session =
            &s_sessions[index];

        if (!session->open ||
            (event->frame.bus != session->config.bus)) {

            continue;
        }

        if ((event->type == CAN_EVENT_RX) &&
            xcp_service_frame_matches_session(
                &event->frame,
                session
            )) {

            return true;
        }

        if ((event->type != CAN_EVENT_RX) &&
            (event->transaction_id !=
             CAN_TRANSACTION_ID_NONE) &&
            (event->transaction_id ==
             session->pending_transaction_id)) {

            return true;
        }
    }

    return false;
}

static bool xcp_service_frame_matches_session(
    const can_frame_t *frame,
    const xcp_service_session_t *session
)
{
    const bool extended =
        (frame->flags & CAN_FRAME_FLAG_EXTENDED_ID) != 0U;
    const bool can_fd =
        (frame->flags & CAN_FRAME_FLAG_FD) != 0U;

    return
        (frame->bus == session->config.bus) &&
        (frame->identifier ==
         session->config.response_identifier) &&
        (extended == session->config.extended_identifier) &&
        (can_fd == session->config.can_fd) &&
        ((frame->flags & CAN_FRAME_FLAG_REMOTE) == 0U) &&
        (frame->data_length > 0U);
}

static void xcp_service_update_queue_peak(void)
{
    if (s_queue == NULL) {
        return;
    }

    const uint_fast32_t current =
        (uint_fast32_t)uxQueueMessagesWaiting(s_queue);
    uint_fast32_t peak = atomic_load(&s_queue_peak);

    while ((current > peak) &&
           !atomic_compare_exchange_weak(
               &s_queue_peak,
               &peak,
               current
           )) {
    }
}

static void xcp_service_task(void *context)
{
    (void)context;

    bool stop_requested = false;

    while (!stop_requested) {
        xcp_service_command_t command = {0};

        if (xQueueReceive(
                s_queue,
                &command,
                pdMS_TO_TICKS(10U)
            ) == pdTRUE) {

            switch (command.type) {
                case XCP_SERVICE_COMMAND_CAN_EVENT:
                    xcp_service_process_can_event(
                        &command.can_event
                    );
                    break;

                case XCP_SERVICE_COMMAND_EXECUTE:
                    xcp_service_process_execute(
                        command.session_id
                    );
                    break;

                case XCP_SERVICE_COMMAND_CANCEL: {
                    xcp_service_session_t *session =
                        xcp_service_find_session(
                            command.session_id
                        );

                    if ((session != NULL) &&
                        session->operation_reserved) {

                        xcp_service_complete(
                            session,
                            ESP_ERR_INVALID_STATE
                        );
                    }
                    break;
                }

                case XCP_SERVICE_COMMAND_STOP:
                    stop_requested = true;
                    break;

                default:
                    break;
            }
        }

        xcp_service_process_timeouts();
    }

    for (size_t index = 0U;
         index < XCP_SERVICE_SESSION_COUNT;
         ++index) {

        if (s_sessions[index].operation_reserved) {
            xcp_service_complete(
                &s_sessions[index],
                ESP_ERR_INVALID_STATE
            );
        }
    }

    atomic_store(&s_running, false);
    vTaskDelete(NULL);
}

static void xcp_service_process_execute(
    uint32_t session_id
)
{
    xcp_service_session_t *session =
        xcp_service_find_session(session_id);

    if ((session == NULL) ||
        !session->operation_reserved) {

        return;
    }

    can_frame_t frame = {
        .bus = session->config.bus,
        .identifier =
            session->config.command_identifier,
        .flags =
            (session->config.extended_identifier
                ? CAN_FRAME_FLAG_EXTENDED_ID
                : 0U) |
            (session->config.can_fd
                ? CAN_FRAME_FLAG_FD
                : 0U) |
            (session->config.bit_rate_switch
                ? CAN_FRAME_FLAG_BRS
                : 0U),
        .data_length =
            session->config.transmit_data_length,
        .timestamp_source =
            CAN_TIMESTAMP_SOURCE_NONE,
    };

    uint8_t encoded_length = 0U;
    esp_err_t result =
        can_frame_length_to_dlc(
            frame.data_length,
            session->config.can_fd,
            &frame.dlc,
            &encoded_length
        );

    if ((result != ESP_OK) ||
        (encoded_length != frame.data_length)) {

        xcp_service_complete(
            session,
            ESP_ERR_INVALID_SIZE
        );
        return;
    }

    memset(
        frame.data,
        session->config.padding_byte,
        frame.data_length
    );
    memcpy(
        frame.data,
        session->command,
        session->command_size
    );

    result = can_router_transmit(
        &frame,
        s_transmit_timeout_ms,
        &session->pending_transaction_id
    );

    if (result != ESP_OK) {
        xcp_service_complete(session, result);
        return;
    }

    session->transmitted_commands++;
    session->response_deadline_us =
        (uint64_t)esp_timer_get_time() +
        ((uint64_t)(
            session->config.response_timeout_ms != 0U
                ? session->config.response_timeout_ms
                : XCP_SERVICE_DEFAULT_RESPONSE_MS
        ) * UINT64_C(1000));
}

static void xcp_service_process_can_event(
    const can_event_t *event
)
{
    for (size_t index = 0U;
         index < XCP_SERVICE_SESSION_COUNT;
         ++index) {

        xcp_service_session_t *session =
            &s_sessions[index];

        if (!session->open ||
            (event->frame.bus != session->config.bus)) {

            continue;
        }

        if ((event->type == CAN_EVENT_RX) &&
            xcp_service_frame_matches_session(
                &event->frame,
                session
            )) {

            xcp_service_process_receive(
                session,
                &event->frame
            );
            continue;
        }

        if ((event->transaction_id ==
             session->pending_transaction_id) &&
            (session->pending_transaction_id !=
             CAN_TRANSACTION_ID_NONE)) {

            xcp_service_process_confirmation(
                session,
                event
            );
        }
    }
}

static void xcp_service_process_receive(
    xcp_service_session_t *session,
    const can_frame_t *frame
)
{
    xcp_packet_t packet = {0};
    const esp_err_t decode_result =
        xcp_protocol_decode_packet(
            frame->data,
            frame->data_length,
            &packet
        );

    if (decode_result != ESP_OK) {
        session->unexpected_packets++;
        return;
    }

    if (packet.type == XCP_PACKET_RESPONSE) {
        if (!session->operation_reserved) {
            session->unexpected_packets++;
            return;
        }

        memcpy(
            session->response,
            frame->data,
            frame->data_length
        );
        session->response_size = frame->data_length;
        session->received_responses++;
        xcp_service_complete(session, ESP_OK);
        return;
    }

    if (packet.type == XCP_PACKET_ERROR) {
        if (!session->operation_reserved) {
            session->unexpected_packets++;
            return;
        }

        memcpy(
            session->response,
            frame->data,
            frame->data_length
        );
        session->response_size = frame->data_length;
        session->last_xcp_error = packet.code;
        session->received_errors++;
        xcp_service_complete(
            session,
            ESP_ERR_INVALID_RESPONSE
        );
        return;
    }

    session->received_events++;
    xcp_service_notify(session, &packet);
}

static void xcp_service_process_confirmation(
    xcp_service_session_t *session,
    const can_event_t *event
)
{
    session->pending_transaction_id =
        CAN_TRANSACTION_ID_NONE;

    if ((event->type == CAN_EVENT_TX_FAILED) ||
        (event->type == CAN_EVENT_TX_ABORTED)) {

        xcp_service_complete(
            session,
            event->result != ESP_OK
                ? event->result
                : ESP_FAIL
        );
    }
}

static void xcp_service_process_timeouts(void)
{
    const uint64_t now_us =
        (uint64_t)esp_timer_get_time();

    for (size_t index = 0U;
         index < XCP_SERVICE_SESSION_COUNT;
         ++index) {

        xcp_service_session_t *session =
            &s_sessions[index];

        if (session->operation_reserved &&
            (session->response_deadline_us != 0U) &&
            (now_us >= session->response_deadline_us)) {

            session->timeouts++;
            xcp_service_complete(
                session,
                ESP_ERR_TIMEOUT
            );
        }
    }
}

static void xcp_service_complete(
    xcp_service_session_t *session,
    esp_err_t result
)
{
    session->pending_transaction_id =
        CAN_TRANSACTION_ID_NONE;
    session->response_deadline_us = 0U;
    session->operation_result = result;
    session->last_error = result;
    session->state = session->connected
        ? XCP_SERVICE_SESSION_CONNECTED
        : XCP_SERVICE_SESSION_DISCONNECTED;

    xSemaphoreGive(session->completion);
}

static void xcp_service_notify(
    xcp_service_session_t *session,
    const xcp_packet_t *packet
)
{
    if ((session->config.callback == NULL) ||
        (packet == NULL)) {

        return;
    }

    const xcp_service_event_t event = {
        .session_id = session->id,
        .type = packet->type,
        .packet_identifier =
            packet->packet_identifier,
        .code = packet->code,
        .payload = packet->payload,
        .payload_length = packet->payload_length,
    };

    session->config.callback(
        &event,
        session->config.callback_context
    );
}

static esp_err_t xcp_service_execute_internal(
    uint32_t session_id,
    const uint8_t *command,
    size_t command_size,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_size,
    bool allow_disconnected
)
{
    if ((session_id == XCP_SERVICE_SESSION_ID_NONE) ||
        (command == NULL) ||
        (command_size == 0U) ||
        (command_size > XCP_CAN_FD_CTO_MAX_SIZE) ||
        (command[0] < 0xC0U) ||
        (response == NULL) ||
        (response_capacity == 0U) ||
        (response_size == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *response_size = 0U;

    if (!atomic_load(&s_running)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_lock,
            pdMS_TO_TICKS(XCP_SERVICE_LOCK_TIMEOUT_MS)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    xcp_service_session_t *session =
        xcp_service_find_session(session_id);

    if (session == NULL) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }

    if (session->operation_reserved ||
        (!session->connected && !allow_disconnected) ||
        (command_size >
         session->config.transmit_data_length) ||
        (session->connected &&
         (session->slave.maximum_cto != 0U) &&
         (command_size > session->slave.maximum_cto))) {

        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    while (xSemaphoreTake(session->completion, 0U) == pdTRUE) {
    }

    memcpy(
        session->command,
        command,
        command_size
    );
    session->command_size = command_size;
    session->response_size = 0U;
    session->operation_result = ESP_ERR_INVALID_STATE;
    session->last_xcp_error = 0U;
    session->operation_reserved = true;
    session->state = allow_disconnected
        ? XCP_SERVICE_SESSION_CONNECTING
        : ((command[0] == XCP_COMMAND_DISCONNECT)
            ? XCP_SERVICE_SESSION_DISCONNECTING
            : XCP_SERVICE_SESSION_COMMAND_PENDING);

    const xcp_service_command_t service_command = {
        .type = XCP_SERVICE_COMMAND_EXECUTE,
        .session_id = session_id,
    };

    if (xQueueSend(
            s_queue,
            &service_command,
            0U
        ) != pdTRUE) {

        session->operation_reserved = false;
        session->state = session->connected
            ? XCP_SERVICE_SESSION_CONNECTED
            : XCP_SERVICE_SESSION_DISCONNECTED;
        atomic_fetch_add(&s_dropped_commands, 1U);
        xSemaphoreGive(s_lock);
        return ESP_ERR_TIMEOUT;
    }

    const uint32_t response_timeout_ms =
        session->config.response_timeout_ms != 0U
            ? session->config.response_timeout_ms
            : XCP_SERVICE_DEFAULT_RESPONSE_MS;

    xcp_service_update_queue_peak();
    xSemaphoreGive(s_lock);

    const TickType_t wait_ticks =
        pdMS_TO_TICKS(
            response_timeout_ms +
            s_transmit_timeout_ms +
            XCP_SERVICE_WAIT_MARGIN_MS
        );

    if (xSemaphoreTake(
            session->completion,
            wait_ticks
        ) != pdTRUE) {

        (void)xcp_service_cancel(session_id);
        return ESP_ERR_TIMEOUT;
    }

    if (xSemaphoreTake(
            s_lock,
            pdMS_TO_TICKS(XCP_SERVICE_LOCK_TIMEOUT_MS)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    session = xcp_service_find_session(session_id);

    if (session == NULL) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }

    const esp_err_t result =
        session->operation_result;

    if (session->response_size > response_capacity) {
        session->operation_reserved = false;
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_SIZE;
    }

    if (session->response_size > 0U) {
        memcpy(
            response,
            session->response,
            session->response_size
        );
    }

    *response_size = session->response_size;
    session->operation_reserved = false;

    xSemaphoreGive(s_lock);
    return result;
}
