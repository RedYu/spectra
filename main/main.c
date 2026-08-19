#include <stdbool.h>
#include <stdint.h>
#include <string.h>

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

#define STARTUP_TASK_STACK_SIZE  (6144U)
#define STARTUP_TASK_PRIORITY    (5U)

#define TWAI_STARTUP_SELF_TEST_ENABLED  (1)
#define TWAI_SELF_TEST_TIMEOUT_MS       (500U)

#define CAN_FD_STARTUP_SELF_TEST_ENABLED  (1)
#define CAN_FD_SELF_TEST_TIMEOUT_MS       (500U)

#define CAN_LINK_STARTUP_SELF_TEST_ENABLED  (1)
#define CAN_LINK_TEST_TIMEOUT_MS          (500U)

typedef enum
{
    SERVICE_OPTIONAL = 0,
    SERVICE_REQUIRED

} service_requirement_t;

static const char *TAG = "app_main";

static esp_err_t app_wait_secondary_can_rx(
    uint32_t previous_count,
    uint32_t timeout_ms
)
{
    const TickType_t started_at =
        xTaskGetTickCount();

    TickType_t timeout_ticks =
        pdMS_TO_TICKS(
            timeout_ms
        );

    if (timeout_ticks == 0U) {
        timeout_ticks = 1U;
    }

    while ((xTaskGetTickCount() -
            started_at) < timeout_ticks) {

        can_fd_mcp2518fd_info_t info;

        if ((can_fd_service_get_info(
                 &info
             ) == ESP_OK) &&
            (info.received_frames >
             previous_count)) {

            return ESP_OK;
        }

        vTaskDelay(1U);
    }

    return ESP_ERR_TIMEOUT;
}

static esp_err_t app_wait_primary_can_rx(
    uint32_t previous_count,
    uint32_t timeout_ms
)
{
    const TickType_t started_at =
        xTaskGetTickCount();

    TickType_t timeout_ticks =
        pdMS_TO_TICKS(
            timeout_ms
        );

    if (timeout_ticks == 0U) {
        timeout_ticks = 1U;
    }

    while ((xTaskGetTickCount() -
            started_at) < timeout_ticks) {

        can_twai_driver_info_t info;

        if ((can_service_get_info(
                 &info
             ) == ESP_OK) &&
            (info.received_frames >
             previous_count)) {

            return ESP_OK;
        }

        vTaskDelay(1U);
    }

    return ESP_ERR_TIMEOUT;
}

static esp_err_t app_can_link_self_test(void)
{
    if (!can_service_is_running() ||
        !can_fd_service_is_running()) {

        return ESP_ERR_INVALID_STATE;
    }

    can_twai_driver_info_t primary_before;
    can_fd_mcp2518fd_info_t secondary_before;

    esp_err_t result =
        can_service_get_info(
            &primary_before
        );

    if (result != ESP_OK) {
        return result;
    }

    result =
        can_fd_service_get_info(
            &secondary_before
        );

    if (result != ESP_OK) {
        return result;
    }

    if ((primary_before.mode !=
         CAN_TWAI_MODE_NORMAL) ||
        (secondary_before.mode !=
         CAN_FD_MCP2518FD_MODE_NORMAL) ||
        (primary_before.bitrate !=
         secondary_before.nominal_bitrate)) {

        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Primary TWAI -> secondary MCP2518FD.
     */
    const can_twai_frame_t primary_frame = {
        .identifier = 0x601U,
        .data_length = 8U,
        .extended = false,
        .remote = false,

        .data = {
            0x50U, 0x52U, 0x49U, 0x4DU,
            0x41U, 0x52U, 0x59U, 0x01U,
        },
    };

    result =
        can_service_transmit(
            &primary_frame,
            100U
        );

    if (result != ESP_OK) {
        return result;
    }

    result =
        app_wait_secondary_can_rx(
            secondary_before.received_frames,
            CAN_LINK_TEST_TIMEOUT_MS
        );

    if (result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Primary -> secondary CAN test failed"
        );

        return result;
    }

    /*
     * Secondary MCP2518FD -> primary TWAI.
     */
    can_twai_driver_info_t primary_current;

    result =
        can_service_get_info(
            &primary_current
        );

    if (result != ESP_OK) {
        return result;
    }

    const can_fd_mcp2518fd_frame_t secondary_frame = {
        .identifier = 0x602U,
        .data_length = 8U,

        .data = {
            0x53U, 0x45U, 0x43U, 0x4FU,
            0x4EU, 0x44U, 0x41U, 0x02U,
        },

        .extended = false,
        .remote = false,
        .fd_frame = false,
        .bit_rate_switch = false,
        .error_state_indicator = false,
    };

    result =
        can_fd_service_transmit(
            &secondary_frame,
            100U
        );

    if (result != ESP_OK) {
        return result;
    }

    result =
        app_wait_primary_can_rx(
            primary_current.received_frames,
            CAN_LINK_TEST_TIMEOUT_MS
        );

    if (result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Secondary -> primary CAN test failed"
        );

        return result;
    }

    return ESP_OK;
}

