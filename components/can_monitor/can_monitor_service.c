#include "can_monitor_service.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "can_router.h"

#include "app_task_priorities.h"

#define CAN_MONITOR_TASK_STACK_SIZE       (4096U)
#define CAN_MONITOR_TASK_PRIORITY \
    APP_TASK_PRIORITY_CAN_MONITOR

#define CAN_MONITOR_TASK_WAIT_MS          (50U)
#define CAN_MONITOR_STOP_TIMEOUT_MS       (1000U)
#define CAN_MONITOR_CALLBACK_WAIT_MS      (100U)

static const char *TAG =
    "can_monitor_service";

typedef struct
{
    bool occupied;

    can_monitor_identifier_info_t info;

} can_monitor_identifier_slot_t;

static atomic_bool s_running =
    ATOMIC_VAR_INIT(false);

static atomic_bool s_accepting_events =
    ATOMIC_VAR_INIT(false);

static atomic_bool s_stop_requested =
    ATOMIC_VAR_INIT(false);

static atomic_uint_fast32_t s_active_callbacks =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast32_t s_dropped_input_events =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast32_t s_queue_peak =
    ATOMIC_VAR_INIT(0U);

static QueueHandle_t s_input_queue = NULL;
static SemaphoreHandle_t s_mutex = NULL;
static SemaphoreHandle_t s_task_stopped = NULL;

static TaskHandle_t s_task = NULL;
static StaticTask_t *s_task_control_block = NULL;
static StackType_t *s_task_stack = NULL;

static uint32_t s_subscription_id =
    CAN_ROUTER_SUBSCRIPTION_ID_NONE;

static can_event_t *s_history = NULL;
static uint32_t s_history_capacity = 0U;
static uint32_t s_history_count = 0U;
static uint32_t s_history_write_index = 0U;

static can_monitor_identifier_slot_t
    *s_identifier_slots = NULL;

static uint32_t s_identifier_capacity = 0U;
static uint32_t s_identifier_count = 0U;

static can_monitor_service_statistics_t
    s_statistics;

