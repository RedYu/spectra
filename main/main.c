/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"

#include "board.h"
#include "display_driver.h"
#include "touch_driver.h"
#include "system_model.h"
#include "system_service.h"
#include "power_service.h"
#include "battery_service.h"
#include "buzzer_service.h"
#include "storage_service.h"
#include "storage_sd_service.h"
#include "settings_model.h"
#include "settings_service.h"
#include "wifi_credentials_service.h"
#include "gui_service.h"
#include "logging_service.h"
#include "network_service.h"
#include "usb_network_service.h"
#include "web_service.h"
#include "mdns_service.h"
#include "internet_service.h"
#include "crash_dump_service.h"
#include "can_service.h"
#include "can_fd_service.h"
#include "can_router.h"
#include "can_monitor_service.h"
#include "can_logger_service.h"

#define STARTUP_TASK_STACK_SIZE  (6144U)
#define STARTUP_TASK_PRIORITY    (5U)

typedef enum
{
    SERVICE_OPTIONAL = 0,
    SERVICE_REQUIRED

} service_requirement_t;

typedef struct
{
    size_t internal_free;
    size_t internal_minimum;
    size_t internal_largest;

    size_t dma_free;
    size_t dma_largest;

    size_t psram_free;
    size_t psram_largest;

} memory_snapshot_t;

static const char *TAG = "app_main";

static memory_snapshot_t capture_memory_snapshot(void)
{
    const memory_snapshot_t snapshot = {
        .internal_free =
            heap_caps_get_free_size(
                MALLOC_CAP_INTERNAL |
                MALLOC_CAP_8BIT
            ),

        .internal_minimum =
            heap_caps_get_minimum_free_size(
                MALLOC_CAP_INTERNAL |
                MALLOC_CAP_8BIT
            ),

        .internal_largest =
            heap_caps_get_largest_free_block(
                MALLOC_CAP_INTERNAL |
                MALLOC_CAP_8BIT
            ),

        .dma_free =
            heap_caps_get_free_size(
                MALLOC_CAP_INTERNAL |
                MALLOC_CAP_DMA |
                MALLOC_CAP_8BIT
            ),

        .dma_largest =
            heap_caps_get_largest_free_block(
                MALLOC_CAP_INTERNAL |
                MALLOC_CAP_DMA |
                MALLOC_CAP_8BIT
            ),

        .psram_free =
            heap_caps_get_free_size(
                MALLOC_CAP_SPIRAM |
                MALLOC_CAP_8BIT
            ),

        .psram_largest =
            heap_caps_get_largest_free_block(
                MALLOC_CAP_SPIRAM |
                MALLOC_CAP_8BIT
            ),
    };

    return snapshot;
}

static void log_memory_status(
    const char *stage
)
{
    const memory_snapshot_t snapshot =
        capture_memory_snapshot();

    ESP_LOGI(
        "memory",
        "%s: internal=%u, min=%u, largest=%u, "
        "DMA=%u, DMA largest=%u, "
        "PSRAM=%u, PSRAM largest=%u",
        stage != NULL
            ? stage
            : "unknown",
        (unsigned int)snapshot.internal_free,
        (unsigned int)snapshot.internal_minimum,
        (unsigned int)snapshot.internal_largest,
        (unsigned int)snapshot.dma_free,
        (unsigned int)snapshot.dma_largest,
        (unsigned int)snapshot.psram_free,
        (unsigned int)snapshot.psram_largest
    );
}

static int32_t memory_free_delta(
    size_t before,
    size_t after
)
{
    return
        (int32_t)(
            (int64_t)after -
            (int64_t)before
        );
}

static void log_service_memory_delta(
    const char *service,
    const memory_snapshot_t *before,
    const memory_snapshot_t *after,
    bool started
)
{
    if ((service == NULL) ||
        (before == NULL) ||
        (after == NULL)) {

        return;
    }

    ESP_LOGI(
        "memory",
        "%s %s: internal=%ld, DMA=%ld, PSRAM=%ld, "
        "largest=%u -> %u",
        service,
        started
            ? "start"
            : "failed start",
        (long)memory_free_delta(
            before->internal_free,
            after->internal_free
        ),
        (long)memory_free_delta(
            before->dma_free,
            after->dma_free
        ),
        (long)memory_free_delta(
            before->psram_free,
            after->psram_free
        ),
        (unsigned int)before->internal_largest,
        (unsigned int)after->internal_largest
    );
}

