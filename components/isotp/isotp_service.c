/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "isotp_service.h"

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

#define ISOTP_SERVICE_TASK_STACK_SIZE        (6144U)
#define ISOTP_SERVICE_STOP_TIMEOUT_MS        (2000U)
#define ISOTP_SERVICE_DEFAULT_QUEUE_DEPTH    (32U)
#define ISOTP_SERVICE_DEFAULT_TX_TIMEOUT_MS  (20U)

typedef enum
{
    ISOTP_SERVICE_COMMAND_CAN_EVENT = 0,
    ISOTP_SERVICE_COMMAND_START_TRANSMIT,
    ISOTP_SERVICE_COMMAND_STOP,

} isotp_service_command_type_t;

typedef struct
{
    isotp_service_command_type_t type;
    uint32_t channel_id;
    can_event_t can_event;

} isotp_service_command_t;

typedef struct
{
    bool open;
    bool transmit_reserved;
    uint32_t id;
    uint32_t pending_transaction_id;
    size_t requested_transmit_size;
    isotp_service_channel_config_t config;
    isotp_session_t session;

} isotp_service_channel_t;

static const char *TAG = "isotp_service";

static isotp_service_channel_t s_channels[
    ISOTP_SERVICE_CHANNEL_COUNT
];

static SemaphoreHandle_t s_lock = NULL;
static QueueHandle_t s_queue = NULL;
static TaskHandle_t s_task = NULL;
static uint32_t s_subscription_id =
    CAN_ROUTER_SUBSCRIPTION_ID_NONE;
static uint32_t s_next_channel_id = 1U;
static uint32_t s_transmit_timeout_ms =
    ISOTP_SERVICE_DEFAULT_TX_TIMEOUT_MS;
static atomic_bool s_running = false;

static bool isotp_service_channel_config_valid(
    const isotp_service_channel_config_t *config
);

static isotp_service_channel_t *isotp_service_find_channel(
    uint32_t channel_id
);

static void isotp_service_router_callback(
    const can_event_t *event,
    void *context
);

static bool isotp_service_event_relevant(
    const can_event_t *event
);

static bool isotp_service_frame_matches_channel(
    const can_frame_t *frame,
    const isotp_service_channel_t *channel
);

static void isotp_service_task(
    void *context
);

static void isotp_service_process_can_event(
    const can_event_t *event
);

static void isotp_service_process_start_transmit(
    uint32_t channel_id
);

static void isotp_service_process_action(
    isotp_service_channel_t *channel,
    const isotp_session_action_t *action
);

static esp_err_t isotp_service_send_frame(
    isotp_service_channel_t *channel,
    const isotp_session_action_t *action
);

static void isotp_service_notify(
    isotp_service_channel_t *channel,
    isotp_service_event_type_t type,
    const uint8_t *payload,
    size_t payload_length,
    esp_err_t result,
    isotp_session_error_t session_error
);

esp_err_t isotp_service_start(
    const isotp_service_config_t *config
)
{
    if (atomic_load(&s_running)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!can_router_is_running()) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint32_t queue_depth =
        ((config != NULL) &&
         (config->queue_depth != 0U))
            ? config->queue_depth
            : ISOTP_SERVICE_DEFAULT_QUEUE_DEPTH;

    if (queue_depth < 4U) {
        return ESP_ERR_INVALID_ARG;
    }

    s_transmit_timeout_ms =
        ((config != NULL) &&
         (config->transmit_timeout_ms != 0U))
            ? config->transmit_timeout_ms
            : ISOTP_SERVICE_DEFAULT_TX_TIMEOUT_MS;

    s_lock = xSemaphoreCreateMutex();

    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_queue =
        xQueueCreate(
            queue_depth,
            sizeof(isotp_service_command_t)
        );

    if (s_queue == NULL) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }

    memset(
        s_channels,
        0,
        sizeof(s_channels)
    );

    atomic_store(&s_running, true);

    const BaseType_t task_result =
        xTaskCreate(
            isotp_service_task,
            "isotp_service",
            ISOTP_SERVICE_TASK_STACK_SIZE,
            NULL,
            APP_TASK_PRIORITY_ISOTP,
            &s_task
        );

    if (task_result != pdPASS) {
        atomic_store(&s_running, false);
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
        .callback = isotp_service_router_callback,
        .context = NULL,
    };

    const esp_err_t result =
        can_router_subscribe(
            &subscription,
            &s_subscription_id
        );

    if (result != ESP_OK) {
        (void)isotp_service_stop();
        return result;
    }

    ESP_LOGI(
        TAG,
        "ISO-TP service started: channels=%u, queue=%" PRIu32,
        ISOTP_SERVICE_CHANNEL_COUNT,
        queue_depth
    );

    return ESP_OK;
}