static esp_err_t can_monitor_lock_running(void)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_mutex,
            portMAX_DELAY
        ) != pdTRUE) {

        return ESP_FAIL;
    }

    if (!atomic_load(
            &s_running
        )) {

        (void)xSemaphoreGive(
            s_mutex
        );

        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

static void can_monitor_unlock(void)
{
    (void)xSemaphoreGive(
        s_mutex
    );
}

static void can_monitor_update_atomic_peak(
    atomic_uint_fast32_t *peak,
    uint32_t value
)
{
    if (peak == NULL) {
        return;
    }

    uint_fast32_t current =
        atomic_load(peak);

    while ((value > current) &&
           !atomic_compare_exchange_weak(
               peak,
               &current,
               value
           )) {
    }
}

static bool can_monitor_event_has_bus_traffic(
    const can_event_t *event
)
{
    if (event == NULL) {
        return false;
    }

    return (event->type == CAN_EVENT_RX) ||
           (event->type == CAN_EVENT_TX_COMPLETED);
}

static void can_monitor_update_timestamp_range(
    can_monitor_bus_statistics_t *statistics,
    uint64_t timestamp_us
)
{
    if ((statistics == NULL) ||
        (timestamp_us == 0U)) {

        return;
    }

    if ((statistics->first_event_timestamp_us == 0U) ||
        (timestamp_us <
         statistics->first_event_timestamp_us)) {

        statistics->first_event_timestamp_us =
            timestamp_us;
    }

    if (timestamp_us >
        statistics->last_event_timestamp_us) {

        statistics->last_event_timestamp_us =
            timestamp_us;
    }
}

static void can_monitor_store_history(
    const can_event_t *event
)
{
    if ((event == NULL) ||
        (s_history == NULL) ||
        (s_history_capacity == 0U)) {

        return;
    }

    s_history[
        s_history_write_index
    ] = *event;

    s_history_write_index =
        (s_history_write_index + 1U) %
        s_history_capacity;

    if (s_history_count <
        s_history_capacity) {

        ++s_history_count;
    }
}

static can_monitor_identifier_slot_t *
can_monitor_find_identifier_slot(
    const can_event_t *event
)
{
    if ((event == NULL) ||
        (s_identifier_slots == NULL)) {

        return NULL;
    }

    const bool extended =
        (event->frame.flags &
         CAN_FRAME_FLAG_EXTENDED_ID) != 0U;

    can_monitor_identifier_slot_t
        *free_slot = NULL;

    for (uint32_t index = 0U;
         index < s_identifier_capacity;
         ++index) {

        can_monitor_identifier_slot_t *slot =
            &s_identifier_slots[index];

        if (!slot->occupied) {
            if (free_slot == NULL) {
                free_slot = slot;
            }

            continue;
        }

        if ((slot->info.bus ==
             event->frame.bus) &&
            (slot->info.identifier ==
             event->frame.identifier) &&
            (slot->info.extended ==
             extended)) {

            return slot;
        }
    }

    if (free_slot == NULL) {
        return NULL;
    }

    memset(
        free_slot,
        0,
        sizeof(*free_slot)
    );

    free_slot->occupied = true;

    free_slot->info.bus =
        event->frame.bus;

    free_slot->info.identifier =
        event->frame.identifier;

    free_slot->info.extended =
        extended;

    ++s_identifier_count;

    return free_slot;
}

static void can_monitor_update_identifier(
    const can_event_t *event
)
{
    if (!can_monitor_event_has_bus_traffic(
            event
        )) {

        return;
    }

    can_monitor_identifier_slot_t *slot =
        can_monitor_find_identifier_slot(
            event
        );

    if (slot == NULL) {
        return;
    }

    const uint64_t timestamp_us =
        event->frame.timestamp_us;

    if ((slot->info.first_seen_timestamp_us == 0U) &&
        (timestamp_us != 0U)) {

        slot->info.first_seen_timestamp_us =
            timestamp_us;
    }

    if (timestamp_us >
        slot->info.last_seen_timestamp_us) {

        slot->info.last_seen_timestamp_us =
            timestamp_us;
    }

    if (event->type == CAN_EVENT_RX) {
        ++slot->info.received_frames;

        slot->info.received_bytes +=
            event->frame.data_length;

        slot->info.last_direction =
            CAN_MONITOR_DIRECTION_RX;
    } else if (event->type == CAN_EVENT_TX_COMPLETED) {
        ++slot->info.transmitted_frames;

        slot->info.transmitted_bytes +=
            event->frame.data_length;

        slot->info.last_direction =
            CAN_MONITOR_DIRECTION_TX;
    }

    slot->info.last_activity_timestamp_us =
        (uint64_t)esp_timer_get_time();

    slot->info.last_frame =
        event->frame;
}

static void can_monitor_process_event(
    const can_event_t *event
)
{
    if ((event == NULL) ||
        (event->frame.bus >= CAN_BUS_COUNT)) {

        return;
    }

    if (xSemaphoreTake(
            s_mutex,
            portMAX_DELAY
        ) != pdTRUE) {

        return;
    }

    can_monitor_bus_statistics_t *bus =
        &s_statistics.buses[
            event->frame.bus
        ];

    ++s_statistics.processed_events;

    switch (event->type) {
        case CAN_EVENT_RX:
            ++bus->received_frames;

            bus->received_bytes +=
                event->frame.data_length;
            break;

        case CAN_EVENT_TX_QUEUED:
            ++bus->queued_transmissions;
            break;

        case CAN_EVENT_TX_COMPLETED:
            ++bus->completed_transmissions;

            bus->transmitted_bytes +=
                event->frame.data_length;
            break;

        case CAN_EVENT_TX_FAILED:
            ++bus->failed_transmissions;
            break;

        case CAN_EVENT_TX_ABORTED:
            ++bus->aborted_transmissions;
            break;

        default:
            break;
    }

    can_monitor_update_timestamp_range(
        bus,
        event->frame.timestamp_us
    );

    can_monitor_store_history(
        event
    );

    can_monitor_update_identifier(
        event
    );

    (void)xSemaphoreGive(
        s_mutex
    );
}

static void can_monitor_router_callback(
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
        (s_input_queue == NULL)) {

        atomic_fetch_sub(
            &s_active_callbacks,
            1U
        );

        return;
    }

    if (xQueueSend(
            s_input_queue,
            event,
            0U
        ) != pdTRUE) {

        atomic_fetch_add(
            &s_dropped_input_events,
            1U
        );
    } else {
        const uint32_t queue_current =
            (uint32_t)uxQueueMessagesWaiting(
                s_input_queue
            );

        can_monitor_update_atomic_peak(
            &s_queue_peak,
            queue_current
        );
    }

    atomic_fetch_sub(
        &s_active_callbacks,
        1U
    );
}

