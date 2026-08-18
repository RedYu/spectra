#include "shutdown_service.h"

#include <stdatomic.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"

#include "internet_service.h"
#include "logging_service.h"
#include "mdns_service.h"
#include "storage_sd_service.h"
#include "usb_network_service.h"
#include "web_service.h"
#include "wifi_service.h"
#include "system_service.h"
#include "battery_service.h"
#include "buzzer_service.h"
#include "can_service.h"
#include "can_fd_service.h"
#include "settings_service.h"

#define SHUTDOWN_SERVICE_TASK_STACK_SIZE  (4096U)
#define SHUTDOWN_SERVICE_TASK_PRIORITY    (5U)

#define SHUTDOWN_SERVICE_BUZZER_WAIT_MS   (700U)

static const char *TAG =
    "shutdown_service";

static atomic_bool s_restart_scheduled =
    ATOMIC_VAR_INIT(false);

static void shutdown_service_task(
    void *argument
)
{
    const uint32_t delay_ms =
        (uint32_t)(uintptr_t)argument;

    if (delay_ms > 0U) {
        vTaskDelay(
            pdMS_TO_TICKS(
                delay_ms
            )
        );
    }

    ESP_LOGI(
        TAG,
        "Graceful restart started"
    );

    /*
     * Discard pending UI feedback and allow the shutdown signal to
     * finish before application services begin terminating.
     */
    if (buzzer_service_is_running()) {
        const esp_err_t cancel_result =
            buzzer_service_cancel();

        if ((cancel_result != ESP_OK) &&
            (cancel_result != ESP_ERR_INVALID_STATE)) {

            ESP_LOGW(
                TAG,
                "Failed to cancel pending buzzer signals: %s",
                esp_err_to_name(cancel_result)
            );
        }

        const esp_err_t buzzer_play_result =
            buzzer_service_play(
                BUZZER_SIGNAL_SHUTDOWN
            );

        if (buzzer_play_result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Failed to queue shutdown signal: %s",
                esp_err_to_name(buzzer_play_result)
            );
        } else{
            /*
            * Allow the asynchronous shutdown signal to finish before the
            * buzzer service is stopped.
            */
            vTaskDelay(
                pdMS_TO_TICKS(SHUTDOWN_SERVICE_BUZZER_WAIT_MS)
            );
        }
    }

    /*
     * Stop network consumers before shutting down the network interfaces.
     */
    internet_service_stop();
    mdns_service_stop();

    /*
     * Stop accepting new HTTP requests and wait until the HTTP server
     * task releases its sockets.
     */
    const esp_err_t web_result =
        web_service_stop();

    if ((web_result != ESP_OK) &&
        (web_result != ESP_ERR_INVALID_STATE)) {

        ESP_LOGW(
            TAG,
            "Failed to stop web service: %s",
            esp_err_to_name(web_result)
        );
    }

    const esp_err_t settings_result =
        settings_service_save();

    if (settings_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to save settings before restart: %s",
            esp_err_to_name(settings_result)
        );
    }

    /*
     * Stop CAN reception and abort pending transmissions after the web
     * interface has stopped accepting CAN-related requests.
     */
    if (can_service_is_running()) {
        const esp_err_t can_result =
            can_service_stop();

        if ((can_result != ESP_OK) &&
            (can_result != ESP_ERR_INVALID_STATE)) {

            ESP_LOGW(
                TAG,
                "Failed to stop primary CAN service: %s",
                esp_err_to_name(can_result)
            );
        }
    }

    if (can_fd_service_is_running()) {
        const esp_err_t can_fd_result =
            can_fd_service_stop();

        if (can_fd_result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Failed to stop CAN FD service: %s",
                esp_err_to_name(can_fd_result)
            );
        }
    }

    /*
     * Disconnect SoftAP clients and Station, then stop Wi-Fi.
     */
    const esp_err_t wifi_result =
        wifi_service_stop();

    if ((wifi_result != ESP_OK) &&
        (wifi_result != ESP_ERR_INVALID_STATE)) {

        ESP_LOGW(
            TAG,
            "Failed to stop Wi-Fi service: %s",
            esp_err_to_name(wifi_result)
        );
    }

    /*
     * Stop USB networking without affecting unrelated USB functions.
     */
    const esp_err_t usb_result =
        usb_network_service_stop();

    if ((usb_result != ESP_OK) &&
        (usb_result != ESP_ERR_INVALID_STATE)) {

        ESP_LOGW(
            TAG,
            "Failed to stop USB network service: %s",
            esp_err_to_name(usb_result)
        );
    }

    /*
     * Stop periodic runtime and temperature updates.
     */
    system_service_stop();

    /*
     * Stop battery measurements and release ADC resources after all
     * potential battery-information consumers have been stopped.
     */
    const esp_err_t battery_result =
        battery_service_stop();

    if ((battery_result != ESP_OK) &&
        (battery_result != ESP_ERR_INVALID_STATE)) {

        ESP_LOGW(
            TAG,
            "Failed to stop battery service: %s",
            esp_err_to_name(battery_result)
        );
    }

    /*
     * The shutdown signal has already finished, so the buzzer can now be
     * stopped and its LEDC resources released.
     */
    const esp_err_t buzzer_result =
        buzzer_service_stop();

    if ((buzzer_result != ESP_OK) &&
        (buzzer_result != ESP_ERR_INVALID_STATE)) {

        ESP_LOGW(
            TAG,
            "Failed to stop buzzer service: %s",
            esp_err_to_name(buzzer_result)
        );
    }

    /*
     * Drain queued log messages, flush and close the log file.
     */
    const esp_err_t logging_result =
        logging_service_prepare_shutdown();

    if ((logging_result != ESP_OK) &&
        (logging_result != ESP_ERR_INVALID_STATE)) {

        ESP_LOGW(
            TAG,
            "Failed to prepare logging shutdown: %s",
            esp_err_to_name(logging_result)
        );
    }

    /*
     * Block new SD operations and unmount the filesystem.
     *
     * This function calls logging_service_prepare_shutdown() again,
     * but that operation is intentionally idempotent.
     */
    const esp_err_t storage_result =
        storage_sd_service_prepare_shutdown();

    if ((storage_result != ESP_OK) &&
        (storage_result != ESP_ERR_INVALID_STATE)) {

        ESP_LOGW(
            TAG,
            "Failed to prepare SD shutdown: %s",
            esp_err_to_name(storage_result)
        );
    }

    /*
     * File logging is already closed, so this message is written only
     * to UART.
     */
    ESP_LOGI(
        TAG,
        "Graceful restart completed"
    );

    esp_restart();
}

esp_err_t shutdown_service_schedule_restart(
    uint32_t delay_ms
)
{
    bool expected = false;

    if (!atomic_compare_exchange_strong(
            &s_restart_scheduled,
            &expected,
            true
        )) {

        return ESP_ERR_INVALID_STATE;
    }

    const BaseType_t result =
        xTaskCreate(
            shutdown_service_task,
            "shutdown_service",
            SHUTDOWN_SERVICE_TASK_STACK_SIZE,
            (void *)(uintptr_t)delay_ms,
            SHUTDOWN_SERVICE_TASK_PRIORITY,
            NULL
        );

    if (result != pdPASS) {
        atomic_store(
            &s_restart_scheduled,
            false
        );

        ESP_LOGE(
            TAG,
            "Failed to create shutdown task"
        );

        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "Graceful restart scheduled in %lu ms",
        (unsigned long)delay_ms
    );

    return ESP_OK;
}

bool shutdown_service_is_restart_scheduled(void)
{
    return atomic_load(
        &s_restart_scheduled
    );
}
