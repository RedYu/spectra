#include "can_service.h"

#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_log.h"

#define CAN_SERVICE_TASK_STACK_SIZE          (4096U)
#define CAN_SERVICE_TASK_PRIORITY            (5U)

#define CAN_SERVICE_RECEIVE_TIMEOUT_MS       (100U)
#define CAN_SERVICE_STOP_TIMEOUT_MS          (2000U)
#define CAN_SERVICE_ERROR_RETRY_DELAY_MS     (20U)

static const char *TAG =
    "can_service";

static TaskHandle_t s_task = NULL;

static SemaphoreHandle_t s_api_mutex = NULL;
static SemaphoreHandle_t s_task_stopped = NULL;

static can_service_receive_cb_t
    s_receive_callback = NULL;

static void *s_receive_context = NULL;

static atomic_bool s_running =
    ATOMIC_VAR_INIT(false);

static atomic_bool s_stop_requested =
    ATOMIC_VAR_INIT(false);

static atomic_bool s_stop_in_progress =
    ATOMIC_VAR_INIT(false);

static can_service_config_t s_config;

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

        can_twai_frame_t frame;

        const esp_err_t result =
            can_twai_driver_receive(
                &frame,
                CAN_SERVICE_RECEIVE_TIMEOUT_MS
            );

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

        can_service_receive_cb_t callback =
            s_receive_callback;

        void *context =
            s_receive_context;

        if (callback != NULL) {
            callback(
                &frame,
                context
            );
        }
    }

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

    result =
        can_twai_driver_init(
            &config->driver
        );

    if (result != ESP_OK) {
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

    atomic_store(
        &s_stop_requested,
        false
    );

    atomic_store(
        &s_stop_in_progress,
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

        s_receive_callback = NULL;
        s_receive_context = NULL;

        (void)can_twai_driver_stop();
        (void)can_twai_driver_deinit();

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

    s_config = *config;

    ESP_LOGI(
        TAG,
        "Primary CAN service started"
    );

    can_service_unlock();

    return ESP_OK;
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

    s_receive_callback = NULL;
    s_receive_context = NULL;

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

esp_err_t can_service_reconfigure(
    const can_twai_driver_config_t *driver_config
)
{
    if (driver_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * Preserve the complete current configuration, including the
     * receive callback and its context.
     */
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

    const can_service_config_t previous_config =
        s_config;

    can_service_config_t new_config =
        previous_config;

    new_config.driver =
        *driver_config;

    can_service_unlock();

    ESP_LOGI(
        TAG,
        "Reconfiguring primary CAN: bitrate=%lu, mode=%u",
        (unsigned long)new_config.driver.bitrate,
        (unsigned int)new_config.driver.mode
    );

    result =
        can_service_stop();

    if (result != ESP_OK) {
        return result;
    }

    result =
        can_service_start(
            &new_config
        );

    if (result == ESP_OK) {
        ESP_LOGI(
            TAG,
            "Primary CAN reconfigured successfully"
        );

        return ESP_OK;
    }

    ESP_LOGE(
        TAG,
        "Failed to apply new CAN configuration: %s",
        esp_err_to_name(result)
    );

    /*
     * Try to restore the previous working configuration.
     */
    const esp_err_t rollback_result =
        can_service_start(
            &previous_config
        );

    if (rollback_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to restore previous CAN configuration: %s",
            esp_err_to_name(rollback_result)
        );

        return rollback_result;
    }

    ESP_LOGW(
        TAG,
        "Previous CAN configuration restored"
    );

    return result;
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

bool can_service_is_running(void)
{
    return atomic_load(
        &s_running
    );
}
