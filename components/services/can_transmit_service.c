/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "can_transmit_service.h"

#include <stdatomic.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "esp_timer.h"

#include "app_task_priorities.h"
#include "can_router.h"
#include "can_service.h"
#include "can_fd_service.h"

#define TX_CONFIRM_TIMEOUT_US  (5000000ULL)

typedef struct
{
    uint32_t transaction;
    can_event_type_t type;
    esp_err_t result;

} tx_confirmation_t;

static struct
{
    can_transmit_job_info_t info;
    uint64_t due_us;
    uint64_t submitted_us;

} s_jobs[CAN_TRANSMIT_JOB_COUNT];

static SemaphoreHandle_t s_lock;
static QueueHandle_t s_confirmations;
static TaskHandle_t s_task;
static uint32_t s_subscription;
static atomic_bool s_running;

esp_err_t can_transmit_job_validate(
    const can_transmit_job_config_t *config
)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const can_frame_t *frame =
        &config->frame;

    const bool fd =
        (frame->flags & CAN_FRAME_FLAG_FD) != 0U;

    const uint32_t max_id =
        (frame->flags & CAN_FRAME_FLAG_EXTENDED_ID)
            ? CAN_FRAME_EXTENDED_ID_MAX
            : CAN_FRAME_STANDARD_ID_MAX;

    if ((can_frame_validate(
            frame
        ) != ESP_OK) ||
        ((frame->flags &
          (CAN_FRAME_FLAG_REMOTE | CAN_FRAME_FLAG_ESI)) != 0U) ||
        ((frame->bus == CAN_BUS_PRIMARY) && fd) ||
        (!fd && (frame->dlc > 8U)) ||
        (config->interval_ms < CAN_TRANSMIT_MIN_INTERVAL_MS) ||
        (config->interval_ms > 3600000U) ||
        (config->count > 1000000U) ||
        ((config->id_step != 0U) &&
         ((config->id_end < frame->identifier) ||
          (config->id_end > max_id) ||
          (config->id_step > max_id))) ||
        (config->increment_dlc &&
         ((config->dlc_end < frame->dlc) ||
          (config->dlc_end > (fd ? 15U : 8U)))) ||
        (config->data_width > 8U) ||
        ((config->data_width != 0U) &&
         ((config->data_step == 0U) ||
          (((uint32_t)config->data_offset + config->data_width) >
           frame->data_length)))) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

void can_transmit_job_advance(
    const can_transmit_job_config_t *config,
    can_frame_t *frame
)
{
    if (config->id_step != 0U) {
        const uint64_t next =
            (uint64_t)frame->identifier + config->id_step;
        frame->identifier =
            (next > config->id_end)
                ? config->frame.identifier
                : (uint32_t)next;
    }

    uint64_t carry = config->data_step;

    for (uint8_t i = 0U; i < config->data_width; ++i) {
        const uint8_t index =
            config->data_offset +
            (config->data_big_endian
                ? config->data_width - 1U - i
                : i);

        carry += frame->data[index];
        frame->data[index] = (uint8_t)carry;
        carry >>= 8U;
    }

    if (config->increment_dlc) {
        const uint8_t old_length = frame->data_length;
        frame->dlc =
            (frame->dlc >= config->dlc_end)
                ? config->frame.dlc
                : frame->dlc + 1U;

        (void)can_frame_dlc_to_length(
            frame->dlc,
            (frame->flags & CAN_FRAME_FLAG_FD) != 0U,
            &frame->data_length
        );
        const uint8_t retained =
            (old_length < frame->data_length)
                ? old_length
                : frame->data_length;

        memset(
            frame->data + retained,
            0,
            sizeof(frame->data) - retained
        );
    }
}