static void can_monitor_task(
    void *argument
)
{
    (void)argument;

    ESP_LOGI(
        TAG,
        "CAN monitor task started"
    );

    can_event_t event;

    while (!atomic_load(
               &s_stop_requested
           )) {

        if (xQueueReceive(
                s_input_queue,
                &event,
                pdMS_TO_TICKS(
                    CAN_MONITOR_TASK_WAIT_MS
                )
            ) == pdTRUE) {

            can_monitor_process_event(
                &event
            );
        }
    }

    ESP_LOGI(
        TAG,
        "CAN monitor task stopped"
    );

    /*
     * Notify the owner and suspend. The owner deletes the task before
     * releasing its statically allocated stack and control block.
     */
    (void)xSemaphoreGive(
        s_task_stopped
    );

    vTaskSuspend(NULL);
}

static void can_monitor_release_resources(void)
{
    if (s_input_queue != NULL) {
        vQueueDelete(
            s_input_queue
        );

        s_input_queue = NULL;
    }

    if (s_task_stopped != NULL) {
        vSemaphoreDelete(
            s_task_stopped
        );

        s_task_stopped = NULL;
    }

    if (s_history != NULL) {
        heap_caps_free(
            s_history
        );

        s_history = NULL;
    }

    if (s_identifier_slots != NULL) {
        heap_caps_free(
            s_identifier_slots
        );

        s_identifier_slots = NULL;
    }

    if (s_task_stack != NULL) {
        heap_caps_free(
            s_task_stack
        );

        s_task_stack = NULL;
    }

    if (s_task_control_block != NULL) {
        heap_caps_free(
            s_task_control_block
        );

        s_task_control_block = NULL;
    }

    s_history_capacity = 0U;
    s_history_count = 0U;
    s_history_write_index = 0U;

    s_identifier_capacity = 0U;
    s_identifier_count = 0U;

    s_subscription_id =
        CAN_ROUTER_SUBSCRIPTION_ID_NONE;

    memset(
        &s_statistics,
        0,
        sizeof(s_statistics)
    );
}