static esp_err_t initialize_nvs(void)
{
    esp_err_t result =
        nvs_flash_init();

    if ((result ==
         ESP_ERR_NVS_NO_FREE_PAGES) ||
        (result ==
         ESP_ERR_NVS_NEW_VERSION_FOUND)) {

        /*
         * The NVS partition cannot be used in its current format.
         * Erase it and create a new compatible NVS structure.
         */
        result = nvs_flash_erase();

        if (result != ESP_OK) {
            return result;
        }

        result = nvs_flash_init();
    }

    return result;
}

static esp_err_t start_service(
    const char *name,
    esp_err_t (*start)(void),
    service_requirement_t requirement
)
{
    if ((name == NULL) ||
        (start == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    const memory_snapshot_t memory_before =
        capture_memory_snapshot();

    ESP_LOGI(
        TAG,
        "Starting %s service",
        name
    );

    const esp_err_t result =
        start();

    /*
     * Record memory even when service initialization fails.
     */
    const memory_snapshot_t memory_after =
        capture_memory_snapshot();

    log_service_memory_delta(
        name,
        &memory_before,
        &memory_after,
        result == ESP_OK
    );

    if (result != ESP_OK) {
        if (requirement ==
            SERVICE_REQUIRED) {

            ESP_LOGE(
                TAG,
                "Failed to start required %s service: %s",
                name,
                esp_err_to_name(result)
            );

        } else {
            ESP_LOGW(
                TAG,
                "Failed to start optional %s service: %s",
                name,
                esp_err_to_name(result)
            );
        }

        return result;
    }

    ESP_LOGI(
        TAG,
        "%s service started",
        name
    );

    return ESP_OK;
}

static void startup_task(
    void *argument
)
{
    (void)argument;

    bool startup_warning = false;

    log_memory_status(
        "Application start"
    );

    /*
     * Initialize the display stack first so the splash screen can
     * report the remaining startup stages.
     */
    esp_err_t result =
        display_driver_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Display initialization failed: %s",
            esp_err_to_name(result)
        );

        vTaskDelete(NULL);
        return;
    }

    /*
     * Initialize the power-management service using the shared board
     * I2C bus. Power monitoring is optional for application startup.
     */
    result =
        power_service_init(
            board_i2c_get_handle()
        );

    if (result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Power-service initialization failed: %s",
            esp_err_to_name(result)
        );

        startup_warning = true;
    } else {
        power_service_snapshot_t power = {0};

        const esp_err_t power_result =
            power_service_get_snapshot(
                &power
            );

        if (power_result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Failed to read power information: %s",
                esp_err_to_name(power_result)
            );

            startup_warning = true;
        } else {
            const axp313a_status_t *power_status =
                &power.status;

            const axp313a_configuration_t
                *power_configuration =
                    &power.configuration;

            ESP_LOGI(
                TAG,
                "AXP313A outputs: "
                "DCDC1=%u, DCDC2=%u, DCDC3=%u, "
                "ALDO1=%u, DLDO1=%u",
                (unsigned int)
                    power_status->dcdc1_enabled,
                (unsigned int)
                    power_status->dcdc2_enabled,
                (unsigned int)
                    power_status->dcdc3_enabled,
                (unsigned int)
                    power_status->aldo1_enabled,
                (unsigned int)
                    power_status->dldo1_enabled
            );

            ESP_LOGI(
                TAG,
                "AXP313A configured voltages: "
                "DCDC1=%u mV, DCDC2=%u mV, "
                "DCDC3=%u mV, ALDO1=%u mV, "
                "DLDO1=%u mV",
                (unsigned int)
                    power_status
                        ->dcdc1_configured_voltage_mv,
                (unsigned int)
                    power_status
                        ->dcdc2_configured_voltage_mv,
                (unsigned int)
                    power_status
                        ->dcdc3_configured_voltage_mv,
                (unsigned int)
                    power_status
                        ->aldo1_configured_voltage_mv,
                (unsigned int)
                    power_status
                        ->dldo1_configured_voltage_mv
            );

            ESP_LOGI(
                TAG,
                "AXP313A modes: "
                "DCDC1_PWM=%u, DCDC2_PWM=%u, "
                "DCDC3_PWM=%u, source=0x%02X, "
                "IRQ=0x%02X",
                (unsigned int)
                    power_status->dcdc1_forced_pwm,
                (unsigned int)
                    power_status->dcdc2_forced_pwm,
                (unsigned int)
                    power_status->dcdc3_forced_pwm,
                (unsigned int)
                    power_status->power_on_source,
                (unsigned int)
                    power_status->irq_status
            );

            ESP_LOGI(
                TAG,
                "AXP313A DCDC configuration: "
                "spread=%u, frequency=%u kHz",
                (unsigned int)
                    power_configuration
                        ->spread_spectrum_enabled,
                (unsigned int)
                    power_configuration
                        ->spread_spectrum_frequency_khz
            );

            ESP_LOGI(
                TAG,
                "AXP313A thermal protection: "
                "IRQ=%u, shutdown=%u, threshold=%u C",
                (unsigned int)
                    power_configuration
                        ->overtemperature_irq_enabled,
                (unsigned int)
                    power_configuration
                        ->overtemperature_shutdown_enabled,
                (unsigned int)
                    power_configuration
                        ->overtemperature_threshold_c
            );

            ESP_LOGI(
                TAG,
                "AXP313A power control: "
                "PWROK_monitor=%u, PWROK_restart=%u, "
                "shutdown_sequence=%u, shutdown_delay=%u, "
                "sleep_wakeup=%u, IRQ_wakeup=%u",
                (unsigned int)
                    power_configuration
                        ->startup_pwrok_monitoring_enabled,
                (unsigned int)
                    power_configuration
                        ->pwrok_low_restart_enabled,
                (unsigned int)
                    power_configuration
                        ->reverse_shutdown_sequence_enabled,
                (unsigned int)
                    power_configuration
                        ->shutdown_delay_enabled,
                (unsigned int)
                    power_configuration
                        ->sleep_wakeup_enabled,
                (unsigned int)
                    power_configuration
                        ->irq_wakeup_enabled
            );

            ESP_LOGI(
                TAG,
                "AXP313A output protection: "
                "DLDO1_OC=%u, DCDC1_UV=%u, "
                "DCDC2_UV=%u, DCDC3_UV=%u, "
                "DCDC_OV=%u",
                (unsigned int)
                    power_configuration
                        ->dldo1_overcurrent_shutdown_enabled,
                (unsigned int)
                    power_configuration
                        ->dcdc1_undervoltage_shutdown_enabled,
                (unsigned int)
                    power_configuration
                        ->dcdc2_undervoltage_shutdown_enabled,
                (unsigned int)
                    power_configuration
                        ->dcdc3_undervoltage_shutdown_enabled,
                (unsigned int)
                    power_configuration
                        ->dcdc_overvoltage_shutdown_enabled
            );
        }
    }

    /*
     * Initialize the touch controller on the same shared I2C bus.
     */
    result =
        touch_driver_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Touch initialization failed: %s",
            esp_err_to_name(result)
        );

        vTaskDelete(NULL);
        return;
    }

    result = storage_service_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Internal storage initialization failed: %s",
            esp_err_to_name(result)
        );

        vTaskDelete(NULL);
        return;
    }

    result = start_service(
        "System",
        system_service_start,
        SERVICE_REQUIRED
    );

    if (result != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    result = start_service(
        "Battery",
        battery_service_start,
        SERVICE_OPTIONAL
    );

    if (result != ESP_OK) {
        startup_warning = true;
    }

    result = start_service(
        "Buzzer",
        buzzer_service_start,
        SERVICE_OPTIONAL
    );

    if (result != ESP_OK) {
        startup_warning = true;
    }

    result = network_service_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize network service: %s",
            esp_err_to_name(result)
        );

        vTaskDelete(NULL);
        return;
    }

    result =
        wifi_credentials_service_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize Wi-Fi credentials service: %s",
            esp_err_to_name(result)
        );

        vTaskDelete(NULL);
        return;
    }

    const can_router_config_t can_router_config = {
        .queue_depth =
            64U,

        .subscriber_capacity =
            8U,

        .pending_tx_capacity =
            32U,
    };

    result =
        can_router_start(
            &can_router_config
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to start CAN router: %s",
            esp_err_to_name(result)
        );

        (void)gui_service_set_boot_progress(
            50U,
            "CAN router unavailable"
        );

        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(
        TAG,
        "CAN router started"
    );

    const can_monitor_service_config_t
        can_monitor_config = {

            .queue_depth =
                16U,

            .history_capacity =
                128U,

            .identifier_capacity =
                128U,
        };

    result =
        can_monitor_service_start(
            &can_monitor_config
        );

    if (result != ESP_OK) {
        startup_warning = true;

        ESP_LOGW(
            TAG,
            "Failed to start CAN monitor: %s",
            esp_err_to_name(result)
        );

        /*
         * Monitoring is optional. CAN routing and both CAN interfaces can
         * continue operating without accumulated monitor statistics.
         */
    } else {
        ESP_LOGI(
            TAG,
            "CAN monitor service started"
        );
    }

    result = settings_service_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Configuration initialization failed: %s",
            esp_err_to_name(result)
        );

        bool can_monitor_stopped = true;

        if (can_monitor_service_is_running()) {
            const esp_err_t monitor_result =
                can_monitor_service_stop();

            if ((monitor_result != ESP_OK) &&
                (monitor_result != ESP_ERR_INVALID_STATE)) {

                can_monitor_stopped = false;

                ESP_LOGW(
                    TAG,
                    "Failed to stop CAN monitor after startup error: %s",
                    esp_err_to_name(monitor_result)
                );
            }
        }

        bool can_sources_stopped = true;

        if (can_service_is_running()) {
            const esp_err_t can_result =
                can_service_stop();

            if ((can_result != ESP_OK) &&
                (can_result != ESP_ERR_INVALID_STATE)) {

                can_sources_stopped = false;

                ESP_LOGW(
                    TAG,
                    "Failed to stop primary CAN after startup error: %s",
                    esp_err_to_name(can_result)
                );
            }
        }

        if (can_fd_service_is_running()) {
            const esp_err_t can_fd_result =
                can_fd_service_stop();

            if ((can_fd_result != ESP_OK) &&
                (can_fd_result != ESP_ERR_INVALID_STATE)) {

                can_sources_stopped = false;

                ESP_LOGW(
                    TAG,
                    "Failed to stop secondary CAN after startup error: %s",
                    esp_err_to_name(can_fd_result)
                );
            }
        }

        if (can_sources_stopped &&
            can_monitor_stopped &&
            can_router_is_running()) {

            const esp_err_t router_result =
                can_router_stop();

            if ((router_result != ESP_OK) &&
                (router_result != ESP_ERR_INVALID_STATE)) {

                ESP_LOGW(
                    TAG,
                    "Failed to stop CAN router after startup error: %s",
                    esp_err_to_name(router_result)
                );
            }
        }

        vTaskDelete(NULL);
        return;
    }

    result = gui_service_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize GUI service: %s",
            esp_err_to_name(result)
        );

        vTaskDelete(NULL);
        return;
    }

    result = start_service(
        "GUI",
        gui_service_start,
        SERVICE_REQUIRED
    );

    if (result != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    /*
     * Give the GUI task an opportunity to create the splash screen.
     */
    vTaskDelay(
        pdMS_TO_TICKS(250U)
    );

    (void)gui_service_set_boot_progress(
        35U,
        "Configuration loaded"
    );

    app_settings_t settings;

    result = settings_model_get(
        &settings
    );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to read application settings: %s",
            esp_err_to_name(result)
        );

        vTaskDelete(NULL);
        return;
    }

    (void)gui_service_set_boot_progress(
        40U,
        "Initializing SD card"
    );

    result = start_service(
        "Storage SD card",
        storage_sd_service_start,
        SERVICE_OPTIONAL
    );

    if (result != ESP_OK) {
        startup_warning = true;

        (void)gui_service_set_boot_progress(
            45U,
            "SD card service unavailable"
        );

    } else {
        storage_sd_state_t sd_state =
            STORAGE_SD_STATE_UNAVAILABLE;

        result = storage_sd_service_get_state(
            &sd_state
        );

        if ((result != ESP_OK) ||
            (sd_state != STORAGE_SD_STATE_MOUNTED)) {

            startup_warning = true;

            (void)gui_service_set_boot_progress(
                45U,
                "SD card unavailable"
            );

        } else {
            const esp_err_t dump_result =
                crash_dump_service_export_to_sd();

            if (dump_result != ESP_OK) {
                startup_warning = true;

                ESP_LOGW(
                    TAG,
                    "Failed to export crash dump: %s",
                    esp_err_to_name(dump_result)
                );

                (void)gui_service_set_boot_progress(
                    45U,
                    "Crash dump export failed"
                );

            } else {
                (void)gui_service_set_boot_progress(
                    45U,
                    "SD card ready"
                );
            }
        }
    }

    (void)gui_service_set_boot_progress(
        48U,
        "Starting CAN logger"
    );

    result = start_service(
        "CAN logger",
        can_logger_service_start,
        SERVICE_OPTIONAL
    );

    if (result != ESP_OK) {
        startup_warning = true;

        (void)gui_service_set_boot_progress(
            48U,
            "CAN logger unavailable"
        );
    } else {
        (void)gui_service_set_boot_progress(
            48U,
            "CAN logger ready"
        );
    }

    (void)gui_service_set_boot_progress(
        50U,
        "Applying settings"
    );

    log_memory_status(
        "Before applying settings"
    );

    result =
        settings_service_apply();

    log_memory_status(
        result == ESP_OK
            ? "After applying settings"
            : "After failed settings application"
    );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to apply settings: %s",
            esp_err_to_name(result)
        );

        (void)gui_service_set_boot_progress(
            50U,
            "Settings application failed"
        );

        vTaskDelete(NULL);
        return;
    }

    can_router_statistics_t router_statistics;

    const esp_err_t router_statistics_result =
        can_router_get_statistics(
            &router_statistics
        );

    if (router_statistics_result == ESP_OK) {
        ESP_LOGI(
            TAG,
            "CAN router ready: queue=%lu, subscribers=%lu, "
            "pending TX=%lu",
            (unsigned long)
                router_statistics.queue_capacity,
            (unsigned long)
                router_statistics.subscriber_capacity,
            (unsigned long)
                router_statistics.pending_tx_capacity
        );
    }

    if (can_monitor_service_is_running()) {
        can_monitor_service_statistics_t
            monitor_statistics;

        const esp_err_t monitor_result =
            can_monitor_service_get_statistics(
                &monitor_statistics
            );

        if (monitor_result == ESP_OK) {
            ESP_LOGI(
                TAG,
                "CAN monitor ready: queue=%lu, history=%lu, "
                "identifiers=%lu",
                (unsigned long)
                    monitor_statistics.input_queue_capacity,
                (unsigned long)
                    monitor_statistics.history_capacity,
                (unsigned long)
                    monitor_statistics.identifier_capacity
            );
        }
    }

    if (settings.can_primary.enabled) {
        const bool primary_can_running =
            can_service_is_running();

        (void)gui_service_set_boot_progress(
            70U,
            primary_can_running
                ? "Primary CAN ready"
                : "Primary CAN unavailable"
        );

        if (!primary_can_running) {
            startup_warning = true;

            ESP_LOGW(
                TAG,
                "Primary CAN is enabled but not running"
            );
        }
    } else {
        (void)gui_service_set_boot_progress(
            70U,
            "Primary CAN disabled"
        );
    }

    if (settings.can_secondary.enabled) {
        const bool secondary_can_running =
            can_fd_service_is_running();

        (void)gui_service_set_boot_progress(
            72U,
            secondary_can_running
                ? "Secondary CAN ready"
                : "Secondary CAN unavailable"
        );

        if (!secondary_can_running) {
            startup_warning = true;

            ESP_LOGW(
                TAG,
                "Secondary CAN is enabled but not running"
            );

        } else {
            can_fd_mcp2518fd_info_t can_fd_info;

            const esp_err_t info_result =
                can_fd_service_get_info(
                    &can_fd_info
                );

            if (info_result != ESP_OK) {
                startup_warning = true;

                ESP_LOGW(
                    TAG,
                    "Failed to read secondary CAN information: %s",
                    esp_err_to_name(info_result)
                );

            } else {
                ESP_LOGI(
                    TAG,
                    "Secondary CAN ready: "
                    "nominal=%lu, data=%lu, "
                    "FD=%s, BRS=%s, mode=%u",
                    (unsigned long)
                        can_fd_info.nominal_bitrate,
                    (unsigned long)
                        can_fd_info.data_bitrate,
                    can_fd_info.fd_enabled
                        ? "enabled"
                        : "disabled",
                    can_fd_info.brs_enabled
                        ? "enabled"
                        : "disabled",
                    (unsigned int)
                        can_fd_info.mode
                );
            }
        }

    } else {
        (void)gui_service_set_boot_progress(
            72U,
            "Secondary CAN disabled"
        );
    }

    (void)gui_service_set_boot_progress(
        75U,
        settings.usb_rndis.enabled
            ? "Starting USB network"
            : "USB network disabled"
    );

    if (settings.usb_rndis.enabled) {
        result =
            usb_network_service_init();

        if (result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Failed to initialize USB network service: %s",
                esp_err_to_name(result)
            );

            startup_warning = true;

            (void)gui_service_set_boot_progress(
                85U,
                "USB network unavailable"
            );

        } else {
            (void)gui_service_set_boot_progress(
                85U,
                "USB network ready"
            );
        }
    } else {
        ESP_LOGI(
            TAG,
            "USB RNDIS is disabled in settings"
        );

        (void)gui_service_set_boot_progress(
            85U,
            "USB network disabled"
        );
    }

    (void)gui_service_set_boot_progress(
        90U,
        "Starting web interface"
    );

    result = web_service_start();

    if (result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to start web service: %s",
            esp_err_to_name(result)
        );

        startup_warning = true;

        (void)gui_service_set_boot_progress(
            95U,
            "Web interface unavailable"
        );

    } else {
        (void)gui_service_set_boot_progress(
            95U,
            "Web interface ready"
        );
    }

    result = start_service(
        "mDNS",
        mdns_service_start,
        SERVICE_OPTIONAL
    );

    if (result != ESP_OK) {
        startup_warning = true;

        ESP_LOGW(
            TAG,
            "Local mDNS discovery is unavailable: %s",
            esp_err_to_name(result)
        );
    }

    (void)gui_service_set_boot_progress(
        97U,
        "Starting connectivity monitor"
    );

    result = start_service(
        "Internet connectivity",
        internet_service_start,
        SERVICE_OPTIONAL
    );

    if (result != ESP_OK) {
        startup_warning = true;

        (void)gui_service_set_boot_progress(
            97U,
            "Connectivity monitor unavailable"
        );
    } else {
        (void)gui_service_set_boot_progress(
            97U,
            "Connectivity monitor ready"
        );
    }

    battery_service_info_t battery_info;

    const esp_err_t battery_info_result =
        battery_service_get_info(
            &battery_info
        );

    if ((battery_info_result == ESP_OK) &&
        battery_info.measurement_valid) {

        ESP_LOGI(
            TAG,
            "Battery: voltage=%u mV, level=%u%%, present=%s",
            (unsigned int)battery_info.voltage_mv,
            (unsigned int)battery_info.level_percent,
            battery_info.battery_present
                ? "yes"
                : "no"
        );
    }

    log_memory_status(
        "Startup complete"
    );

    (void)gui_service_set_boot_progress(
        100U,
        startup_warning
            ? "Ready with warnings"
            : "Ready"
    );

    if (startup_warning) {
        ESP_LOGW(
            TAG,
            "Application startup completed with warnings"
        );

    } else {
        ESP_LOGI(
            TAG,
            "Application startup completed"
        );
    }

    if (buzzer_service_is_running()) {
        const esp_err_t buzzer_result =
            buzzer_service_play(
                startup_warning
                    ? BUZZER_SIGNAL_WARNING
                    : BUZZER_SIGNAL_STARTUP
            );

        if (buzzer_result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Failed to queue startup buzzer signal: %s",
                esp_err_to_name(buzzer_result)
            );
        }
    }

    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(
        initialize_nvs()
    );

    ESP_LOGI(
        TAG,
        "NVS initialized"
    );

    ESP_ERROR_CHECK(
        logging_service_init()
    );

    ESP_LOGI(
        TAG,
        "Logging service initialized"
    );

    ESP_ERROR_CHECK(board_init());

    ESP_ERROR_CHECK(system_model_init());

    ESP_LOGI(TAG, "Application starting");

    const BaseType_t task_created =
        xTaskCreate(
            startup_task,
            "startup_task",
            STARTUP_TASK_STACK_SIZE,
            NULL,
            STARTUP_TASK_PRIORITY,
            NULL
        );

    if (task_created != pdPASS) {
        ESP_LOGE(
            TAG,
            "Failed to create startup task"
        );

        return;
    }

    ESP_LOGI(
        TAG,
        "Startup task created"
    );
}
