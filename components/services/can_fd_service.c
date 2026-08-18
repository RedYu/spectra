#include "can_fd_service.h"

#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_log.h"

#define CAN_FD_SERVICE_TASK_STACK_SIZE       (4096U)
#define CAN_FD_SERVICE_TASK_PRIORITY         (5U)

#define CAN_FD_SERVICE_RECEIVE_TIMEOUT_MS    (10U)
#define CAN_FD_SERVICE_STOP_TIMEOUT_MS       (2000U)
#define CAN_FD_SERVICE_ERROR_DELAY_MS        (20U)

static const char *TAG =
    "can_fd_service";

static TaskHandle_t s_task = NULL;

static SemaphoreHandle_t s_api_mutex = NULL;
static SemaphoreHandle_t s_task_stopped = NULL;

static can_fd_service_config_t s_config;

static atomic_bool s_running =
    ATOMIC_VAR_INIT(false);

static atomic_bool s_stop_requested =
    ATOMIC_VAR_INIT(false);

static atomic_bool s_stop_in_progress =
    ATOMIC_VAR_INIT(false);

static atomic_uint_fast32_t s_delivered_rx_frames =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast32_t
    s_delivered_tx_confirmations =
        ATOMIC_VAR_INIT(0U);

static atomic_uint_fast32_t s_unhandled_rx_frames =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast32_t
    s_unhandled_tx_confirmations =
        ATOMIC_VAR_INIT(0U);

static atomic_uint_fast32_t s_receive_errors =
    ATOMIC_VAR_INIT(0U);

static atomic_uint_fast32_t s_tx_event_errors =
    ATOMIC_VAR_INIT(0U);