esp_err_t can_monitor_service_start(
    const can_monitor_service_config_t *config
)
{
    if ((config == NULL) ||
        (config->queue_depth == 0U) ||
        (config->history_capacity == 0U) ||
        (config->identifier_capacity == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (atomic_load(
            &s_running
        )) {

        return ESP_ERR_INVALID_STATE;
    }

    if (!can_router_is_running()) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(
        &s_statistics,
        0,
        sizeof(s_statistics)
    );

    atomic_store(
        &s_stop_requested,
        false
    );

    atomic_store(
        &s_accepting_events,
        false
    );

    atomic_store(
        &s_active_callbacks,
        0U
    );

    atomic_store(
        &s_dropped_input_events,
        0U
    );

    atomic_store(
        &s_queue_peak,
        0U
    );

    s_history_capacity =
        config->history_capacity;

    s_identifier_capacity =
        config->identifier_capacity;

    s_input_queue =
        xQueueCreate(
            config->queue_depth,
            sizeof(can_event_t)
        );

    if (s_input_queue == NULL) {
        can_monitor_release_resources();
        return ESP_ERR_NO_MEM;
    }

    if (s_mutex == NULL) {
        s_mutex =
            xSemaphoreCreateMutex();

        if (s_mutex == NULL) {
            can_monitor_release_resources();
            return ESP_ERR_NO_MEM;
        }
    }

    s_task_stopped =
        xSemaphoreCreateBinary();

    if (s_task_stopped == NULL) {
        can_monitor_release_resources();
        return ESP_ERR_NO_MEM;
    }

    s_history =
        heap_caps_calloc(
            config->history_capacity,
            sizeof(*s_history),
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

    if (s_history == NULL) {
        can_monitor_release_resources();
        return ESP_ERR_NO_MEM;
    }

    s_identifier_slots =
        heap_caps_calloc(
            config->identifier_capacity,
            sizeof(*s_identifier_slots),
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

    if (s_identifier_slots == NULL) {
        can_monitor_release_resources();
        return ESP_ERR_NO_MEM;
    }

    s_task_stack =
        heap_caps_malloc(
            CAN_MONITOR_TASK_STACK_SIZE,
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

    s_task_control_block =
        heap_caps_calloc(
            1U,
            sizeof(*s_task_control_block),
            MALLOC_CAP_INTERNAL |
            MALLOC_CAP_8BIT
        );

    if ((s_task_stack == NULL) ||
        (s_task_control_block == NULL)) {

        can_monitor_release_resources();
        return ESP_ERR_NO_MEM;
    }

    s_task =
        xTaskCreateStatic(
            can_monitor_task,
            "can_monitor",
            CAN_MONITOR_TASK_STACK_SIZE,
            NULL,
            CAN_MONITOR_TASK_PRIORITY,
            s_task_stack,
            s_task_control_block
        );

    if (s_task == NULL) {
        can_monitor_release_resources();
        return ESP_ERR_NO_MEM;
    }

    atomic_store(
        &s_accepting_events,
        true
    );

    const can_router_subscription_t subscription = {
        .bus_mask =
            CAN_ROUTER_ALL_BUSES_MASK,

        .event_mask =
            CAN_ROUTER_ALL_EVENTS_MASK,

        .callback =
            can_monitor_router_callback,

        .context =
            NULL,
    };

    const esp_err_t subscribe_result =
        can_router_subscribe(
            &subscription,
            &s_subscription_id
        );

    if (subscribe_result != ESP_OK) {
        atomic_store(
            &s_accepting_events,
            false
        );

        atomic_store(
            &s_stop_requested,
            true
        );

        (void)xSemaphoreTake(
            s_task_stopped,
            pdMS_TO_TICKS(
                CAN_MONITOR_STOP_TIMEOUT_MS
            )
        );

        if (s_task != NULL) {
            vTaskDelete(
                s_task
            );

            s_task = NULL;
        }

        can_monitor_release_resources();

        return subscribe_result;
    }

    atomic_store(
        &s_running,
        true
    );

    ESP_LOGI(
        TAG,
        "CAN monitor started: queue=%lu, history=%lu, "
        "identifiers=%lu",
        (unsigned long)config->queue_depth,
        (unsigned long)config->history_capacity,
        (unsigned long)config->identifier_capacity
    );

    return ESP_OK;
}

esp_err_t can_monitor_service_stop(void)
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

    if (s_subscription_id !=
        CAN_ROUTER_SUBSCRIPTION_ID_NONE) {

        const esp_err_t result =
            can_router_unsubscribe(
                s_subscription_id
            );

        if ((result != ESP_OK) &&
            (result != ESP_ERR_NOT_FOUND) &&
            (result != ESP_ERR_INVALID_STATE)) {

            return result;
        }

        s_subscription_id =
            CAN_ROUTER_SUBSCRIPTION_ID_NONE;
    }

    const TickType_t callback_deadline =
        xTaskGetTickCount() +
        pdMS_TO_TICKS(
            CAN_MONITOR_CALLBACK_WAIT_MS
        );

    while (atomic_load(
               &s_active_callbacks
           ) != 0U) {

        if ((int32_t)(
                xTaskGetTickCount() -
                callback_deadline
            ) >= 0) {

            return ESP_ERR_TIMEOUT;
        }

        vTaskDelay(
            1U
        );
    }

    atomic_store(
        &s_stop_requested,
        true
    );

    if (xSemaphoreTake(
            s_task_stopped,
            pdMS_TO_TICKS(
                CAN_MONITOR_STOP_TIMEOUT_MS
            )
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    if (s_task != NULL) {
        vTaskDelete(
            s_task
        );

        s_task = NULL;
    }

    if (xSemaphoreTake(
            s_mutex,
            portMAX_DELAY
        ) != pdTRUE) {

        return ESP_FAIL;
    }

    atomic_store(
        &s_running,
        false
    );

    can_monitor_release_resources();

    /*
     * s_mutex is intentionally retained for the complete application
     * lifetime and is not released by can_monitor_release_resources().
     */
    (void)xSemaphoreGive(
        s_mutex
    );

    ESP_LOGI(
        TAG,
        "CAN monitor stopped"
    );

    return ESP_OK;
}

esp_err_t can_monitor_service_get_statistics(
    can_monitor_service_statistics_t *statistics
)
{
    if (statistics == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t lock_result =
        can_monitor_lock_running();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    *statistics =
        s_statistics;

    statistics->dropped_input_events =
        atomic_load(
            &s_dropped_input_events
        );

    statistics->input_queue_current =
        (uint32_t)uxQueueMessagesWaiting(
            s_input_queue
        );

    statistics->input_queue_peak =
        atomic_load(
            &s_queue_peak
        );

    statistics->input_queue_capacity =
        (uint32_t)uxQueueSpacesAvailable(
            s_input_queue
        ) +
        statistics->input_queue_current;

    statistics->history_current =
        s_history_count;

    statistics->history_capacity =
        s_history_capacity;

    statistics->tracked_identifiers =
        s_identifier_count;

    statistics->identifier_capacity =
        s_identifier_capacity;

    can_monitor_unlock();

    return ESP_OK;
}

esp_err_t can_monitor_service_get_recent_events(
    can_event_t *events,
    size_t capacity,
    size_t *count
)
{
    if ((events == NULL) ||
        (capacity == 0U) ||
        (count == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *count = 0U;

    const esp_err_t lock_result =
        can_monitor_lock_running();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    size_t copy_count =
        s_history_count;

    if (copy_count > capacity) {
        copy_count = capacity;
    }

    for (size_t index = 0U;
         index < copy_count;
         ++index) {

        const uint32_t history_index =
            (s_history_write_index +
             s_history_capacity -
             1U -
             (uint32_t)index) %
            s_history_capacity;

        events[index] =
            s_history[history_index];
    }

    *count =
        copy_count;

    can_monitor_unlock();

    return ESP_OK;
}

esp_err_t can_monitor_service_get_identifiers(
    can_monitor_identifier_info_t *identifiers,
    size_t capacity,
    size_t *count
)
{
    if ((identifiers == NULL) ||
        (capacity == 0U) ||
        (count == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *count = 0U;

    const esp_err_t lock_result =
        can_monitor_lock_running();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    size_t copied = 0U;

    for (uint32_t index = 0U;
         (index < s_identifier_capacity) &&
         (copied < capacity);
         ++index) {

        const can_monitor_identifier_slot_t *slot =
            &s_identifier_slots[index];

        if (!slot->occupied) {
            continue;
        }

        identifiers[copied] =
            slot->info;

        ++copied;
    }

    *count =
        copied;

    can_monitor_unlock();

    return ESP_OK;
}

esp_err_t can_monitor_service_clear(void)
{
    const esp_err_t lock_result =
        can_monitor_lock_running();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    if (xQueueReset(
            s_input_queue
        ) != pdPASS) {

        can_monitor_unlock();

        return ESP_FAIL;
    }

    memset(
        &s_statistics,
        0,
        sizeof(s_statistics)
    );

    memset(
        s_history,
        0,
        s_history_capacity *
        sizeof(*s_history)
    );

    memset(
        s_identifier_slots,
        0,
        s_identifier_capacity *
        sizeof(*s_identifier_slots)
    );

    s_history_count = 0U;
    s_history_write_index = 0U;
    s_identifier_count = 0U;

    atomic_store(
        &s_dropped_input_events,
        0U
    );

    atomic_store(
        &s_queue_peak,
        (uint32_t)uxQueueMessagesWaiting(
            s_input_queue
        )
    );

    can_monitor_unlock();

    return ESP_OK;
}

bool can_monitor_service_is_running(void)
{
    return atomic_load(
        &s_running
    );
}
