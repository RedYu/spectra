#include "can_router.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "app_task_priorities.h"
#include "can_service.h"
#include "can_fd_service.h"
#include "can_router_service_adapter.h"

#define CAN_ROUTER_TASK_STACK_SIZE       (4096U)
#define CAN_ROUTER_TASK_PRIORITY \
    APP_TASK_PRIORITY_CAN_ROUTER
#define CAN_ROUTER_STOP_TIMEOUT_MS       (2000U)
#define CAN_ROUTER_LOCK_TIMEOUT_MS       (100U)

_Static_assert(
    CAN_BUS_COUNT <= 32,
    "CAN router bus mask supports at most 32 buses"
);

_Static_assert(
    CAN_EVENT_TYPE_COUNT <= 32,
    "CAN router event mask supports at most 32 event types"
);

typedef struct
{
    bool used;
    uint32_t id;
    can_router_subscription_t subscription;

} can_router_subscriber_slot_t;

typedef struct
{
    bool used;

    /*
     * True while the CAN service transmit function is still executing
     * and native_sequence has not necessarily been returned yet.
     */
    bool submission_in_progress;

    /*
     * A confirmation arrived before submission completed.
     */
    bool confirmation_pending;

    can_bus_id_t bus;

    uint32_t transaction_id;
    uint32_t native_sequence;

    can_frame_t frame;

    can_event_type_t confirmation_event_type;
    esp_err_t confirmation_result;

    uint64_t confirmation_timestamp_us;
    can_timestamp_source_t confirmation_timestamp_source;

} can_router_pending_tx_t;

static const char *TAG =
    "can_router";

static TaskHandle_t s_task = NULL;

static QueueHandle_t s_event_queue = NULL;
static SemaphoreHandle_t s_api_mutex = NULL;
static SemaphoreHandle_t s_task_stopped = NULL;

static can_router_subscriber_slot_t *s_subscribers = NULL;
static can_router_subscriber_slot_t *s_dispatch_snapshot = NULL;
static can_router_pending_tx_t *s_pending_tx = NULL;

static SemaphoreHandle_t s_tx_mutex[
    CAN_BUS_COUNT
];

static uint32_t s_queue_capacity = 0U;
static uint32_t s_subscriber_capacity = 0U;
static uint32_t s_pending_tx_capacity = 0U;

static uint32_t s_next_subscription_id =
    CAN_ROUTER_SUBSCRIPTION_ID_NONE + 1U;

static uint32_t s_next_transaction_id =
    CAN_TRANSACTION_ID_NONE + 1U;

static uint32_t s_next_event_sequence =
    CAN_EVENT_SEQUENCE_INITIAL;

static atomic_bool s_running =
    ATOMIC_VAR_INIT(false);

static atomic_bool s_stop_in_progress =
    ATOMIC_VAR_INIT(false);

static atomic_bool s_stop_requested =
    ATOMIC_VAR_INIT(false);

static atomic_uint_fast32_t s_queue_peak =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast32_t s_pending_tx_current =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast32_t s_pending_tx_peak =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast32_t s_registered_subscribers =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast32_t s_received_frames =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast32_t s_transmitted_frames =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast32_t s_completed_transmissions =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast32_t s_failed_transmissions =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast32_t s_aborted_transmissions =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast32_t s_dispatched_events =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast32_t s_dropped_events =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast32_t s_unmatched_tx_confirmations =
    ATOMIC_VAR_INIT(0U);