esp_err_t isotp_service_stop(void)
{
    if (!atomic_load(&s_running)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_subscription_id !=
        CAN_ROUTER_SUBSCRIPTION_ID_NONE) {

        (void)can_router_unsubscribe(
            s_subscription_id
        );
        s_subscription_id =
            CAN_ROUTER_SUBSCRIPTION_ID_NONE;
    }

    const isotp_service_command_t command = {
        .type = ISOTP_SERVICE_COMMAND_STOP,
    };

    if (xQueueSend(
            s_queue,
            &command,
            pdMS_TO_TICKS(ISOTP_SERVICE_STOP_TIMEOUT_MS)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    const TickType_t started = xTaskGetTickCount();

    while (atomic_load(&s_running)) {
        if ((xTaskGetTickCount() - started) >=
            pdMS_TO_TICKS(ISOTP_SERVICE_STOP_TIMEOUT_MS)) {

            return ESP_ERR_TIMEOUT;
        }

        vTaskDelay(pdMS_TO_TICKS(10U));
    }

    vQueueDelete(s_queue);
    vSemaphoreDelete(s_lock);
    s_queue = NULL;
    s_lock = NULL;
    s_task = NULL;

    return ESP_OK;
}

bool isotp_service_is_running(void)
{
    return atomic_load(&s_running);
}

esp_err_t isotp_service_open_channel(
    const isotp_service_channel_config_t *config,
    uint32_t *channel_id
)
{
    if ((config == NULL) ||
        (channel_id == NULL) ||
        !isotp_service_channel_config_valid(config)) {

        return ESP_ERR_INVALID_ARG;
    }

    *channel_id = ISOTP_SERVICE_CHANNEL_ID_NONE;

    if (!atomic_load(&s_running)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_lock,
            pdMS_TO_TICKS(100U)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    isotp_service_channel_t *channel = NULL;

    for (size_t index = 0U;
         index < ISOTP_SERVICE_CHANNEL_COUNT;
         ++index) {

        if (!s_channels[index].open) {
            channel = &s_channels[index];
            break;
        }
    }

    if (channel == NULL) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NO_MEM;
    }

    memset(
        channel,
        0,
        sizeof(*channel)
    );

    channel->config = *config;
    channel->id = s_next_channel_id++;

    if (channel->id == ISOTP_SERVICE_CHANNEL_ID_NONE) {
        channel->id = s_next_channel_id++;
    }

    const esp_err_t result =
        isotp_session_init(
            &channel->session,
            &config->session,
            config->receive_buffer,
            config->receive_capacity
        );

    if (result == ESP_OK) {
        channel->open = true;
        *channel_id = channel->id;
    }

    xSemaphoreGive(s_lock);

    return result;
}

esp_err_t isotp_service_close_channel(
    uint32_t channel_id
)
{
    if ((channel_id == ISOTP_SERVICE_CHANNEL_ID_NONE) ||
        !atomic_load(&s_running)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(
            s_lock,
            pdMS_TO_TICKS(100U)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    isotp_service_channel_t *channel =
        isotp_service_find_channel(channel_id);

    if (channel == NULL) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }

    if ((channel->pending_transaction_id !=
         CAN_TRANSACTION_ID_NONE) ||
        channel->transmit_reserved ||
        (channel->session.state != ISOTP_SESSION_IDLE)) {

        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    memset(
        channel,
        0,
        sizeof(*channel)
    );

    xSemaphoreGive(s_lock);

    return ESP_OK;
}

esp_err_t isotp_service_send(
    uint32_t channel_id,
    const uint8_t *payload,
    size_t payload_length
)
{
    if ((channel_id == ISOTP_SERVICE_CHANNEL_ID_NONE) ||
        (payload == NULL) ||
        (payload_length == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (!atomic_load(&s_running)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_lock,
            pdMS_TO_TICKS(100U)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    isotp_service_channel_t *channel =
        isotp_service_find_channel(channel_id);

    if (channel == NULL) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }

    if (payload_length > channel->config.transmit_capacity) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_SIZE;
    }

    if (channel->transmit_reserved ||
        (channel->session.state != ISOTP_SESSION_IDLE)) {

        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    if (payload != channel->config.transmit_buffer) {
        memcpy(
            channel->config.transmit_buffer,
            payload,
            payload_length
        );
    }

    channel->requested_transmit_size = payload_length;
    channel->transmit_reserved = true;

    const isotp_service_command_t command = {
        .type = ISOTP_SERVICE_COMMAND_START_TRANSMIT,
        .channel_id = channel_id,
    };

    if (xQueueSend(
            s_queue,
            &command,
            0U
        ) != pdTRUE) {

        channel->transmit_reserved = false;
        channel->requested_transmit_size = 0U;
        xSemaphoreGive(s_lock);
        return ESP_ERR_TIMEOUT;
    }

    xSemaphoreGive(s_lock);

    return ESP_OK;
}

esp_err_t isotp_service_get_channel_info(
    uint32_t channel_id,
    isotp_service_channel_info_t *info
)
{
    if ((channel_id == ISOTP_SERVICE_CHANNEL_ID_NONE) ||
        (info == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (!atomic_load(&s_running)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_lock,
            pdMS_TO_TICKS(100U)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    const isotp_service_channel_t *channel =
        isotp_service_find_channel(channel_id);

    if (channel == NULL) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }

    *info = (isotp_service_channel_info_t) {
        .open = channel->open,
        .busy =
            channel->transmit_reserved ||
            (channel->session.state != ISOTP_SESSION_IDLE),
        .state = channel->session.state,
        .error = channel->session.error,
        .receive_size = channel->session.receive_size,
        .transmit_size = channel->session.transmit_size,
        .pending_transaction_id =
            channel->pending_transaction_id,
    };

    xSemaphoreGive(s_lock);

    return ESP_OK;
}

static bool isotp_service_channel_config_valid(
    const isotp_service_channel_config_t *config
)
{
    if ((config->bus >= CAN_BUS_COUNT) ||
        (config->receive_buffer == NULL) ||
        (config->receive_capacity == 0U) ||
        (config->transmit_buffer == NULL) ||
        (config->transmit_capacity == 0U) ||
        (config->callback == NULL) ||
        (config->session.link_data_length == 0U) ||
        ((config->bus == CAN_BUS_PRIMARY) && config->can_fd) ||
        (config->bit_rate_switch && !config->can_fd) ||
        (!config->can_fd &&
         (config->session.link_data_length !=
          CAN_FRAME_CLASSIC_DATA_MAX_LENGTH))) {

        return false;
    }

    const uint32_t maximum_identifier =
        config->extended_identifier
            ? CAN_FRAME_EXTENDED_ID_MAX
            : CAN_FRAME_STANDARD_ID_MAX;

    return (config->receive_identifier <= maximum_identifier) &&
           (config->transmit_identifier <= maximum_identifier);
}

static isotp_service_channel_t *isotp_service_find_channel(
    uint32_t channel_id
)
{
    for (size_t index = 0U;
         index < ISOTP_SERVICE_CHANNEL_COUNT;
         ++index) {

        if (s_channels[index].open &&
            (s_channels[index].id == channel_id)) {

            return &s_channels[index];
        }
    }

    return NULL;
}

static void isotp_service_router_callback(
    const can_event_t *event,
    void *context
)
{
    (void)context;

    if ((event == NULL) ||
        !atomic_load(&s_running) ||
        (s_queue == NULL) ||
        !isotp_service_event_relevant(event)) {

        return;
    }

    const isotp_service_command_t command = {
        .type = ISOTP_SERVICE_COMMAND_CAN_EVENT,
        .can_event = *event,
    };

    (void)xQueueSend(
        s_queue,
        &command,
        0U
    );
}

static bool isotp_service_event_relevant(
    const can_event_t *event
)
{
    for (size_t index = 0U;
         index < ISOTP_SERVICE_CHANNEL_COUNT;
         ++index) {

        const isotp_service_channel_t *channel =
            &s_channels[index];

        if (!channel->open ||
            (event->frame.bus != channel->config.bus)) {

            continue;
        }

        if (event->type == CAN_EVENT_RX) {
            if (isotp_service_frame_matches_channel(
                    &event->frame,
                    channel
                )) {

                return true;
            }
        } else if ((event->transaction_id !=
                    CAN_TRANSACTION_ID_NONE) &&
                   (event->transaction_id ==
                    channel->pending_transaction_id)) {

            return true;
        }
    }

    return false;
}

static bool isotp_service_frame_matches_channel(
    const can_frame_t *frame,
    const isotp_service_channel_t *channel
)
{
    const bool extended =
        (frame->flags & CAN_FRAME_FLAG_EXTENDED_ID) != 0U;
    const bool can_fd =
        (frame->flags & CAN_FRAME_FLAG_FD) != 0U;

    if ((frame->bus != channel->config.bus) ||
        (frame->identifier !=
         channel->config.receive_identifier) ||
        (extended != channel->config.extended_identifier) ||
        (can_fd != channel->config.can_fd) ||
        ((frame->flags & CAN_FRAME_FLAG_REMOTE) != 0U)) {

        return false;
    }

    if (channel->config.session.addressing_mode ==
        ISOTP_ADDRESSING_NORMAL) {

        return true;
    }

    return
        (frame->data_length > 0U) &&
        (frame->data[0] ==
         channel->config.session.receive_address);
}

static void isotp_service_task(
    void *context
)
{
    (void)context;

    bool stop_requested = false;

    while (!stop_requested) {
        isotp_service_command_t command = {0};

        if (xQueueReceive(
                s_queue,
                &command,
                1U
            ) == pdTRUE) {

            switch (command.type) {
                case ISOTP_SERVICE_COMMAND_CAN_EVENT:
                    isotp_service_process_can_event(
                        &command.can_event
                    );
                    break;

                case ISOTP_SERVICE_COMMAND_START_TRANSMIT:
                    isotp_service_process_start_transmit(
                        command.channel_id
                    );
                    break;

                case ISOTP_SERVICE_COMMAND_STOP:
                    stop_requested = true;
                    break;

                default:
                    break;
            }
        }

        const uint64_t now_us = esp_timer_get_time();

        for (size_t index = 0U;
             index < ISOTP_SERVICE_CHANNEL_COUNT;
             ++index) {

            isotp_service_channel_t *channel =
                &s_channels[index];

            if (!channel->open ||
                (channel->pending_transaction_id !=
                 CAN_TRANSACTION_ID_NONE)) {

                continue;
            }

            isotp_session_action_t action = {0};
            const esp_err_t result =
                isotp_session_poll(
                    &channel->session,
                    now_us,
                    &action
                );

            if ((result != ESP_OK) ||
                (action.type != ISOTP_SESSION_ACTION_NONE)) {

                isotp_service_process_action(
                    channel,
                    &action
                );
            }
        }
    }

    atomic_store(&s_running, false);
    vTaskDelete(NULL);
}

static void isotp_service_process_can_event(
    const can_event_t *event
)
{
    const uint64_t now_us = esp_timer_get_time();

    for (size_t index = 0U;
         index < ISOTP_SERVICE_CHANNEL_COUNT;
         ++index) {

        isotp_service_channel_t *channel =
            &s_channels[index];

        if (!channel->open ||
            (event->frame.bus != channel->config.bus)) {

            continue;
        }

        if (event->type == CAN_EVENT_RX) {
            if (!isotp_service_frame_matches_channel(
                    &event->frame,
                    channel
                )) {

                continue;
            }

            isotp_session_action_t action = {0};

            (void)isotp_session_receive_frame(
                &channel->session,
                event->frame.data,
                event->frame.data_length,
                now_us,
                &action
            );

            isotp_service_process_action(
                channel,
                &action
            );
            continue;
        }

        if ((event->transaction_id ==
             channel->pending_transaction_id) &&
            (channel->pending_transaction_id !=
             CAN_TRANSACTION_ID_NONE)) {

            channel->pending_transaction_id =
                CAN_TRANSACTION_ID_NONE;

            if (event->type == CAN_EVENT_TX_COMPLETED) {
                isotp_session_action_t action = {0};

                (void)isotp_session_frame_transmitted(
                    &channel->session,
                    now_us,
                    &action
                );

                isotp_service_process_action(
                    channel,
                    &action
                );
            } else {
                isotp_session_reset(&channel->session);
                channel->transmit_reserved = false;
                channel->requested_transmit_size = 0U;
                isotp_service_notify(
                    channel,
                    ISOTP_SERVICE_EVENT_ERROR,
                    NULL,
                    0U,
                    event->result,
                    ISOTP_SESSION_ERROR_NONE
                );
            }
        }
    }
}

static void isotp_service_process_start_transmit(
    uint32_t channel_id
)
{
    isotp_service_channel_t *channel =
        isotp_service_find_channel(channel_id);

    if ((channel == NULL) ||
        !channel->transmit_reserved) {

        return;
    }

    isotp_session_action_t action = {0};
    const esp_err_t result =
        isotp_session_start_transmit(
            &channel->session,
            channel->config.transmit_buffer,
            channel->requested_transmit_size,
            esp_timer_get_time(),
            &action
        );

    if (result != ESP_OK) {
        channel->transmit_reserved = false;
        isotp_service_notify(
            channel,
            ISOTP_SERVICE_EVENT_ERROR,
            NULL,
            0U,
            result,
            channel->session.error
        );
        isotp_session_reset(&channel->session);
        return;
    }

    isotp_service_process_action(
        channel,
        &action
    );
}

static void isotp_service_process_action(
    isotp_service_channel_t *channel,
    const isotp_session_action_t *action
)
{
    switch (action->type) {
        case ISOTP_SESSION_ACTION_NONE:
            break;

        case ISOTP_SESSION_ACTION_SEND_FRAME:
        {
            const esp_err_t result =
                isotp_service_send_frame(
                    channel,
                    action
                );

            if (result != ESP_OK) {
                isotp_service_notify(
                    channel,
                    ISOTP_SERVICE_EVENT_ERROR,
                    NULL,
                    0U,
                    result,
                    channel->session.error
                );
                isotp_session_reset(&channel->session);
                channel->transmit_reserved = false;
            }
            break;
        }

        case ISOTP_SESSION_ACTION_RECEIVE_COMPLETE:
        {
            const size_t message_length =
                action->message_length;

            isotp_session_reset(&channel->session);
            isotp_service_notify(
                channel,
                ISOTP_SERVICE_EVENT_RECEIVED,
                channel->config.receive_buffer,
                message_length,
                ESP_OK,
                ISOTP_SESSION_ERROR_NONE
            );
            break;
        }

        case ISOTP_SESSION_ACTION_TRANSMIT_COMPLETE:
        {
            const size_t message_length =
                action->message_length;

            channel->transmit_reserved = false;
            channel->requested_transmit_size = 0U;
            isotp_session_reset(&channel->session);
            isotp_service_notify(
                channel,
                ISOTP_SERVICE_EVENT_TRANSMITTED,
                channel->config.transmit_buffer,
                message_length,
                ESP_OK,
                ISOTP_SESSION_ERROR_NONE
            );
            break;
        }

        case ISOTP_SESSION_ACTION_ERROR:
        {
            const isotp_session_error_t session_error =
                action->error;

            channel->transmit_reserved = false;
            channel->requested_transmit_size = 0U;
            isotp_session_reset(&channel->session);
            isotp_service_notify(
                channel,
                ISOTP_SERVICE_EVENT_ERROR,
                NULL,
                0U,
                ESP_FAIL,
                session_error
            );
            break;
        }

        default:
            break;
    }
}

static esp_err_t isotp_service_send_frame(
    isotp_service_channel_t *channel,
    const isotp_session_action_t *action
)
{
    can_frame_t frame = {
        .bus = channel->config.bus,
        .identifier = channel->config.transmit_identifier,
        .flags =
            (channel->config.extended_identifier
                ? CAN_FRAME_FLAG_EXTENDED_ID : 0U) |
            (channel->config.can_fd
                ? CAN_FRAME_FLAG_FD : 0U) |
            (channel->config.bit_rate_switch
                ? CAN_FRAME_FLAG_BRS : 0U),
        .data_length = action->frame_data_length,
        .timestamp_source = CAN_TIMESTAMP_SOURCE_NONE,
    };

    uint8_t encoded_length = 0U;
    const esp_err_t dlc_result =
        can_frame_length_to_dlc(
            frame.data_length,
            channel->config.can_fd,
            &frame.dlc,
            &encoded_length
        );

    if ((dlc_result != ESP_OK) ||
        (encoded_length != frame.data_length)) {

        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(
        frame.data,
        action->frame_data,
        action->frame_data_length
    );

    return can_router_transmit(
        &frame,
        s_transmit_timeout_ms,
        &channel->pending_transaction_id
    );
}

static void isotp_service_notify(
    isotp_service_channel_t *channel,
    isotp_service_event_type_t type,
    const uint8_t *payload,
    size_t payload_length,
    esp_err_t result,
    isotp_session_error_t session_error
)
{
    if (channel->config.callback == NULL) {
        return;
    }

    const isotp_service_event_t event = {
        .type = type,
        .channel_id = channel->id,
        .payload = payload,
        .payload_length = payload_length,
        .result = result,
        .session_error = session_error,
    };

    channel->config.callback(
        &event,
        channel->config.callback_context
    );
}