static void log_heap_region(
    const char *name,
    uint32_t capabilities
)
{
    if (name == NULL) {
        return;
    }

    multi_heap_info_t info = {0};

    heap_caps_get_info(
        &info,
        capabilities
    );

    const size_t total =
        info.total_allocated_bytes +
        info.total_free_bytes;

    ESP_LOGI(
        "memory",
        "%s: tot=%u, usd=%u, fr=%u, "
        "min=%u, larg=%u, "
        "alloc_b=%u, free_b=%u",
        name,
        (unsigned int)total,
        (unsigned int)
            info.total_allocated_bytes,
        (unsigned int)
            info.total_free_bytes,
        (unsigned int)
            info.minimum_free_bytes,
        (unsigned int)
            info.largest_free_block,
        (unsigned int)
            info.allocated_blocks,
        (unsigned int)
            info.free_blocks
    );
}

static void log_memory_status(
    const char *stage
)
{
    ESP_LOGI(
        "memory",
        "Memory status: %s",
        stage != NULL
            ? stage
            : "unknown"
    );

    log_heap_region(
        "Internal",
        MALLOC_CAP_INTERNAL |
        MALLOC_CAP_8BIT
    );

    log_heap_region(
        "DMA",
        MALLOC_CAP_DMA |
        MALLOC_CAP_8BIT
    );

    log_heap_region(
        "PSRAM",
        MALLOC_CAP_SPIRAM |
        MALLOC_CAP_8BIT
    );

    log_heap_region(
        "Default",
        MALLOC_CAP_DEFAULT
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

    ESP_LOGI(
        TAG,
        "Starting %s service",
        name
    );

    const esp_err_t result =
        start();

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

    result = settings_service_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Configuration initialization failed: %s",
            esp_err_to_name(result)
        );

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

#if TWAI_STARTUP_SELF_TEST_ENABLED || \
    CAN_FD_STARTUP_SELF_TEST_ENABLED

    /*
     * settings_service_init() already applies the loaded settings and
     * may start both CAN services. Stop them temporarily before running
     * isolated controller self-tests. settings_service_apply() below
     * restores their configured operating modes.
     */
    if (can_service_is_running()) {
        const esp_err_t stop_result =
            can_service_stop();

        if (stop_result != ESP_OK) {
            startup_warning = true;

            ESP_LOGW(
                TAG,
                "Failed to stop primary CAN before self-test: %s",
                esp_err_to_name(stop_result)
            );
        }
    }

    if (can_fd_service_is_running()) {
        const esp_err_t stop_result =
            can_fd_service_stop();

        if (stop_result != ESP_OK) {
            startup_warning = true;

            ESP_LOGW(
                TAG,
                "Failed to stop secondary CAN before self-test: %s",
                esp_err_to_name(stop_result)
            );
        }
    }

#endif

#if TWAI_STARTUP_SELF_TEST_ENABLED

    (void)gui_service_set_boot_progress(
        48U,
        "Testing primary CAN"
    );

    ESP_LOGI(
        TAG,
        "Starting TWAI self-test"
    );

    if (!can_service_is_running()) {
        const esp_err_t twai_test_result =
            can_service_run_self_test(
                CAN_TWAI_BITRATE_500_KBIT,
                TWAI_SELF_TEST_TIMEOUT_MS
            );

        if (twai_test_result != ESP_OK) {
            startup_warning = true;

            ESP_LOGW(
                TAG,
                "TWAI self-test failed: %s",
                esp_err_to_name(twai_test_result)
            );
        } else {
            ESP_LOGI(
                TAG,
                "TWAI self-test passed"
            );
        }
    }
#endif

#if CAN_FD_STARTUP_SELF_TEST_ENABLED

    ESP_LOGI(
        TAG,
        "Starting MCP2518FD service self-test"
    );

    if (!can_fd_service_is_running()) {
        const esp_err_t can_fd_test_result =
            can_fd_service_run_self_test(
                500000U,
                CAN_FD_SELF_TEST_TIMEOUT_MS
            );

        if (can_fd_test_result != ESP_OK) {
            startup_warning = true;

            ESP_LOGW(
                TAG,
                "MCP2518FD service self-test failed: %s",
                esp_err_to_name(can_fd_test_result)
            );
        } else {
            ESP_LOGI(
                TAG,
                "MCP2518FD service self-test passed"
            );
        }
    }

#endif

    (void)gui_service_set_boot_progress(
        50U,
        "Applying settings"
    );

    result = settings_service_apply();

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

#if CAN_LINK_STARTUP_SELF_TEST_ENABLED

    if (settings.can_primary.enabled &&
        settings.can_secondary.enabled &&
        !settings.can_primary.listen_only &&
        !settings.can_secondary.listen_only &&
        (settings.can_primary.bitrate ==
         settings.can_secondary.nominal_bitrate)) {

        ESP_LOGI(
            TAG,
            "Starting bidirectional CAN link test"
        );

        const esp_err_t link_test_result =
            app_can_link_self_test();

        if (link_test_result != ESP_OK) {
            startup_warning = true;

            ESP_LOGW(
                TAG,
                "Bidirectional CAN link test failed: %s",
                esp_err_to_name(link_test_result)
            );
        } else {
            ESP_LOGI(
                TAG,
                "Bidirectional CAN link test passed"
            );
        }

    } else {
        ESP_LOGW(
            TAG,
            "Bidirectional CAN link test skipped: "
            "both interfaces must be enabled in normal mode "
            "with equal nominal bitrates"
        );
    }

#endif

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