static esp_err_t can_router_lock(
    TickType_t timeout_ticks
)
{
    if (s_api_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_api_mutex,
            timeout_ticks
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void can_router_unlock(void)
{
    if (s_api_mutex != NULL) {
        (void)xSemaphoreGive(
            s_api_mutex
        );
    }
}

static esp_err_t can_router_lock_bus(
    can_bus_id_t bus
)
{
    if ((bus < CAN_BUS_PRIMARY) ||
        (bus >= CAN_BUS_COUNT)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (s_tx_mutex[bus] == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_tx_mutex[bus],
            portMAX_DELAY
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void can_router_unlock_bus(
    can_bus_id_t bus
)
{
    if ((bus >= CAN_BUS_PRIMARY) &&
        (bus < CAN_BUS_COUNT) &&
        (s_tx_mutex[bus] != NULL)) {

        (void)xSemaphoreGive(
            s_tx_mutex[bus]
        );
    }
}

static void can_router_update_atomic_peak(
    atomic_uint_fast32_t *peak,
    uint32_t current
)
{
    uint_fast32_t observed =
        atomic_load_explicit(
            peak,
            memory_order_relaxed
        );

    while ((uint_fast32_t)current > observed) {
        if (atomic_compare_exchange_weak_explicit(
                peak,
                &observed,
                (uint_fast32_t)current,
                memory_order_relaxed,
                memory_order_relaxed
            )) {

            break;
        }
    }
}

static void can_router_update_queue_peak(void)
{
    if (s_event_queue == NULL) {
        return;
    }

    const uint32_t current =
        (uint32_t)uxQueueMessagesWaiting(
            s_event_queue
        );

    can_router_update_atomic_peak(
        &s_queue_peak,
        current
    );
}

static esp_err_t can_router_queue_event(
    const can_event_t *event
)
{
    if ((event == NULL) ||
        (s_event_queue == NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    if (xQueueSend(
            s_event_queue,
            event,
            0U
        ) != pdTRUE) {

        (void)atomic_fetch_add_explicit(
            &s_dropped_events,
            1U,
            memory_order_relaxed
        );

        return ESP_ERR_TIMEOUT;
    }

    can_router_update_queue_peak();

    return ESP_OK;
}

static uint32_t can_router_next_nonzero_id(
    uint32_t *next_id
)
{
    uint32_t id =
        *next_id;

    if (id == 0U) {
        id = 1U;
    }

    *next_id =
        id + 1U;

    if (*next_id == 0U) {
        *next_id = 1U;
    }

    return id;
}

static uint32_t can_router_next_event_sequence(void)
{
    uint32_t sequence =
        s_next_event_sequence;

    ++s_next_event_sequence;

    if (s_next_event_sequence == 0U) {
        s_next_event_sequence =
            CAN_EVENT_SEQUENCE_INITIAL;
    }

    return sequence;
}

static void can_router_dispatch_event(
    can_event_t *event
)
{
    if (event == NULL) {
        return;
    }

    event->event_sequence =
        can_router_next_event_sequence();

    uint32_t snapshot_count = 0U;

    if (can_router_lock(
            portMAX_DELAY
        ) == ESP_OK) {

        for (uint32_t index = 0U;
             index < s_subscriber_capacity;
             ++index) {

            if (!s_subscribers[index].used) {
                continue;
            }

            s_dispatch_snapshot[snapshot_count] =
                s_subscribers[index];

            ++snapshot_count;
        }

        can_router_unlock();
    }

    const uint32_t bus_mask =
        CAN_ROUTER_BUS_MASK(
            event->frame.bus
        );

    const uint32_t event_mask =
        CAN_ROUTER_EVENT_MASK(
            event->type
        );

    for (uint32_t index = 0U;
         index < snapshot_count;
         ++index) {

        const can_router_subscription_t *subscription =
            &s_dispatch_snapshot[index].subscription;

        if (((subscription->bus_mask &
              bus_mask) == 0U) ||
            ((subscription->event_mask &
              event_mask) == 0U)) {

            continue;
        }

        subscription->callback(
            event,
            subscription->context
        );
    }

    (void)atomic_fetch_add_explicit(
        &s_dispatched_events,
        1U,
        memory_order_relaxed
    );
}

static void can_router_task(
    void *argument
)
{
    (void)argument;

    ESP_LOGI(
        TAG,
        "CAN router task started"
    );

    while (!atomic_load(
               &s_stop_requested
           )) {

        can_event_t event;

        if (xQueueReceive(
                s_event_queue,
                &event,
                pdMS_TO_TICKS(50U)
            ) != pdTRUE) {

            continue;
        }

        can_router_dispatch_event(
            &event
        );
    }

    ESP_LOGI(
        TAG,
        "CAN router task stopped"
    );

    if (s_task_stopped != NULL) {
        (void)xSemaphoreGive(
            s_task_stopped
        );
    }

    vTaskDelete(NULL);
}

static void can_router_reset_statistics_internal(void)
{
    atomic_store_explicit(
        &s_queue_peak,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_pending_tx_current,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_pending_tx_peak,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_registered_subscribers,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_received_frames,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_transmitted_frames,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_completed_transmissions,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_failed_transmissions,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_aborted_transmissions,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_dispatched_events,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_dropped_events,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_unmatched_tx_confirmations,
        0U,
        memory_order_relaxed
    );
}

static void can_router_release_runtime_resources(void)
{
    if (s_event_queue != NULL) {
        vQueueDelete(
            s_event_queue
        );

        s_event_queue = NULL;
    }

    if (s_task_stopped != NULL) {
        vSemaphoreDelete(
            s_task_stopped
        );

        s_task_stopped = NULL;
    }

    free(
        s_subscribers
    );

    s_subscribers = NULL;

    free(
        s_dispatch_snapshot
    );

    s_dispatch_snapshot = NULL;

    free(
        s_pending_tx
    );

    s_pending_tx = NULL;

    s_queue_capacity = 0U;
    s_subscriber_capacity = 0U;
    s_pending_tx_capacity = 0U;
}

esp_err_t can_router_start(
    const can_router_config_t *config
)
{
    if ((config == NULL) ||
        (config->queue_depth == 0U) ||
        (config->subscriber_capacity == 0U) ||
        (config->pending_tx_capacity == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (s_api_mutex == NULL) {
        s_api_mutex =
            xSemaphoreCreateMutex();

        if (s_api_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t result =
        can_router_lock(
            portMAX_DELAY
        );

    if (result != ESP_OK) {
        return result;
    }

    if (atomic_load(
            &s_running
        ) ||
        (s_task != NULL)) {

        can_router_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    s_subscribers =
        calloc(
            config->subscriber_capacity,
            sizeof(*s_subscribers)
        );

    s_dispatch_snapshot =
        calloc(
            config->subscriber_capacity,
            sizeof(*s_dispatch_snapshot)
        );

    s_pending_tx =
        calloc(
            config->pending_tx_capacity,
            sizeof(*s_pending_tx)
        );

    if ((s_subscribers == NULL) ||
        (s_dispatch_snapshot == NULL) ||
        (s_pending_tx == NULL)) {

        can_router_release_runtime_resources();
        can_router_unlock();

        return ESP_ERR_NO_MEM;
    }

    s_event_queue =
        xQueueCreate(
            (UBaseType_t)config->queue_depth,
            sizeof(can_event_t)
        );

    if (s_event_queue == NULL) {
        can_router_release_runtime_resources();
        can_router_unlock();

        return ESP_ERR_NO_MEM;
    }

    s_task_stopped =
        xSemaphoreCreateBinary();

    if (s_task_stopped == NULL) {
        can_router_release_runtime_resources();
        can_router_unlock();

        return ESP_ERR_NO_MEM;
    }

    s_queue_capacity =
        config->queue_depth;

    s_subscriber_capacity =
        config->subscriber_capacity;

    s_pending_tx_capacity =
        config->pending_tx_capacity;

    s_next_subscription_id =
        CAN_ROUTER_SUBSCRIPTION_ID_NONE + 1U;

    s_next_transaction_id =
        CAN_TRANSACTION_ID_NONE + 1U;

    s_next_event_sequence =
        CAN_EVENT_SEQUENCE_INITIAL;

    can_router_reset_statistics_internal();

    atomic_store(
        &s_stop_requested,
        false
    );

    atomic_store(
        &s_stop_in_progress,
        false
    );

    for (uint32_t bus = 0U;
         bus < (uint32_t)CAN_BUS_COUNT;
         ++bus) {

        if (s_tx_mutex[bus] == NULL) {
            s_tx_mutex[bus] =
                xSemaphoreCreateMutex();
        }

        if (s_tx_mutex[bus] == NULL) {
            can_router_release_runtime_resources();
            can_router_unlock();

            return ESP_ERR_NO_MEM;
        }
    }

    const BaseType_t task_result =
        xTaskCreate(
            can_router_task,
            "can_router",
            CAN_ROUTER_TASK_STACK_SIZE,
            NULL,
            CAN_ROUTER_TASK_PRIORITY,
            &s_task
        );

    if (task_result != pdPASS) {
        s_task = NULL;

        can_router_release_runtime_resources();
        can_router_unlock();

        return ESP_ERR_NO_MEM;
    }

    atomic_store(
        &s_running,
        true
    );

    ESP_LOGI(
        TAG,
        "CAN router started: queue=%lu, subscribers=%lu, pending TX=%lu",
        (unsigned long)s_queue_capacity,
        (unsigned long)s_subscriber_capacity,
        (unsigned long)s_pending_tx_capacity
    );

    can_router_unlock();

    return ESP_OK;
}

static esp_err_t can_router_lock_all_buses(void)
{
    for (uint32_t bus = 0U;
         bus < (uint32_t)CAN_BUS_COUNT;
         ++bus) {

        const esp_err_t result =
            can_router_lock_bus(
                (can_bus_id_t)bus
            );

        if (result != ESP_OK) {
            for (uint32_t unlock_bus = 0U;
                 unlock_bus < bus;
                 ++unlock_bus) {

                can_router_unlock_bus(
                    (can_bus_id_t)unlock_bus
                );
            }

            return result;
        }
    }

    return ESP_OK;
}

static void can_router_unlock_all_buses(void)
{
    for (uint32_t bus =
             (uint32_t)CAN_BUS_COUNT;
         bus > 0U;
         --bus) {

        can_router_unlock_bus(
            (can_bus_id_t)(bus - 1U)
        );
    }
}

esp_err_t can_router_stop(void)
{
    esp_err_t result =
        can_router_lock(
            portMAX_DELAY
        );

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(
            &s_running
        ) ||
        (s_task == NULL) ||
        (s_task_stopped == NULL)) {

        can_router_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    bool expected = false;

    if (!atomic_compare_exchange_strong(
            &s_stop_in_progress,
            &expected,
            true
        )) {

        can_router_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    atomic_store(
        &s_stop_requested,
        true
    );

    can_router_unlock();

    result =
        can_router_lock_all_buses();

    if (result != ESP_OK) {
        atomic_store(
            &s_stop_in_progress,
            false
        );

        return result;
    }

    if (xSemaphoreTake(
            s_task_stopped,
            pdMS_TO_TICKS(
                CAN_ROUTER_STOP_TIMEOUT_MS
            )
        ) != pdTRUE) {

        can_router_unlock_all_buses();

        atomic_store(
            &s_stop_in_progress,
            false
        );

        ESP_LOGE(
            TAG,
            "CAN router task did not stop"
        );

        return ESP_ERR_TIMEOUT;
    }

    result =
        can_router_lock(
            portMAX_DELAY
        );

    if (result != ESP_OK) {
        can_router_unlock_all_buses();

        atomic_store(
            &s_stop_in_progress,
            false
        );

        return result;
    }

    s_task = NULL;

    atomic_store(
        &s_running,
        false
    );

    can_router_release_runtime_resources();

    atomic_store(
        &s_stop_requested,
        false
    );

    atomic_store(
        &s_stop_in_progress,
        false
    );

    ESP_LOGI(
        TAG,
        "CAN router stopped"
    );

    can_router_unlock();

    can_router_unlock_all_buses();

    return ESP_OK;
}

bool can_router_is_running(void)
{
    return atomic_load(
        &s_running
    );
}

static bool can_router_subscription_id_used(
    uint32_t subscription_id
)
{
    for (uint32_t index = 0U;
         index < s_subscriber_capacity;
         ++index) {

        if (s_subscribers[index].used &&
            (s_subscribers[index].id ==
             subscription_id)) {

            return true;
        }
    }

    return false;
}

static uint32_t can_router_allocate_subscription_id(void)
{
    for (uint32_t attempt = 0U;
         attempt <= s_subscriber_capacity;
         ++attempt) {

        const uint32_t candidate =
            can_router_next_nonzero_id(
                &s_next_subscription_id
            );

        if (!can_router_subscription_id_used(
                candidate
            )) {

            return candidate;
        }
    }

    return CAN_ROUTER_SUBSCRIPTION_ID_NONE;
}

esp_err_t can_router_subscribe(
    const can_router_subscription_t *subscription,
    uint32_t *subscription_id
)
{
    if ((subscription == NULL) ||
        (subscription_id == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *subscription_id =
        CAN_ROUTER_SUBSCRIPTION_ID_NONE;

    if ((subscription->callback == NULL) ||
        (subscription->bus_mask == 0U) ||
        (subscription->event_mask == 0U) ||
        ((subscription->bus_mask &
          ~((uint32_t)CAN_ROUTER_ALL_BUSES_MASK)) != 0U) ||
        ((subscription->event_mask &
          ~((uint32_t)CAN_ROUTER_ALL_EVENTS_MASK)) != 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        can_router_lock(
            portMAX_DELAY
        );

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(
            &s_running
        ) ||
        atomic_load(
            &s_stop_requested
        ) ||
        atomic_load(
            &s_stop_in_progress
        )) {

        can_router_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t free_index =
        s_subscriber_capacity;

    for (uint32_t index = 0U;
         index < s_subscriber_capacity;
         ++index) {

        if (!s_subscribers[index].used) {
            free_index = index;
            break;
        }
    }

    if (free_index >=
        s_subscriber_capacity) {

        can_router_unlock();
        return ESP_ERR_NO_MEM;
    }

    const uint32_t id =
        can_router_allocate_subscription_id();

    if (id ==
        CAN_ROUTER_SUBSCRIPTION_ID_NONE) {

        can_router_unlock();
        return ESP_ERR_NO_MEM;
    }

    s_subscribers[free_index].subscription =
        *subscription;

    s_subscribers[free_index].id =
        id;

    s_subscribers[free_index].used =
        true;

    (void)atomic_fetch_add_explicit(
        &s_registered_subscribers,
        1U,
        memory_order_relaxed
    );

    *subscription_id =
        id;

    can_router_unlock();

    return ESP_OK;
}

esp_err_t can_router_unsubscribe(
    uint32_t subscription_id
)
{
    if (subscription_id ==
        CAN_ROUTER_SUBSCRIPTION_ID_NONE) {

        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        can_router_lock(
            portMAX_DELAY
        );

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(
            &s_running
        ) ||
        atomic_load(
            &s_stop_requested
        ) ||
        atomic_load(
            &s_stop_in_progress
        )) {

        can_router_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    for (uint32_t index = 0U;
         index < s_subscriber_capacity;
         ++index) {

        if (!s_subscribers[index].used ||
            (s_subscribers[index].id !=
             subscription_id)) {

            continue;
        }

        memset(
            &s_subscribers[index],
            0,
            sizeof(s_subscribers[index])
        );

        (void)atomic_fetch_sub_explicit(
            &s_registered_subscribers,
            1U,
            memory_order_relaxed
        );

        can_router_unlock();

        return ESP_OK;
    }

    can_router_unlock();

    return ESP_ERR_NOT_FOUND;
}

void can_router_receive_callback(
    const can_frame_t *frame,
    void *context
)
{
    (void)context;

    if (frame == NULL) {
        (void)atomic_fetch_add_explicit(
            &s_dropped_events,
            1U,
            memory_order_relaxed
        );

        return;
    }

    if (!atomic_load(
            &s_running
        ) ||
        atomic_load(
            &s_stop_requested
        ) ||
        atomic_load(
            &s_stop_in_progress
        )) {

        (void)atomic_fetch_add_explicit(
            &s_dropped_events,
            1U,
            memory_order_relaxed
        );

        return;
    }

    const esp_err_t validation_result =
        can_frame_validate(
            frame
        );

    if (validation_result != ESP_OK) {
        (void)atomic_fetch_add_explicit(
            &s_dropped_events,
            1U,
            memory_order_relaxed
        );

        ESP_LOGW(
            TAG,
            "Rejected invalid RX frame: bus=%d, ID=0x%lX, error=%s",
            (int)frame->bus,
            (unsigned long)frame->identifier,
            esp_err_to_name(validation_result)
        );

        return;
    }

    can_event_t event;

    memset(
        &event,
        0,
        sizeof(event)
    );

    event.type =
        CAN_EVENT_RX;

    event.direction =
        CAN_FRAME_DIRECTION_RX;

    event.frame =
        *frame;

    /*
     * event_sequence is assigned by the router task immediately before
     * dispatch.
     */
    event.event_sequence = 0U;

    event.transaction_id =
        CAN_TRANSACTION_ID_NONE;

    event.native_sequence =
        CAN_NATIVE_SEQUENCE_NONE;

    event.result =
        ESP_OK;

    (void)atomic_fetch_add_explicit(
        &s_received_frames,
        1U,
        memory_order_relaxed
    );

    (void)can_router_queue_event(
        &event
    );
}

static can_router_pending_tx_t *
can_router_find_free_pending_tx(void)
{
    for (uint32_t index = 0U;
         index < s_pending_tx_capacity;
         ++index) {

        if (!s_pending_tx[index].used) {
            return &s_pending_tx[index];
        }
    }

    return NULL;
}

static can_router_pending_tx_t *
can_router_find_pending_tx_by_transaction(
    uint32_t transaction_id
)
{
    for (uint32_t index = 0U;
         index < s_pending_tx_capacity;
         ++index) {

        if (s_pending_tx[index].used &&
            (s_pending_tx[index].transaction_id ==
             transaction_id)) {

            return &s_pending_tx[index];
        }
    }

    return NULL;
}

static can_router_pending_tx_t *
can_router_find_pending_tx_by_native_sequence(
    can_bus_id_t bus,
    uint32_t native_sequence
)
{
    for (uint32_t index = 0U;
         index < s_pending_tx_capacity;
         ++index) {

        if (s_pending_tx[index].used &&
            !s_pending_tx[index].submission_in_progress &&
            (s_pending_tx[index].bus == bus) &&
            (s_pending_tx[index].native_sequence ==
             native_sequence)) {

            return &s_pending_tx[index];
        }
    }

    return NULL;
}

static can_router_pending_tx_t *
can_router_find_submitting_tx(
    can_bus_id_t bus
)
{
    for (uint32_t index = 0U;
         index < s_pending_tx_capacity;
         ++index) {

        if (s_pending_tx[index].used &&
            s_pending_tx[index].submission_in_progress &&
            (s_pending_tx[index].bus == bus)) {

            return &s_pending_tx[index];
        }
    }

    return NULL;
}

static bool can_router_transaction_id_used(
    uint32_t transaction_id
)
{
    return can_router_find_pending_tx_by_transaction(
               transaction_id
           ) != NULL;
}

static uint32_t can_router_allocate_transaction_id(void)
{
    for (uint32_t attempt = 0U;
         attempt <= s_pending_tx_capacity;
         ++attempt) {

        const uint32_t candidate =
            can_router_next_nonzero_id(
                &s_next_transaction_id
            );

        if (!can_router_transaction_id_used(
                candidate
            )) {

            return candidate;
        }
    }

    return CAN_TRANSACTION_ID_NONE;
}

static void can_router_release_pending_tx(
    can_router_pending_tx_t *pending
)
{
    if ((pending == NULL) ||
        !pending->used) {

        return;
    }

    memset(
        pending,
        0,
        sizeof(*pending)
    );

    (void)atomic_fetch_sub_explicit(
        &s_pending_tx_current,
        1U,
        memory_order_relaxed
    );
}

static void can_router_initialize_tx_event(
    can_event_t *event,
    can_event_type_t type,
    const can_router_pending_tx_t *pending,
    esp_err_t result,
    uint64_t timestamp_us,
    can_timestamp_source_t timestamp_source
)
{
    memset(
        event,
        0,
        sizeof(*event)
    );

    event->type =
        type;

    event->direction =
        CAN_FRAME_DIRECTION_TX;

    event->frame =
        pending->frame;

    event->frame.timestamp_us =
        timestamp_us;

    event->frame.timestamp_source =
        timestamp_source;

    event->event_sequence = 0U;

    event->transaction_id =
        pending->transaction_id;

    event->native_sequence =
        pending->native_sequence;

    event->result =
        result;
}

static void can_router_complete_pending_tx(
    can_router_pending_tx_t *pending,
    can_event_type_t event_type,
    esp_err_t result,
    uint64_t timestamp_us,
    can_timestamp_source_t timestamp_source
)
{
    if ((pending == NULL) ||
        !pending->used) {

        return;
    }

    can_event_t event;

    can_router_initialize_tx_event(
        &event,
        event_type,
        pending,
        result,
        timestamp_us,
        timestamp_source
    );

    switch (event_type) {
        case CAN_EVENT_TX_COMPLETED:
            (void)atomic_fetch_add_explicit(
                &s_completed_transmissions,
                1U,
                memory_order_relaxed
            );
            break;

        case CAN_EVENT_TX_FAILED:
            (void)atomic_fetch_add_explicit(
                &s_failed_transmissions,
                1U,
                memory_order_relaxed
            );
            break;

        case CAN_EVENT_TX_ABORTED:
            (void)atomic_fetch_add_explicit(
                &s_aborted_transmissions,
                1U,
                memory_order_relaxed
            );
            break;

        default:
            return;
    }

    /*
     * Release the slot before dispatch. The event already contains a
     * complete copy of the pending transmission.
     */
    can_router_release_pending_tx(
        pending
    );

    (void)can_router_queue_event(
        &event
    );
}

esp_err_t can_router_transmit(
    const can_frame_t *frame,
    uint32_t timeout_ms,
    uint32_t *transaction_id
)
{
    if ((frame == NULL) ||
        (transaction_id == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *transaction_id =
        CAN_TRANSACTION_ID_NONE;

    const esp_err_t validation_result =
        can_frame_validate(
            frame
        );

    if (validation_result != ESP_OK) {
        return validation_result;
    }

    if (!atomic_load(
            &s_running
        ) ||
        atomic_load(
            &s_stop_requested
        ) ||
        atomic_load(
            &s_stop_in_progress
        )) {

        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result =
        can_router_lock_bus(
            frame->bus
        );

    if (result != ESP_OK) {
        return result;
    }

    result =
        can_router_lock(
            portMAX_DELAY
        );

    if (result != ESP_OK) {
        can_router_unlock_bus(
            frame->bus
        );

        return result;
    }

    if (!atomic_load(
            &s_running
        ) ||
        atomic_load(
            &s_stop_requested
        ) ||
        atomic_load(
            &s_stop_in_progress
        )) {

        can_router_unlock();
        can_router_unlock_bus(
            frame->bus
        );

        return ESP_ERR_INVALID_STATE;
    }

    can_router_pending_tx_t *pending =
        can_router_find_free_pending_tx();

    if (pending == NULL) {
        can_router_unlock();
        can_router_unlock_bus(
            frame->bus
        );

        return ESP_ERR_NO_MEM;
    }

    const uint32_t assigned_transaction_id =
        can_router_allocate_transaction_id();

    if (assigned_transaction_id ==
        CAN_TRANSACTION_ID_NONE) {

        can_router_unlock();
        can_router_unlock_bus(
            frame->bus
        );

        return ESP_ERR_NO_MEM;
    }

    memset(
        pending,
        0,
        sizeof(*pending)
    );

    pending->used = true;
    pending->submission_in_progress = true;
    pending->confirmation_pending = false;

    pending->bus =
        frame->bus;

    pending->transaction_id =
        assigned_transaction_id;

    pending->native_sequence =
        CAN_NATIVE_SEQUENCE_NONE;

    pending->frame =
        *frame;

    const uint32_t pending_current =
        (uint32_t)atomic_fetch_add_explicit(
            &s_pending_tx_current,
            1U,
            memory_order_relaxed
        ) + 1U;

    can_router_update_atomic_peak(
        &s_pending_tx_peak,
        pending_current
    );

    can_router_unlock();

    uint32_t native_sequence =
        CAN_NATIVE_SEQUENCE_NONE;

    switch (frame->bus) {
        case CAN_BUS_PRIMARY:
            result =
                can_service_transmit_common_tracked(
                    frame,
                    NULL,
                    timeout_ms,
                    &native_sequence
                );
            break;

        case CAN_BUS_SECONDARY:
            result =
                can_fd_service_transmit_common_tracked(
                    frame,
                    timeout_ms,
                    &native_sequence
                );
            break;

        default:
            result =
                ESP_ERR_INVALID_ARG;
            break;
    }

    const esp_err_t lock_result =
        can_router_lock(
            portMAX_DELAY
        );

    if (lock_result != ESP_OK) {
        can_router_unlock_bus(
            frame->bus
        );

        return lock_result;
    }

    /*
     * Locate the entry again instead of relying on a pointer across an
     * unlocked section.
     */
    pending =
        can_router_find_pending_tx_by_transaction(
            assigned_transaction_id
        );

    if (pending == NULL) {
        can_router_unlock();
        can_router_unlock_bus(
            frame->bus
        );

        return ESP_ERR_INVALID_STATE;
    }

    if (result != ESP_OK) {
        can_router_release_pending_tx(
            pending
        );

        can_router_unlock();
        can_router_unlock_bus(
            frame->bus
        );

        return result;
    }

    /*
     * An early confirmation may already have supplied the sequence.
     */
    if (pending->confirmation_pending) {
        if (pending->native_sequence !=
            native_sequence) {

            ESP_LOGW(
                TAG,
                "Early TX sequence mismatch: bus=%d, returned=%lu, "
                "confirmed=%lu",
                (int)frame->bus,
                (unsigned long)native_sequence,
                (unsigned long)pending->native_sequence
            );
        }
    } else {
        pending->native_sequence =
            native_sequence;
    }

    pending->submission_in_progress =
        false;

    can_event_t queued_event;

    can_router_initialize_tx_event(
        &queued_event,
        CAN_EVENT_TX_QUEUED,
        pending,
        ESP_OK,
        0U,
        CAN_TIMESTAMP_SOURCE_NONE
    );

    (void)atomic_fetch_add_explicit(
        &s_transmitted_frames,
        1U,
        memory_order_relaxed
    );

    /*
     * Queue TX_QUEUED before processing an early confirmation so
     * subscribers always observe the correct lifecycle order.
     */
    (void)can_router_queue_event(
        &queued_event
    );

    *transaction_id =
        assigned_transaction_id;

    if (pending->confirmation_pending) {
        can_router_complete_pending_tx(
            pending,
            pending->confirmation_event_type,
            pending->confirmation_result,
            pending->confirmation_timestamp_us,
            pending->confirmation_timestamp_source
        );
    }

    can_router_unlock();
    can_router_unlock_bus(
        frame->bus
    );

    return ESP_OK;
}

esp_err_t can_router_report_tx_result(
    can_bus_id_t bus,
    uint32_t native_sequence,
    can_event_type_t event_type,
    esp_err_t result,
    uint64_t timestamp_us,
    can_timestamp_source_t timestamp_source
)
{
    if ((bus < CAN_BUS_PRIMARY) ||
        (bus >= CAN_BUS_COUNT) ||
        (native_sequence ==
         CAN_NATIVE_SEQUENCE_NONE)) {

        return ESP_ERR_INVALID_ARG;
    }

    if ((event_type !=
         CAN_EVENT_TX_COMPLETED) &&
        (event_type !=
         CAN_EVENT_TX_FAILED) &&
        (event_type !=
         CAN_EVENT_TX_ABORTED)) {

        return ESP_ERR_INVALID_ARG;
    }

    if ((timestamp_source <
         CAN_TIMESTAMP_SOURCE_NONE) ||
        (timestamp_source >
         CAN_TIMESTAMP_SOURCE_HARDWARE)) {

        return ESP_ERR_INVALID_ARG;
    }

    if ((timestamp_source ==
         CAN_TIMESTAMP_SOURCE_NONE) &&
        (timestamp_us != 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    /*
     * Successful completion must use ESP_OK. Failed and aborted
     * transmissions must carry an error describing the reason.
     */
    if (((event_type ==
          CAN_EVENT_TX_COMPLETED) &&
         (result != ESP_OK)) ||
        (((event_type ==
           CAN_EVENT_TX_FAILED) ||
          (event_type ==
           CAN_EVENT_TX_ABORTED)) &&
         (result == ESP_OK))) {

        return ESP_ERR_INVALID_ARG;
    }

    TickType_t lock_timeout =
        pdMS_TO_TICKS(
            CAN_ROUTER_LOCK_TIMEOUT_MS
        );

    if (lock_timeout == 0U) {
        lock_timeout = 1U;
    }

    esp_err_t lock_result =
        can_router_lock(
            lock_timeout
        );

    if (lock_result != ESP_OK) {
        /*
         * A hardware confirmation normally occurs only once. Record
         * the loss explicitly if router synchronization is unavailable.
         */
        (void)atomic_fetch_add_explicit(
            &s_dropped_events,
            1U,
            memory_order_relaxed
        );

        return lock_result;
    }

    if (!atomic_load(
            &s_running
        ) ||
        atomic_load(
            &s_stop_requested
        ) ||
        atomic_load(
            &s_stop_in_progress
        )) {

        can_router_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    can_router_pending_tx_t *pending =
        can_router_find_pending_tx_by_native_sequence(
            bus,
            native_sequence
        );

    if (pending != NULL) {
        can_router_complete_pending_tx(
            pending,
            event_type,
            result,
            timestamp_us,
            timestamp_source
        );

        can_router_unlock();

        return ESP_OK;
    }

    /*
     * The controller may complete a very short transmission before
     * can_router_transmit() returns from the hardware-specific service.
     * Since bus submissions are serialized, at most one provisional
     * pending entry can exist for a given bus.
     */
    pending =
        can_router_find_submitting_tx(
            bus
        );

    if (pending != NULL) {
        if (pending->confirmation_pending) {
            /*
             * A second confirmation for the same in-progress
             * transmission is unexpected.
             */
            (void)atomic_fetch_add_explicit(
                &s_unmatched_tx_confirmations,
                1U,
                memory_order_relaxed
            );

            can_router_unlock();

            return ESP_ERR_INVALID_STATE;
        }

        pending->native_sequence =
            native_sequence;

        pending->confirmation_pending =
            true;

        pending->confirmation_event_type =
            event_type;

        pending->confirmation_result =
            result;

        pending->confirmation_timestamp_us =
            timestamp_us;

        pending->confirmation_timestamp_source =
            timestamp_source;

        can_router_unlock();

        return ESP_OK;
    }

    (void)atomic_fetch_add_explicit(
        &s_unmatched_tx_confirmations,
        1U,
        memory_order_relaxed
    );

    can_router_unlock();

    return ESP_ERR_NOT_FOUND;
}

void can_router_twai_tx_confirmation_callback(
    const can_twai_tx_confirmation_t *confirmation,
    void *context
)
{
    (void)context;

    if (confirmation == NULL) {
        return;
    }

    const can_event_type_t event_type =
        confirmation->successful
            ? CAN_EVENT_TX_COMPLETED
            : CAN_EVENT_TX_FAILED;

    const esp_err_t result =
        confirmation->successful
            ? ESP_OK
            : ESP_FAIL;

    const esp_err_t report_result =
        can_router_report_tx_result(
            CAN_BUS_PRIMARY,
            confirmation->transmission_id,
            event_type,
            result,
            0U,
            CAN_TIMESTAMP_SOURCE_NONE
        );

    if ((report_result != ESP_OK) &&
        (report_result != ESP_ERR_NOT_FOUND) &&
        (report_result != ESP_ERR_INVALID_STATE)) {

        ESP_LOGW(
            TAG,
            "Failed to report TWAI TX confirmation: ID=%lu, error=%s",
            (unsigned long)confirmation->transmission_id,
            esp_err_to_name(report_result)
        );
    }
}

void can_router_mcp2518fd_tx_confirmation_callback(
    const can_fd_mcp2518fd_tx_event_t *event,
    void *context
)
{
    (void)context;

    if (event == NULL) {
        return;
    }

    const esp_err_t report_result =
        can_router_report_tx_result(
            CAN_BUS_SECONDARY,
            event->sequence,
            CAN_EVENT_TX_COMPLETED,
            ESP_OK,
            (uint64_t)event->timestamp,
            CAN_TIMESTAMP_SOURCE_HARDWARE
        );

    if ((report_result != ESP_OK) &&
        (report_result != ESP_ERR_NOT_FOUND) &&
        (report_result != ESP_ERR_INVALID_STATE)) {

        ESP_LOGW(
            TAG,
            "Failed to report MCP2518FD TX confirmation: "
            "sequence=%lu, error=%s",
            (unsigned long)event->sequence,
            esp_err_to_name(report_result)
        );
    }
}

esp_err_t can_router_get_statistics(
    can_router_statistics_t *statistics
)
{
    if (statistics == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(
        statistics,
        0,
        sizeof(*statistics)
    );

    esp_err_t result =
        can_router_lock(
            portMAX_DELAY
        );

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(
            &s_running
        )) {

        can_router_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    statistics->queue_current =
        (s_event_queue != NULL)
            ? (uint32_t)uxQueueMessagesWaiting(
                  s_event_queue
              )
            : 0U;

    statistics->queue_peak =
        (uint32_t)atomic_load_explicit(
            &s_queue_peak,
            memory_order_relaxed
        );

    statistics->queue_capacity =
        s_queue_capacity;

    statistics->pending_tx_current =
        (uint32_t)atomic_load_explicit(
            &s_pending_tx_current,
            memory_order_relaxed
        );

    statistics->pending_tx_peak =
        (uint32_t)atomic_load_explicit(
            &s_pending_tx_peak,
            memory_order_relaxed
        );

    statistics->pending_tx_capacity =
        s_pending_tx_capacity;

    statistics->registered_subscribers =
        (uint32_t)atomic_load_explicit(
            &s_registered_subscribers,
            memory_order_relaxed
        );

    statistics->subscriber_capacity =
        s_subscriber_capacity;

    statistics->received_frames =
        (uint32_t)atomic_load_explicit(
            &s_received_frames,
            memory_order_relaxed
        );

    statistics->transmitted_frames =
        (uint32_t)atomic_load_explicit(
            &s_transmitted_frames,
            memory_order_relaxed
        );

    statistics->completed_transmissions =
        (uint32_t)atomic_load_explicit(
            &s_completed_transmissions,
            memory_order_relaxed
        );

    statistics->failed_transmissions =
        (uint32_t)atomic_load_explicit(
            &s_failed_transmissions,
            memory_order_relaxed
        );

    statistics->aborted_transmissions =
        (uint32_t)atomic_load_explicit(
            &s_aborted_transmissions,
            memory_order_relaxed
        );

    statistics->dispatched_events =
        (uint32_t)atomic_load_explicit(
            &s_dispatched_events,
            memory_order_relaxed
        );

    statistics->dropped_events =
        (uint32_t)atomic_load_explicit(
            &s_dropped_events,
            memory_order_relaxed
        );

    statistics->unmatched_tx_confirmations =
        (uint32_t)atomic_load_explicit(
            &s_unmatched_tx_confirmations,
            memory_order_relaxed
        );

    can_router_unlock();

    return ESP_OK;
}

esp_err_t can_router_reset_statistics(void)
{
    esp_err_t result =
        can_router_lock(
            portMAX_DELAY
        );

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(
            &s_running
        ) ||
        atomic_load(
            &s_stop_requested
        )) {

        can_router_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    const uint32_t queue_current =
        (s_event_queue != NULL)
            ? (uint32_t)uxQueueMessagesWaiting(
                  s_event_queue
              )
            : 0U;

    const uint32_t pending_current =
        (uint32_t)atomic_load_explicit(
            &s_pending_tx_current,
            memory_order_relaxed
        );

    /*
     * Peaks restart from current occupancy so they never become lower
     * than the current value.
     */
    atomic_store_explicit(
        &s_queue_peak,
        queue_current,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_pending_tx_peak,
        pending_current,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_received_frames,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_transmitted_frames,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_completed_transmissions,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_failed_transmissions,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_aborted_transmissions,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_dispatched_events,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_dropped_events,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_unmatched_tx_confirmations,
        0U,
        memory_order_relaxed
    );

    can_router_unlock();

    return ESP_OK;
}