static esp_err_t tx_bus_ready(
    const can_frame_t *frame
)
{
    if (!can_router_is_running()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (frame->bus == CAN_BUS_PRIMARY) {
        can_twai_driver_info_t info;

        const esp_err_t result =
            can_service_get_info(
                &info
            );

        if (result != ESP_OK) {
            return result;
        }

        const bool ready =
            info.started &&
            (info.mode == CAN_TWAI_MODE_NORMAL) &&
            ((info.state == CAN_TWAI_STATE_ERROR_ACTIVE) ||
             (info.state == CAN_TWAI_STATE_ERROR_WARNING) ||
             (info.state == CAN_TWAI_STATE_ERROR_PASSIVE));

        return ready
            ? ESP_OK
            : ESP_ERR_INVALID_STATE;
    }

    can_fd_mcp2518fd_info_t info;

    const esp_err_t result =
        can_fd_service_get_info(
            &info
        );

    if (result != ESP_OK) {
        return result;
    }

    if (!info.started ||
        (info.mode != CAN_FD_MCP2518FD_MODE_NORMAL) ||
        (info.state != CAN_FD_MCP2518FD_STATE_ERROR_ACTIVE &&
         info.state != CAN_FD_MCP2518FD_STATE_ERROR_WARNING &&
         info.state != CAN_FD_MCP2518FD_STATE_ERROR_PASSIVE) ||
        (((frame->flags & CAN_FRAME_FLAG_FD) != 0U) &&
         !info.fd_enabled) ||
        (((frame->flags & CAN_FRAME_FLAG_BRS) != 0U) &&
         !info.brs_enabled)) {
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

static void tx_confirmation(
    const can_event_t *event,
    void *context
)
{
    (void)context;

    if (!atomic_load(
            &s_running
        )) {
        return;
    }

    const tx_confirmation_t confirmation = {
        .transaction = event->transaction_id,
        .type = event->type,
        .result = event->result,
    };
    /* Loss never counts as success: a missing result times out as unknown. */
    (void)xQueueSend(
        s_confirmations,
        &confirmation,
        0U
    );

    xTaskNotifyGive(
        s_task
    );
}

static void tx_worker(
    void *context
)
{
    (void)context;

    for (;;) {
        bool work = false;

        (void)xSemaphoreTake(
            s_lock,
            portMAX_DELAY
        );

        tx_confirmation_t confirmation;

        for (uint32_t received = 0U;
             received < 32U &&
             (xQueueReceive(
                 s_confirmations,
                 &confirmation,
                 0U
              ) == pdTRUE);
             ++received) {
            for (uint32_t i = 0U; i < CAN_TRANSMIT_JOB_COUNT; ++i) {
                can_transmit_job_info_t *job =
                    &s_jobs[i].info;

                if ((job->pending_transaction == 0U) ||
                    (job->pending_transaction !=
                     confirmation.transaction)) {
                    continue;
                }

                job->pending_transaction = 0U;
                job->last_error = confirmation.result;

                if (confirmation.type == CAN_EVENT_TX_COMPLETED) {
                    ++job->completed;

                    if (job->state == CAN_TRANSMIT_ACTIVE &&
                        (job->config.count != 0U) &&
                        (job->attempts >= job->config.count)) {
                        job->state = CAN_TRANSMIT_COMPLETE;
                    }
                } else {
                    if (confirmation.type == CAN_EVENT_TX_ABORTED) {
                        ++job->aborted;
                    } else {
                        ++job->failed;
                    }
                    job->state = CAN_TRANSMIT_ERROR;
                }
            }
        }

        const uint64_t now =
            esp_timer_get_time();

        for (uint32_t i = 0U; i < CAN_TRANSMIT_JOB_COUNT; ++i) {
            can_transmit_job_info_t *job =
                &s_jobs[i].info;

            if (job->pending_transaction != 0U) {
                if ((now - s_jobs[i].submitted_us) >=
                    TX_CONFIRM_TIMEOUT_US) {
                    job->pending_transaction = 0U;
                    ++job->unknown;
                    job->last_error = ESP_ERR_TIMEOUT;
                    job->state = CAN_TRANSMIT_UNKNOWN;
                } else {
                    work = true;
                }

                continue;
            }

            if (!atomic_load(
                    &s_running
                ) ||
                (job->state != CAN_TRANSMIT_ACTIVE)) {
                continue;
            }

            work = true;

            if (now < s_jobs[i].due_us) {
                continue;
            }

            if (job->attempts == UINT32_MAX) {
                job->state = CAN_TRANSMIT_COMPLETE;
                continue;
            }

            ++job->attempts;

            esp_err_t result =
                tx_bus_ready(
                    &job->next_frame
                );

            if (result == ESP_OK) {
                result =
                    can_router_transmit(
                        &job->next_frame,
                        0U,
                        &job->pending_transaction
                    );
            }

            job->last_error = result;

            if (result != ESP_OK) {
                job->pending_transaction = 0U;
                ++job->failed;
                job->state = CAN_TRANSMIT_ERROR;
            } else {
                ++job->queued;

                s_jobs[i].submitted_us =
                    esp_timer_get_time();

                const uint64_t interval_us =
                    (uint64_t)job->config.interval_ms * 1000U;

                /*
                 * Keep the original schedule instead of adding driver and
                 * task wake-up latency to every period. Skip expired slots
                 * so a delayed job never sends a catch-up burst.
                 */
                const uint64_t elapsed_intervals =
                    (s_jobs[i].submitted_us - s_jobs[i].due_us) /
                    interval_us;

                s_jobs[i].due_us +=
                    (elapsed_intervals + 1U) * interval_us;

                can_transmit_job_advance(
                    &job->config,
                    &job->next_frame
                );
            }
        }
        (void)xSemaphoreGive(
            s_lock
        );

        TickType_t wait =
            pdMS_TO_TICKS(
                CAN_TRANSMIT_MIN_INTERVAL_MS
            );

        if (wait == 0U) {
            wait = 1U;
        }

        /* Block even when confirmations are frequent: no busy waiting. */
        if (work) {
            vTaskDelay(
                wait
            );
        } else {
            (void)ulTaskNotifyTake(
                pdTRUE,
                portMAX_DELAY
            );
        }
    }
}

esp_err_t can_transmit_service_init(
    void
)
{
    if ((s_lock != NULL) ||
        !can_router_is_running()) {
        return ESP_ERR_INVALID_STATE;
    }

    s_lock =
        xSemaphoreCreateMutex();

    s_confirmations =
        xQueueCreate(
            32U,
            sizeof(tx_confirmation_t)
        );

    if ((s_lock == NULL) ||
        (s_confirmations == NULL)) {
        if (s_lock != NULL) {
            vSemaphoreDelete(
                s_lock
            );
        }

        if (s_confirmations != NULL) {
            vQueueDelete(
                s_confirmations
            );
        }

        s_lock = NULL;
        s_confirmations = NULL;

        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(
            tx_worker,
            "can_transmit",
            4096U,
            NULL,
            APP_TASK_PRIORITY_CAN_TRANSMIT,
            &s_task
        ) != pdPASS) {
        vSemaphoreDelete(
            s_lock
        );

        vQueueDelete(
            s_confirmations
        );

        s_lock = NULL;
        s_confirmations = NULL;

        return ESP_ERR_NO_MEM;
    }

    const can_router_subscription_t subscription = {
        .bus_mask = CAN_ROUTER_ALL_BUSES_MASK,
        .event_mask = CAN_ROUTER_EVENT_MASK(CAN_EVENT_TX_COMPLETED) |
                      CAN_ROUTER_EVENT_MASK(CAN_EVENT_TX_FAILED) |
                      CAN_ROUTER_EVENT_MASK(CAN_EVENT_TX_ABORTED),
        .callback = tx_confirmation,
    };
    const esp_err_t result =
        can_router_subscribe(
            &subscription,
            &s_subscription
        );

    atomic_store(
        &s_running,
        result == ESP_OK
    );

    return result;
}

esp_err_t can_transmit_service_stop(
    void
)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_lock,
            pdMS_TO_TICKS(1000U)
        ) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    atomic_store(
        &s_running,
        false
    );

    for (uint32_t i = 0U; i < CAN_TRANSMIT_JOB_COUNT; ++i) {
        if (s_jobs[i].info.state == CAN_TRANSMIT_ACTIVE) {
            s_jobs[i].info.state = CAN_TRANSMIT_STOPPED;
        }
    }

    (void)xSemaphoreGive(
        s_lock
    );

    /* Static callback context/queue remain valid for dispatch snapshots. */
    if (s_subscription != 0U) {
        (void)can_router_unsubscribe(
            s_subscription
        );

        s_subscription = 0U;
    }

    return ESP_OK;
}

esp_err_t can_transmit_job_start(
    uint32_t slot,
    const can_transmit_job_config_t *config
)
{
    if (slot >= CAN_TRANSMIT_JOB_COUNT ||
        can_transmit_job_validate(config) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_lock,
            pdMS_TO_TICKS(1000U)
        ) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = ESP_ERR_INVALID_STATE;

    can_transmit_job_info_t *job =
        &s_jobs[slot].info;

    if (atomic_load(
            &s_running
        ) &&
        (job->state != CAN_TRANSMIT_ACTIVE) &&
        (job->pending_transaction == 0U)) {
        result =
            tx_bus_ready(
                &config->frame
            );

        if (result == ESP_OK) {
            memset(
                &s_jobs[slot],
                0,
                sizeof(s_jobs[slot])
            );

            job->config = *config;
            job->next_frame = config->frame;
            job->next_frame.timestamp_us = 0U;
            job->next_frame.timestamp_source =
                CAN_TIMESTAMP_SOURCE_NONE;
            memset(
                job->next_frame.data + job->next_frame.data_length,
                0,
                sizeof(job->next_frame.data) -
                    job->next_frame.data_length
            );
            s_jobs[slot].due_us =
                esp_timer_get_time();

            job->state = CAN_TRANSMIT_ACTIVE;
        }
    }

    (void)xSemaphoreGive(
        s_lock
    );

    if (result == ESP_OK) {
        xTaskNotifyGive(
            s_task
        );
    }

    return result;
}

esp_err_t can_transmit_job_stop(
    uint32_t slot
)
{
    if (slot >= CAN_TRANSMIT_JOB_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_lock,
            pdMS_TO_TICKS(1000U)
        ) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_jobs[slot].info.state == CAN_TRANSMIT_ACTIVE) {
        s_jobs[slot].info.state = CAN_TRANSMIT_STOPPED;
    }
    (void)xSemaphoreGive(
        s_lock
    );

    return ESP_OK;
}

esp_err_t can_transmit_job_get(
    uint32_t slot,
    can_transmit_job_info_t *info
)
{
    if ((slot >= CAN_TRANSMIT_JOB_COUNT) ||
        (info == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((s_lock == NULL) ||
        !atomic_load(
            &s_running
        )) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_lock,
            pdMS_TO_TICKS(1000U)
        ) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    *info = s_jobs[slot].info;

    (void)xSemaphoreGive(
        s_lock
    );

    return ESP_OK;
}