static esp_err_t can_fd_service_lock(void)
{
    if (s_api_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_api_mutex,
            portMAX_DELAY
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void can_fd_service_unlock(void)
{
    if (s_api_mutex != NULL) {
        (void)xSemaphoreGive(
            s_api_mutex
        );
    }
}

static void can_fd_service_reset_statistics_internal(void)
{
    atomic_store_explicit(
        &s_delivered_rx_frames,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_delivered_tx_confirmations,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_unhandled_rx_frames,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_unhandled_tx_confirmations,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_receive_errors,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &s_tx_event_errors,
        0U,
        memory_order_relaxed
    );
}

static void can_fd_service_deliver_frame(
    const can_fd_mcp2518fd_frame_t *frame
)
{
    can_fd_service_receive_cb_t callback =
        s_config.receive_callback;

    void *context =
        s_config.receive_context;

    if (callback == NULL) {
        (void)atomic_fetch_add_explicit(
            &s_unhandled_rx_frames,
            1U,
            memory_order_relaxed
        );

        return;
    }

    callback(
        frame,
        context
    );

    (void)atomic_fetch_add_explicit(
        &s_delivered_rx_frames,
        1U,
        memory_order_relaxed
    );
}

static void can_fd_service_deliver_tx_event(
    const can_fd_mcp2518fd_tx_event_t *event
)
{
    can_fd_service_tx_confirmation_cb_t callback =
        s_config.tx_confirmation_callback;

    void *context =
        s_config.tx_confirmation_context;

    if (callback == NULL) {
        (void)atomic_fetch_add_explicit(
            &s_unhandled_tx_confirmations,
            1U,
            memory_order_relaxed
        );

        return;
    }

    callback(
        event,
        context
    );

    (void)atomic_fetch_add_explicit(
        &s_delivered_tx_confirmations,
        1U,
        memory_order_relaxed
    );
}

static void can_fd_service_drain_receive_queue(void)
{
    while (!atomic_load(
               &s_stop_requested
           )) {

        can_fd_mcp2518fd_frame_t frame;

        const esp_err_t result =
            can_fd_mcp2518fd_driver_receive(
                &frame,
                0U
            );

        if (result == ESP_ERR_TIMEOUT) {
            break;
        }

        if (result != ESP_OK) {
            if (!atomic_load(
                    &s_stop_requested
                )) {

                (void)atomic_fetch_add_explicit(
                    &s_receive_errors,
                    1U,
                    memory_order_relaxed
                );

                ESP_LOGW(
                    TAG,
                    "Failed to drain RX queue: %s",
                    esp_err_to_name(result)
                );
            }

            break;
        }

        can_fd_service_deliver_frame(
            &frame
        );
    }
}

static void can_fd_service_drain_tx_events(void)
{
    while (!atomic_load(
               &s_stop_requested
           )) {

        can_fd_mcp2518fd_tx_event_t event;

        const esp_err_t result =
            can_fd_mcp2518fd_driver_receive_tx_event(
                &event,
                0U
            );

        if (result == ESP_ERR_TIMEOUT) {
            break;
        }

        if (result != ESP_OK) {
            if (!atomic_load(
                    &s_stop_requested
                )) {

                (void)atomic_fetch_add_explicit(
                    &s_tx_event_errors,
                    1U,
                    memory_order_relaxed
                );

                ESP_LOGW(
                    TAG,
                    "Failed to drain TX events: %s",
                    esp_err_to_name(result)
                );
            }

            break;
        }

        can_fd_service_deliver_tx_event(
            &event
        );
    }
}

static void can_fd_service_task(
    void *argument
)
{
    (void)argument;

    ESP_LOGI(
        TAG,
        "CAN FD processing task started"
    );

    while (!atomic_load(
               &s_stop_requested
           )) {

        /*
         * Wait for one received frame. The timeout also gives the task
         * an opportunity to process TEF events when there is no RX
         * traffic.
         */
        can_fd_mcp2518fd_frame_t frame;

        const esp_err_t receive_result =
            can_fd_mcp2518fd_driver_receive(
                &frame,
                CAN_FD_SERVICE_RECEIVE_TIMEOUT_MS
            );

        if (receive_result == ESP_OK) {
            can_fd_service_deliver_frame(
                &frame
            );

            can_fd_service_drain_receive_queue();
        } else if (receive_result != ESP_ERR_TIMEOUT) {
            if (!atomic_load(
                    &s_stop_requested
                )) {

                (void)atomic_fetch_add_explicit(
                    &s_receive_errors,
                    1U,
                    memory_order_relaxed
                );

                ESP_LOGW(
                    TAG,
                    "Failed to receive CAN FD frame: %s",
                    esp_err_to_name(receive_result)
                );

                vTaskDelay(
                    pdMS_TO_TICKS(
                        CAN_FD_SERVICE_ERROR_DELAY_MS
                    )
                );
            }
        }

        can_fd_service_drain_tx_events();
    }

    /*
     * Do not process callbacks after shutdown has been requested.
     * Remaining hardware transmissions are handled by the abort
     * operation in can_fd_service_stop().
     */
    ESP_LOGI(
        TAG,
        "CAN FD processing task stopped"
    );

    if (s_task_stopped != NULL) {
        (void)xSemaphoreGive(
            s_task_stopped
        );
    }

    vTaskDelete(NULL);
}

esp_err_t can_fd_service_start(
    const can_fd_service_config_t *config
)
{
    if (config == NULL) {
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
        can_fd_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (atomic_load(
            &s_running
        ) ||
        (s_task != NULL)) {

        can_fd_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    s_task_stopped =
        xSemaphoreCreateBinary();

    if (s_task_stopped == NULL) {
        can_fd_service_unlock();
        return ESP_ERR_NO_MEM;
    }

    s_config = *config;

    can_fd_service_reset_statistics_internal();

    atomic_store(
        &s_stop_requested,
        false
    );

    atomic_store(
        &s_stop_in_progress,
        false
    );

    result =
        can_fd_mcp2518fd_driver_init(
            &config->driver
        );

    if (result != ESP_OK) {
        vSemaphoreDelete(
            s_task_stopped
        );

        s_task_stopped = NULL;

        memset(
            &s_config,
            0,
            sizeof(s_config)
        );

        can_fd_service_unlock();

        return result;
    }

    result =
        can_fd_mcp2518fd_driver_start();

    if (result != ESP_OK) {
        (void)can_fd_mcp2518fd_driver_deinit();

        vSemaphoreDelete(
            s_task_stopped
        );

        s_task_stopped = NULL;

        memset(
            &s_config,
            0,
            sizeof(s_config)
        );

        can_fd_service_unlock();

        return result;
    }

    const BaseType_t task_result =
        xTaskCreate(
            can_fd_service_task,
            "can_fd_service",
            CAN_FD_SERVICE_TASK_STACK_SIZE,
            NULL,
            CAN_FD_SERVICE_TASK_PRIORITY,
            &s_task
        );

    if (task_result != pdPASS) {
        s_task = NULL;

        (void)can_fd_mcp2518fd_driver_stop();
        (void)can_fd_mcp2518fd_driver_deinit();

        vSemaphoreDelete(
            s_task_stopped
        );

        s_task_stopped = NULL;

        memset(
            &s_config,
            0,
            sizeof(s_config)
        );

        can_fd_service_unlock();

        return ESP_ERR_NO_MEM;
    }

    atomic_store(
        &s_running,
        true
    );

    ESP_LOGI(
        TAG,
        "Secondary CAN FD service started"
    );

    can_fd_service_unlock();

    return ESP_OK;
}

esp_err_t can_fd_service_stop(void)
{
    esp_err_t result =
        can_fd_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(
            &s_running
        ) ||
        (s_task == NULL) ||
        (s_task_stopped == NULL)) {

        can_fd_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    bool expected = false;

    if (!atomic_compare_exchange_strong(
            &s_stop_in_progress,
            &expected,
            true
        )) {

        can_fd_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    atomic_store(
        &s_stop_requested,
        true
    );

    can_fd_service_unlock();

    if (xSemaphoreTake(
            s_task_stopped,
            pdMS_TO_TICKS(
                CAN_FD_SERVICE_STOP_TIMEOUT_MS
            )
        ) != pdTRUE) {

        atomic_store(
            &s_stop_in_progress,
            false
        );

        ESP_LOGE(
            TAG,
            "CAN FD processing task did not stop"
        );

        return ESP_ERR_TIMEOUT;
    }

    result =
        can_fd_service_lock();

    if (result != ESP_OK) {
        atomic_store(
            &s_stop_in_progress,
            false
        );

        return result;
    }

    s_task = NULL;

    esp_err_t first_error = ESP_OK;

    const esp_err_t abort_result =
        can_fd_mcp2518fd_driver_abort_transmissions();

    if ((abort_result != ESP_OK) &&
        (abort_result != ESP_ERR_INVALID_STATE)) {

        ESP_LOGW(
            TAG,
            "Failed to abort MCP2518FD transmissions: %s",
            esp_err_to_name(abort_result)
        );

        first_error = abort_result;
    }

    const esp_err_t stop_result =
        can_fd_mcp2518fd_driver_stop();

    if ((stop_result != ESP_OK) &&
        (stop_result != ESP_ERR_INVALID_STATE)) {

        ESP_LOGW(
            TAG,
            "Failed to stop MCP2518FD driver: %s",
            esp_err_to_name(stop_result)
        );

        if (first_error == ESP_OK) {
            first_error = stop_result;
        }
    }

    const esp_err_t deinit_result =
        can_fd_mcp2518fd_driver_deinit();

    if (deinit_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to deinitialize MCP2518FD driver: %s",
            esp_err_to_name(deinit_result)
        );

        if (first_error == ESP_OK) {
            first_error = deinit_result;
        }
    }

    if (s_task_stopped != NULL) {
        vSemaphoreDelete(
            s_task_stopped
        );

        s_task_stopped = NULL;
    }

    memset(
        &s_config,
        0,
        sizeof(s_config)
    );

    atomic_store(
        &s_running,
        false
    );

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
        "Secondary CAN FD service stopped"
    );

    can_fd_service_unlock();

    return first_error;
}

esp_err_t can_fd_service_reconfigure(
    const can_fd_mcp2518fd_runtime_config_t *config
)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        can_fd_service_lock();

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

        can_fd_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    result =
        can_fd_mcp2518fd_driver_reconfigure(
            config
        );

    if (result == ESP_OK) {
        s_config.driver.nominal_bitrate =
            config->nominal_bitrate;

        s_config.driver.data_bitrate =
            config->data_bitrate;

        s_config.driver.mode =
            config->mode;

        s_config.driver.retransmission =
            config->retransmission;

        s_config.driver.fd_enabled =
            config->fd_enabled;

        s_config.driver.brs_enabled =
            config->brs_enabled;

        ESP_LOGI(
            TAG,
            "Secondary CAN reconfigured: "
            "nominal=%lu, data=%lu, FD=%s, BRS=%s",
            (unsigned long)config->nominal_bitrate,
            (unsigned long)config->data_bitrate,
            config->fd_enabled
                ? "enabled"
                : "disabled",
            config->brs_enabled
                ? "enabled"
                : "disabled"
        );
    }

    can_fd_service_unlock();

    return result;
}

esp_err_t can_fd_service_transmit(
    const can_fd_mcp2518fd_frame_t *frame,
    uint32_t timeout_ms
)
{
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        can_fd_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(
            &s_running
        ) ||
        atomic_load(
            &s_stop_requested
        )) {

        can_fd_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    result =
        can_fd_mcp2518fd_driver_transmit(
            frame,
            timeout_ms
        );

    can_fd_service_unlock();

    return result;
}

esp_err_t can_fd_service_transmit_tracked(
    const can_fd_mcp2518fd_frame_t *frame,
    uint32_t timeout_ms,
    uint32_t *sequence
)
{
    if ((frame == NULL) ||
        (sequence == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *sequence = 0U;

    esp_err_t result =
        can_fd_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(
            &s_running
        ) ||
        atomic_load(
            &s_stop_requested
        )) {

        can_fd_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    result =
        can_fd_mcp2518fd_driver_transmit_tracked(
            frame,
            timeout_ms,
            sequence
        );

    can_fd_service_unlock();

    return result;
}

esp_err_t can_fd_service_abort_transmissions(void)
{
    esp_err_t result =
        can_fd_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(
            &s_running
        ) ||
        atomic_load(
            &s_stop_requested
        )) {

        can_fd_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    result =
        can_fd_mcp2518fd_driver_abort_transmissions();

    can_fd_service_unlock();

    return result;
}

esp_err_t can_fd_service_recover(void)
{
    esp_err_t result =
        can_fd_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(
            &s_running
        ) ||
        atomic_load(
            &s_stop_requested
        )) {

        can_fd_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    result =
        can_fd_mcp2518fd_driver_recover();

    can_fd_service_unlock();

    return result;
}

esp_err_t can_fd_service_set_filter(
    const can_fd_mcp2518fd_filter_t *filter
)
{
    if (filter == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        can_fd_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(
            &s_running
        ) ||
        atomic_load(
            &s_stop_requested
        )) {

        can_fd_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    result =
        can_fd_mcp2518fd_driver_set_filter(
            filter
        );

    can_fd_service_unlock();

    return result;
}

esp_err_t can_fd_service_disable_filter(
    uint8_t filter_index
)
{
    esp_err_t result =
        can_fd_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(
            &s_running
        ) ||
        atomic_load(
            &s_stop_requested
        )) {

        can_fd_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    result =
        can_fd_mcp2518fd_driver_disable_filter(
            filter_index
        );

    can_fd_service_unlock();

    return result;
}

esp_err_t can_fd_service_disable_all_filters(void)
{
    esp_err_t result =
        can_fd_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(
            &s_running
        ) ||
        atomic_load(
            &s_stop_requested
        )) {

        can_fd_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    result =
        can_fd_mcp2518fd_driver_disable_all_filters();

    can_fd_service_unlock();

    return result;
}

esp_err_t can_fd_service_accept_all(void)
{
    esp_err_t result =
        can_fd_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(
            &s_running
        ) ||
        atomic_load(
            &s_stop_requested
        )) {

        can_fd_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    result =
        can_fd_mcp2518fd_driver_accept_all();

    can_fd_service_unlock();

    return result;
}

esp_err_t can_fd_service_get_info(
    can_fd_mcp2518fd_info_t *info
)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(
        info,
        0,
        sizeof(*info)
    );

    esp_err_t result =
        can_fd_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(
            &s_running
        )) {

        can_fd_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    result =
        can_fd_mcp2518fd_driver_get_info(
            info
        );

    can_fd_service_unlock();

    return result;
}

esp_err_t can_fd_service_get_statistics(
    can_fd_service_statistics_t *statistics
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

    if (!atomic_load(
            &s_running
        )) {

        return ESP_ERR_INVALID_STATE;
    }

    statistics->delivered_rx_frames =
        (uint32_t)atomic_load_explicit(
            &s_delivered_rx_frames,
            memory_order_relaxed
        );

    statistics->delivered_tx_confirmations =
        (uint32_t)atomic_load_explicit(
            &s_delivered_tx_confirmations,
            memory_order_relaxed
        );

    statistics->unhandled_rx_frames =
        (uint32_t)atomic_load_explicit(
            &s_unhandled_rx_frames,
            memory_order_relaxed
        );

    statistics->unhandled_tx_confirmations =
        (uint32_t)atomic_load_explicit(
            &s_unhandled_tx_confirmations,
            memory_order_relaxed
        );

    statistics->receive_errors =
        (uint32_t)atomic_load_explicit(
            &s_receive_errors,
            memory_order_relaxed
        );

    statistics->tx_event_errors =
        (uint32_t)atomic_load_explicit(
            &s_tx_event_errors,
            memory_order_relaxed
        );

    return ESP_OK;
}

esp_err_t can_fd_service_reset_statistics(void)
{
    if (!atomic_load(
            &s_running
        )) {

        return ESP_ERR_INVALID_STATE;
    }

    can_fd_service_reset_statistics_internal();

    return ESP_OK;
}

bool can_fd_service_is_running(void)
{
    return atomic_load(
        &s_running
    );
}
