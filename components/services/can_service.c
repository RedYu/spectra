#include "can_service.h"

#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"

#include "can_twai_frame_adapter.h"
#include "app_task_priorities.h"

#define CAN_SERVICE_TASK_STACK_SIZE          (4096U)
#define CAN_SERVICE_TASK_PRIORITY \
    APP_TASK_PRIORITY_CAN_TWAI

#define CAN_SERVICE_RECEIVE_TIMEOUT_MS       (20U)
#define CAN_SERVICE_STOP_TIMEOUT_MS          (2000U)
#define CAN_SERVICE_ERROR_RETRY_DELAY_MS     (20U)

#define CAN_SERVICE_SELF_TEST_IDENTIFIER     (0x123U)

static const char *TAG =
    "can_service";

static TaskHandle_t s_task = NULL;

static QueueHandle_t s_tx_confirmation_queue = NULL;

static SemaphoreHandle_t s_api_mutex = NULL;
static SemaphoreHandle_t s_task_stopped = NULL;

static can_service_receive_cb_t
    s_receive_callback = NULL;

static void *s_receive_context = NULL;

static can_service_common_receive_cb_t
    s_common_receive_callback = NULL;

static void *s_common_receive_context = NULL;

static atomic_bool s_running =
    ATOMIC_VAR_INIT(false);

static atomic_bool s_stop_requested =
    ATOMIC_VAR_INIT(false);

static atomic_bool s_stop_in_progress =
    ATOMIC_VAR_INIT(false);

static atomic_uint_fast32_t
    s_confirmation_queue_peak =
        ATOMIC_VAR_INIT(0U);

static atomic_uint_fast32_t
    s_dropped_tx_confirmations =
        ATOMIC_VAR_INIT(0U);

static uint32_t
    s_confirmation_queue_capacity = 0U;

static can_service_config_t s_config;

static const uint8_t s_self_test_data[
    CAN_TWAI_CLASSIC_DATA_MAX_LENGTH
] = {
    0x11U,
    0x22U,
    0x33U,
    0x44U,
    0x55U,
    0x66U,
    0x77U,
    0x88U,
};

static SemaphoreHandle_t
    s_self_test_received = NULL;

static esp_err_t can_service_lock(void)
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

static void can_service_unlock(void)
{
    if (s_api_mutex != NULL) {
        (void)xSemaphoreGive(
            s_api_mutex
        );
    }
}

static void can_service_delete_confirmation_queue(void)
{
    if (s_tx_confirmation_queue != NULL) {
        vQueueDelete(
            s_tx_confirmation_queue
        );

        s_tx_confirmation_queue = NULL;
    }

    s_confirmation_queue_capacity = 0U;
}

static void can_service_update_confirmation_peak(
    uint32_t current
)
{
    uint_fast32_t peak =
        atomic_load_explicit(
            &s_confirmation_queue_peak,
            memory_order_relaxed
        );

    while ((current > peak) &&
           !atomic_compare_exchange_weak_explicit(
               &s_confirmation_queue_peak,
               &peak,
               current,
               memory_order_relaxed,
               memory_order_relaxed
           )) {
    }
}

static void can_service_process_tx_confirmations(void)
{
    if (s_tx_confirmation_queue == NULL) {
        return;
    }

    can_twai_tx_confirmation_t confirmation;

    while (xQueueReceive(
               s_tx_confirmation_queue,
               &confirmation,
               0U
           ) == pdTRUE) {

        can_service_tx_confirmation_cb_t callback =
            s_config.tx_confirmation_callback;

        void *context =
            s_config.tx_confirmation_context;

        if (callback != NULL) {
            callback(
                &confirmation,
                context
            );
        }
    }
}

static bool can_service_tx_confirmation_isr_callback(
    const can_twai_tx_confirmation_t *confirmation,
    void *callback_context
)
{
    (void)callback_context;

    if ((confirmation == NULL) ||
        (s_tx_confirmation_queue == NULL)) {

        return false;
    }

    BaseType_t higher_priority_task_woken =
        pdFALSE;

    if (xQueueSendFromISR(
            s_tx_confirmation_queue,
            confirmation,
            &higher_priority_task_woken
        ) != pdTRUE) {

        (void)atomic_fetch_add_explicit(
            &s_dropped_tx_confirmations,
            1U,
            memory_order_relaxed
        );

        return false;
    }

    const UBaseType_t queue_current =
        uxQueueMessagesWaitingFromISR(
            s_tx_confirmation_queue
        );

    can_service_update_confirmation_peak(
        (uint32_t)queue_current
    );

    return higher_priority_task_woken ==
           pdTRUE;
}

static can_twai_driver_config_t
can_service_prepare_driver_config(
    const can_twai_driver_config_t *config
)
{
    can_twai_driver_config_t driver_config =
        *config;

    driver_config.tx_confirmation_callback =
        can_service_tx_confirmation_isr_callback;

    driver_config.tx_confirmation_context =
        NULL;

    return driver_config;
}

static void can_service_receive_task(
    void *argument
)
{
    (void)argument;

    ESP_LOGI(
        TAG,
        "CAN receive task started"
    );

    while (!atomic_load(
               &s_stop_requested
           )) {

        can_service_process_tx_confirmations();

        can_twai_frame_t frame;

        const esp_err_t result =
            can_twai_driver_receive(
                &frame,
                CAN_SERVICE_RECEIVE_TIMEOUT_MS
            );

        can_service_process_tx_confirmations();

        if (result == ESP_ERR_TIMEOUT) {
            continue;
        }

        if (result != ESP_OK) {
            if (atomic_load(
                    &s_stop_requested
                )) {

                break;
            }

            ESP_LOGW(
                TAG,
                "Failed to receive CAN frame: %s",
                esp_err_to_name(result)
            );

            vTaskDelay(
                pdMS_TO_TICKS(
                    CAN_SERVICE_ERROR_RETRY_DELAY_MS
                )
            );

            continue;
        }

        can_frame_t common_frame;
        bool common_frame_valid = false;

        can_service_common_receive_cb_t
            common_callback =
                s_common_receive_callback;

        void *common_context =
            s_common_receive_context;

        /*
         * Convert the frame before invoking the legacy callback so an
         * incorrectly implemented callback cannot modify source data
         * before it reaches the shared-frame consumer.
         */
        if (common_callback != NULL) {
            const esp_err_t adapter_result =
                can_twai_frame_to_common(
                    &frame,
                    &common_frame
                );

            if (adapter_result == ESP_OK) {
                common_frame_valid = true;
            } else {
                ESP_LOGW(
                    TAG,
                    "Failed to convert received TWAI frame: %s",
                    esp_err_to_name(adapter_result)
                );
            }
        }

        can_service_receive_cb_t callback =
            s_receive_callback;

        void *context =
            s_receive_context;

        /*
         * Preserve the documented callback order during migration.
         */
        if (callback != NULL) {
            callback(
                &frame,
                context
            );
        }

        if (common_frame_valid) {
            common_callback(
                &common_frame,
                common_context
            );
        }
    }

    /*
     * Deliver confirmations already queued before task termination.
     */
    can_service_process_tx_confirmations();

    ESP_LOGI(
        TAG,
        "CAN receive task stopped"
    );

    if (s_task_stopped != NULL) {
        (void)xSemaphoreGive(
            s_task_stopped
        );
    }

    vTaskDelete(NULL);
}

static void can_service_self_test_receive_callback(
    const can_twai_frame_t *frame,
    void *context
)
{
    (void)context;

    if ((frame == NULL) ||
        (frame->identifier !=
         CAN_SERVICE_SELF_TEST_IDENTIFIER) ||
        frame->extended ||
        frame->remote ||
        (frame->data_length !=
         CAN_TWAI_CLASSIC_DATA_MAX_LENGTH) ||
        (memcmp(
            frame->data,
            s_self_test_data,
            sizeof(s_self_test_data)
        ) != 0)) {

        return;
    }

    ESP_LOGI(
        TAG,
        "TWAI self-test frame received: "
        "ID=0x%03lX, timestamp=%llu",
        (unsigned long)frame->identifier,
        (unsigned long long)frame->timestamp_us
    );

    if (s_self_test_received != NULL) {
        (void)xSemaphoreGive(
            s_self_test_received
        );
    }
}

esp_err_t can_service_start(
    const can_service_config_t *config
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
        can_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (atomic_load(
            &s_running
        ) ||
        (s_task != NULL)) {

        can_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    s_task_stopped =
        xSemaphoreCreateBinary();

    if (s_task_stopped == NULL) {
        can_service_unlock();
        return ESP_ERR_NO_MEM;
    }

    const UBaseType_t confirmation_queue_length =
        config->driver.tx_queue_depth > 0U
            ? (UBaseType_t)config->driver.tx_queue_depth
            : 1U;

    s_tx_confirmation_queue =
        xQueueCreate(
            confirmation_queue_length,
            sizeof(can_twai_tx_confirmation_t)
        );

    if (s_tx_confirmation_queue == NULL) {
        vSemaphoreDelete(
            s_task_stopped
        );

        s_task_stopped = NULL;

        can_service_unlock();

        return ESP_ERR_NO_MEM;
    }

    s_confirmation_queue_capacity =
        (uint32_t)confirmation_queue_length;

    atomic_store(
        &s_confirmation_queue_peak,
        0U
    );

    atomic_store(
        &s_dropped_tx_confirmations,
        0U
    );

    const can_twai_driver_config_t driver_config =
        can_service_prepare_driver_config(
            &config->driver
        );

    result =
        can_twai_driver_init(
            &driver_config
        );

    if (result != ESP_OK) {
        can_service_delete_confirmation_queue();
        
        vSemaphoreDelete(
            s_task_stopped
        );

        s_task_stopped = NULL;

        can_service_unlock();
        return result;
    }

    result =
        can_twai_driver_start();

    if (result != ESP_OK) {
        (void)can_twai_driver_deinit();
        can_service_delete_confirmation_queue();

        vSemaphoreDelete(
            s_task_stopped
        );

        s_task_stopped = NULL;

        can_service_unlock();
        return result;
    }

    s_receive_callback =
        config->receive_callback;

    s_receive_context =
        config->receive_context;

    s_common_receive_callback =
        config->common_receive_callback;

    s_common_receive_context =
        config->common_receive_context;

    atomic_store(
        &s_stop_requested,
        false
    );

    atomic_store(
        &s_stop_in_progress,
        false
    );

    s_config = *config;

    const BaseType_t task_result =
        xTaskCreate(
            can_service_receive_task,
            "can_service",
            CAN_SERVICE_TASK_STACK_SIZE,
            NULL,
            CAN_SERVICE_TASK_PRIORITY,
            &s_task
        );

    if (task_result != pdPASS) {
        s_task = NULL;

        s_receive_callback = NULL;
        s_receive_context = NULL;

        s_common_receive_callback = NULL;
        s_common_receive_context = NULL;

        memset(
            &s_config,
            0,
            sizeof(s_config)
        );

        (void)can_twai_driver_stop();
        (void)can_twai_driver_deinit();
        can_service_delete_confirmation_queue();

        vSemaphoreDelete(
            s_task_stopped
        );

        s_task_stopped = NULL;

        can_service_unlock();
        return ESP_ERR_NO_MEM;
    }

    atomic_store(
        &s_running,
        true
    );

    ESP_LOGI(
        TAG,
        "Primary CAN service started"
    );

    can_service_unlock();

    return ESP_OK;
}

esp_err_t can_service_run_self_test(
    uint32_t bitrate,
    uint32_t timeout_ms
)
{
    if ((bitrate == 0U) ||
        (timeout_ms == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (can_service_is_running() ||
        (s_self_test_received != NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    s_self_test_received =
        xSemaphoreCreateBinary();

    if (s_self_test_received == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const can_service_config_t config = {
        .driver = {
            .bitrate = bitrate,
            .sample_point_permill = 800U,

            .mode =
                CAN_TWAI_MODE_SELF_TEST,

            .tx_queue_depth = 4U,
            .rx_queue_length = 4U,

            /*
             * Self-test does not require acknowledgement.
             */
            .transmit_retry_count = 0,

            .acceptance_filter = {
                .identifier =
                    CAN_SERVICE_SELF_TEST_IDENTIFIER,

                .mask =
                    CAN_TWAI_STANDARD_ID_MAX,

                .extended = false,
            },

            /*
             * Replaced internally by can_service_start().
             */
            .tx_confirmation_callback = NULL,
            .tx_confirmation_context = NULL,
        },

        .receive_callback =
            can_service_self_test_receive_callback,

        .receive_context = NULL,

        .common_receive_callback = NULL,
        .common_receive_context = NULL,

        .tx_confirmation_callback = NULL,
        .tx_confirmation_context = NULL,
    };

    esp_err_t result =
        can_service_start(
            &config
        );

    if (result != ESP_OK) {
        vSemaphoreDelete(
            s_self_test_received
        );

        s_self_test_received = NULL;

        return result;
    }

    const can_twai_frame_t frame = {
        .identifier =
            CAN_SERVICE_SELF_TEST_IDENTIFIER,

        .data_length =
            CAN_TWAI_CLASSIC_DATA_MAX_LENGTH,

        .extended = false,
        .remote = false,

        .data = {
            0x11U,
            0x22U,
            0x33U,
            0x44U,
            0x55U,
            0x66U,
            0x77U,
            0x88U,
        },

        .timestamp_us = 0U,
    };

    result =
        can_service_transmit(
            &frame,
            timeout_ms
        );

    if (result == ESP_OK) {
        TickType_t timeout_ticks =
            pdMS_TO_TICKS(
                timeout_ms
            );

        if (timeout_ticks == 0U) {
            timeout_ticks = 1U;
        }

        if (xSemaphoreTake(
                s_self_test_received,
                timeout_ticks
            ) != pdTRUE) {

            result = ESP_ERR_TIMEOUT;
        }
    }

    can_twai_driver_info_t info;

    const esp_err_t info_result =
        can_service_get_info(
            &info
        );

    if (info_result == ESP_OK) {
        ESP_LOGD(
            TAG,
            "TWAI self-test diagnostics: "
            "state=%u, TX=%lu, TX success=%lu, "
            "TX failed=%lu, RX=%lu, RX dropped=%lu, "
            "TEC=%u, REC=%u, ACK errors=%lu, "
            "bus errors=%lu",
            (unsigned int)info.state,
            (unsigned long)info.transmitted_frames,
            (unsigned long)info.successful_transmissions,
            (unsigned long)info.failed_transmissions,
            (unsigned long)info.received_frames,
            (unsigned long)info.dropped_rx_frames,
            (unsigned int)info.transmit_error_count,
            (unsigned int)info.receive_error_count,
            (unsigned long)info.acknowledgement_error_count,
            (unsigned long)info.bus_error_count
        );
    }

    const esp_err_t stop_result =
        can_service_stop();

    /*
    * Do not delete the semaphore while the service task may still access
    * the self-test callback.
    */
    if (can_service_is_running()) {
        ESP_LOGE(
            TAG,
            "CAN service remains running after self-test: %s",
            esp_err_to_name(stop_result)
        );

        return stop_result;
    }

    vSemaphoreDelete(
        s_self_test_received
    );

    s_self_test_received = NULL;

    if (stop_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to clean up CAN service after self-test: %s",
            esp_err_to_name(stop_result)
        );

        return stop_result;
    }

    if (result == ESP_OK) {
        ESP_LOGI(
            TAG,
            "TWAI self-test passed at %lu bit/s",
            (unsigned long)bitrate
        );
    } else {
        ESP_LOGW(
            TAG,
            "TWAI self-test failed: %s",
            esp_err_to_name(result)
        );
    }

    return result;
}

esp_err_t can_service_stop(void)
{
    esp_err_t result =
        can_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(
            &s_running
        ) ||
        (s_task == NULL) ||
        (s_task_stopped == NULL)) {

        can_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    bool expected = false;

    if (!atomic_compare_exchange_strong(
            &s_stop_in_progress,
            &expected,
            true
        )) {

        can_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    atomic_store(
        &s_stop_requested,
        true
    );

    /*
     * Do not hold the API mutex while waiting. A receive callback may
     * currently be entering can_service_transmit(). It will acquire the
     * mutex, notice the stop request and return without transmitting.
     */
    can_service_unlock();

    if (xSemaphoreTake(
            s_task_stopped,
            pdMS_TO_TICKS(
                CAN_SERVICE_STOP_TIMEOUT_MS
            )
        ) != pdTRUE) {

        atomic_store(
            &s_stop_in_progress,
            false
        );

        ESP_LOGE(
            TAG,
            "CAN receive task did not stop"
        );

        return ESP_ERR_TIMEOUT;
    }

    result =
        can_service_lock();

    if (result != ESP_OK) {
        atomic_store(
            &s_stop_in_progress,
            false
        );

        return result;
    }

    s_task = NULL;

    esp_err_t first_error = ESP_OK;

    const esp_err_t stop_result =
        can_twai_driver_stop();

    if ((stop_result != ESP_OK) &&
        (stop_result != ESP_ERR_INVALID_STATE)) {

        ESP_LOGW(
            TAG,
            "Failed to stop TWAI driver: %s",
            esp_err_to_name(stop_result)
        );

        first_error = stop_result;
    }

    const esp_err_t deinit_result =
        can_twai_driver_deinit();

    if (deinit_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to deinitialize TWAI driver: %s",
            esp_err_to_name(deinit_result)
        );

        if (first_error == ESP_OK) {
            first_error = deinit_result;
        }
    }

    /*
     * TWAI is completely stopped, so no new ISR confirmations can be
     * produced. Do not invoke application callbacks while holding the
     * service API mutex.
     */
    can_service_unlock();

    can_service_process_tx_confirmations();

    result =
        can_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    can_service_delete_confirmation_queue();

    s_receive_callback = NULL;
    s_receive_context = NULL;

    s_common_receive_callback = NULL;
    s_common_receive_context = NULL;

    memset(
        &s_config,
        0,
        sizeof(s_config)
    );

    atomic_store(
        &s_running,
        false
    );

    if (s_task_stopped != NULL) {
        vSemaphoreDelete(
            s_task_stopped
        );

        s_task_stopped = NULL;
    }

    atomic_store(
        &s_stop_in_progress,
        false
    );

    ESP_LOGI(
        TAG,
        "Primary CAN service stopped"
    );

    can_service_unlock();

    return first_error;
}

static bool can_service_driver_configs_equal(
    const can_twai_driver_config_t *first,
    const can_twai_driver_config_t *second
)
{
    if ((first == NULL) ||
        (second == NULL)) {

        return false;
    }

    return
        (first->bitrate == second->bitrate) &&
        (first->sample_point_permill ==
         second->sample_point_permill) &&
        (first->mode == second->mode) &&
        (first->tx_queue_depth ==
         second->tx_queue_depth) &&
        (first->rx_queue_length ==
         second->rx_queue_length) &&
        (first->transmit_retry_count ==
         second->transmit_retry_count) &&
        (first->acceptance_filter.identifier ==
         second->acceptance_filter.identifier) &&
        (first->acceptance_filter.mask ==
         second->acceptance_filter.mask) &&
        (first->acceptance_filter.extended ==
         second->acceptance_filter.extended);
}

esp_err_t can_service_reconfigure(
    const can_twai_driver_config_t *driver_config
)
{
    if (driver_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        can_service_lock();

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
        ) ||
        (s_task == NULL) ||
        (s_task_stopped == NULL)) {

        can_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    if (can_service_driver_configs_equal(
            &s_config.driver,
            driver_config
        )) {

        can_service_unlock();
        return ESP_OK;
    }

    QueueHandle_t replacement_confirmation_queue =
        NULL;

    const bool confirmation_queue_resize_required =
        driver_config->tx_queue_depth !=
        s_config.driver.tx_queue_depth;

    if (confirmation_queue_resize_required) {
        const UBaseType_t queue_length =
            driver_config->tx_queue_depth > 0U
                ? (UBaseType_t)
                    driver_config->tx_queue_depth
                : 1U;

        replacement_confirmation_queue =
            xQueueCreate(
                queue_length,
                sizeof(can_twai_tx_confirmation_t)
            );

        if (replacement_confirmation_queue == NULL) {
            can_service_unlock();
            return ESP_ERR_NO_MEM;
        }
    }

    /*
     * Use the existing stop-in-progress guard to prevent a concurrent
     * service stop or another reconfiguration.
     */
    bool expected = false;

    if (!atomic_compare_exchange_strong(
            &s_stop_in_progress,
            &expected,
            true
        )) {

        if (replacement_confirmation_queue != NULL) {
            vQueueDelete(
                replacement_confirmation_queue
            );
        }

        can_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * This also blocks new transmissions through
     * can_service_transmit().
     */
    atomic_store(
        &s_stop_requested,
        true
    );

    can_service_unlock();

    /*
     * Wait until can_service_receive_task() has completely returned
     * from can_twai_driver_receive(). Only then may the driver delete
     * and recreate its receive queue.
     */
    if (xSemaphoreTake(
            s_task_stopped,
            pdMS_TO_TICKS(
                CAN_SERVICE_STOP_TIMEOUT_MS
            )
        ) != pdTRUE) {

        atomic_store(
            &s_stop_in_progress,
            false
        );

        ESP_LOGE(
            TAG,
            "CAN receive task did not pause for reconfiguration"
        );

        if (replacement_confirmation_queue != NULL) {
            vQueueDelete(
                replacement_confirmation_queue
            );

            replacement_confirmation_queue =
                NULL;
        }

        /*
         * Keep stop_requested set. The receive task may still be
         * executing the application callback, so resuming it here
         * would be unsafe.
         */
        return ESP_ERR_TIMEOUT;
    }

    result =
        can_service_lock();

    if (result != ESP_OK) {
        atomic_store(
            &s_stop_in_progress,
            false
        );

        if (replacement_confirmation_queue != NULL) {
            vQueueDelete(
                replacement_confirmation_queue
            );
        }

        return result;
    }

    s_task = NULL;

    ESP_LOGI(
        TAG,
        "Reconfiguring primary CAN: "
        "bitrate=%lu, mode=%u, "
        "TX queue=%u, RX queue=%u",
        (unsigned long)driver_config->bitrate,
        (unsigned int)driver_config->mode,
        (unsigned int)driver_config->tx_queue_depth,
        (unsigned int)driver_config->rx_queue_length
    );

    const can_twai_driver_config_t effective_config =
        can_service_prepare_driver_config(
            driver_config
        );

    const esp_err_t reconfigure_result =
        can_twai_driver_reconfigure(
            &effective_config
        );

    /*
     * can_twai_driver_reconfigure() attempts to restore the previous
     * configuration when applying the new one fails. Continue only if
     * either the new or restored driver is running.
     */
    if (!can_twai_driver_is_started()) {
        ESP_LOGE(
            TAG,
            "TWAI driver is unavailable after reconfiguration"
        );

        (void)can_twai_driver_deinit();

        if (replacement_confirmation_queue != NULL) {
            vQueueDelete(
                replacement_confirmation_queue
            );

            replacement_confirmation_queue =
                NULL;
        }

        can_service_delete_confirmation_queue();

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

        s_receive_callback = NULL;
        s_receive_context = NULL;

        s_common_receive_callback = NULL;
        s_common_receive_context = NULL;

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

        can_service_unlock();

        if (reconfigure_result != ESP_OK) {
            return reconfigure_result;
        }

        return ESP_FAIL;
    }

    if ((reconfigure_result == ESP_OK) &&
        (replacement_confirmation_queue != NULL)) {

        /*
         * TWAI reconfiguration can generate final confirmations while
         * stopping the old node. Deliver them using the old queue
         * before replacing it.
         *
         * Do not invoke application callbacks while holding the API
         * mutex.
         */
        can_service_unlock();

        can_service_process_tx_confirmations();

        result =
            can_service_lock();

        if (result != ESP_OK) {
            vQueueDelete(
                replacement_confirmation_queue
            );

            atomic_store(
                &s_stop_in_progress,
                false
            );

            return result;
        }

        QueueHandle_t previous_confirmation_queue =
            s_tx_confirmation_queue;

        s_tx_confirmation_queue =
            replacement_confirmation_queue;

        replacement_confirmation_queue =
            NULL;

        s_confirmation_queue_capacity =
            driver_config->tx_queue_depth > 0U
                ? (uint32_t)driver_config->tx_queue_depth
                : 1U;

        if (previous_confirmation_queue != NULL) {
            vQueueDelete(
                previous_confirmation_queue
            );
        }
    } else if (replacement_confirmation_queue != NULL) {
        /*
         * The new driver configuration failed and the previous one was
         * restored. Retain the queue sized for the previous TX depth.
         */
        vQueueDelete(
            replacement_confirmation_queue
        );

        replacement_confirmation_queue =
            NULL;
    }

    if (reconfigure_result == ESP_OK) {
        s_config.driver =
            *driver_config;
    }

    /*
     * The binary semaphore was consumed above and is ready for the
     * next stop or reconfiguration operation.
     */
    atomic_store(
        &s_stop_requested,
        false
    );

    const BaseType_t task_result =
        xTaskCreate(
            can_service_receive_task,
            "can_service",
            CAN_SERVICE_TASK_STACK_SIZE,
            NULL,
            CAN_SERVICE_TASK_PRIORITY,
            &s_task
        );

    if (task_result != pdPASS) {
        s_task = NULL;

        ESP_LOGE(
            TAG,
            "Failed to restart CAN receive task"
        );

        const esp_err_t stop_result =
            can_twai_driver_stop();

        if ((stop_result != ESP_OK) &&
            (stop_result != ESP_ERR_INVALID_STATE)) {

            ESP_LOGW(
                TAG,
                "Failed to stop TWAI after task error: %s",
                esp_err_to_name(stop_result)
            );
        }

        (void)can_twai_driver_deinit();

        can_service_delete_confirmation_queue();

        if (replacement_confirmation_queue != NULL) {
            vQueueDelete(
                replacement_confirmation_queue
            );

            replacement_confirmation_queue = NULL;
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

        s_receive_callback = NULL;
        s_receive_context = NULL;

        s_common_receive_callback = NULL;
        s_common_receive_context = NULL;

        can_service_unlock();

        return ESP_ERR_NO_MEM;
    }

    atomic_store(
        &s_stop_in_progress,
        false
    );

    if (reconfigure_result == ESP_OK) {
        ESP_LOGI(
            TAG,
            "Primary CAN reconfigured successfully"
        );
    } else {
        ESP_LOGW(
            TAG,
            "New CAN configuration rejected; "
            "previous configuration restored"
        );
    }

    can_service_unlock();

    return reconfigure_result;
}

esp_err_t can_service_transmit(
    const can_twai_frame_t *frame,
    uint32_t timeout_ms
)
{
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        can_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(
            &s_running
        ) ||
        atomic_load(
            &s_stop_requested
        )) {

        can_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    result =
        can_twai_driver_transmit(
            frame,
            timeout_ms
        );

    can_service_unlock();

    return result;
}

esp_err_t can_service_transmit_common(
    const can_frame_t *frame,
    uint32_t timeout_ms
)
{
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    can_twai_frame_t twai_frame;

    const esp_err_t result =
        can_twai_frame_from_common(
            frame,
            &twai_frame
        );

    if (result != ESP_OK) {
        return result;
    }

    return can_service_transmit(
        &twai_frame,
        timeout_ms
    );
}

esp_err_t can_service_transmit_tracked(
    const can_twai_frame_t *frame,
    void *transmission_context,
    uint32_t timeout_ms,
    uint32_t *transmission_id
)
{
    if ((frame == NULL) ||
        (transmission_id == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *transmission_id = 0U;

    esp_err_t result =
        can_service_lock();

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

        can_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    result =
        can_twai_driver_transmit_tracked(
            frame,
            transmission_context,
            timeout_ms,
            transmission_id
        );

    can_service_unlock();

    return result;
}

esp_err_t can_service_transmit_common_tracked(
    const can_frame_t *frame,
    void *transmission_context,
    uint32_t timeout_ms,
    uint32_t *transmission_id
)
{
    if ((frame == NULL) ||
        (transmission_id == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *transmission_id =
        CAN_NATIVE_SEQUENCE_NONE;

    can_twai_frame_t twai_frame;

    const esp_err_t result =
        can_twai_frame_from_common(
            frame,
            &twai_frame
        );

    if (result != ESP_OK) {
        return result;
    }

    return can_service_transmit_tracked(
        &twai_frame,
        transmission_context,
        timeout_ms,
        transmission_id
    );
}

esp_err_t can_service_recover(void)
{
    esp_err_t result =
        can_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(
            &s_running
        ) ||
        atomic_load(
            &s_stop_requested
        )) {

        can_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    result =
        can_twai_driver_recover();

    can_service_unlock();

    return result;
}

esp_err_t can_service_get_info(
    can_twai_driver_info_t *info
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
        can_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(
            &s_running
        )) {

        can_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    result =
        can_twai_driver_get_info(
            info
        );

    can_service_unlock();

    return result;
}

esp_err_t can_service_get_queue_statistics(
    can_service_queue_statistics_t *statistics
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
        can_service_lock();

    if (result != ESP_OK) {
        return result;
    }

    if (!atomic_load(
            &s_running
        )) {

        can_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    if (s_tx_confirmation_queue != NULL) {
        statistics->confirmation_queue_current =
            (uint32_t)uxQueueMessagesWaiting(
                s_tx_confirmation_queue
            );
    }

    can_service_update_confirmation_peak(
        statistics->confirmation_queue_current
    );

    statistics->confirmation_queue_peak =
        (uint32_t)atomic_load_explicit(
            &s_confirmation_queue_peak,
            memory_order_relaxed
        );

    statistics->confirmation_queue_capacity =
        s_confirmation_queue_capacity;

    statistics->dropped_tx_confirmations =
        (uint32_t)atomic_load_explicit(
            &s_dropped_tx_confirmations,
            memory_order_relaxed
        );

    can_service_unlock();

    return ESP_OK;
}

bool can_service_is_running(void)
{
    return atomic_load(
        &s_running
    );
}
