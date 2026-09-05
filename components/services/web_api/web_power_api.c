/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "web_power_api.h"

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"
#include "esp_log.h"

#include "battery_service.h"
#include "power_service.h"
#include "web_api_common.h"

static const char *TAG =
    "web_power_api";

static bool web_power_api_add_outputs(
    cJSON *response,
    const axp313a_status_t *status
)
{
    if ((response == NULL) ||
        (status == NULL)) {

        return false;
    }

    cJSON *outputs =
        cJSON_AddObjectToObject(
            response,
            "outputs"
        );

    if (outputs == NULL) {
        return false;
    }

    return
        (cJSON_AddBoolToObject(
            outputs,
            "dcdc1",
            status->dcdc1_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            outputs,
            "dcdc2",
            status->dcdc2_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            outputs,
            "dcdc3",
            status->dcdc3_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            outputs,
            "aldo1",
            status->aldo1_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            outputs,
            "dldo1",
            status->dldo1_enabled
        ) != NULL);
}

static bool web_power_api_add_voltages(
    cJSON *response,
    const axp313a_status_t *status
)
{
    if ((response == NULL) ||
        (status == NULL)) {

        return false;
    }

    cJSON *voltages =
        cJSON_AddObjectToObject(
            response,
            "configured_voltages_mv"
        );

    if (voltages == NULL) {
        return false;
    }

    return
        (cJSON_AddNumberToObject(
            voltages,
            "dcdc1",
            status->dcdc1_configured_voltage_mv
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            voltages,
            "dcdc2",
            status->dcdc2_configured_voltage_mv
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            voltages,
            "dcdc3",
            status->dcdc3_configured_voltage_mv
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            voltages,
            "aldo1",
            status->aldo1_configured_voltage_mv
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            voltages,
            "dldo1",
            status->dldo1_configured_voltage_mv
        ) != NULL);
}

static bool web_power_api_add_dcdc(
    cJSON *response,
    const axp313a_status_t *status,
    const axp313a_configuration_t *configuration
)
{
    if ((response == NULL) ||
        (status == NULL) ||
        (configuration == NULL)) {

        return false;
    }

    cJSON *dcdc =
        cJSON_AddObjectToObject(
            response,
            "dcdc"
        );

    if (dcdc == NULL) {
        return false;
    }

    return
        (cJSON_AddBoolToObject(
            dcdc,
            "dcdc1_forced_pwm",
            status->dcdc1_forced_pwm
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            dcdc,
            "dcdc2_forced_pwm",
            status->dcdc2_forced_pwm
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            dcdc,
            "dcdc3_forced_pwm",
            status->dcdc3_forced_pwm
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            dcdc,
            "spread_spectrum_enabled",
            configuration->spread_spectrum_enabled
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            dcdc,
            "spread_spectrum_frequency_khz",
            configuration->spread_spectrum_frequency_khz
        ) != NULL);
}

static bool web_power_api_add_interrupts(
    cJSON *response,
    const axp313a_status_t *status,
    const axp313a_configuration_t *configuration
)
{
    if ((response == NULL) ||
        (status == NULL) ||
        (configuration == NULL)) {

        return false;
    }

    cJSON *interrupts =
        cJSON_AddObjectToObject(
            response,
            "interrupts"
        );

    if (interrupts == NULL) {
        return false;
    }

    cJSON *overtemperature =
        cJSON_AddObjectToObject(
            interrupts,
            "overtemperature"
        );

    cJSON *dcdc2_undervoltage =
        cJSON_AddObjectToObject(
            interrupts,
            "dcdc2_undervoltage"
        );

    cJSON *dcdc3_undervoltage =
        cJSON_AddObjectToObject(
            interrupts,
            "dcdc3_undervoltage"
        );

    if ((overtemperature == NULL) ||
        (dcdc2_undervoltage == NULL) ||
        (dcdc3_undervoltage == NULL)) {

        return false;
    }

    return
        (cJSON_AddNumberToObject(
            interrupts,
            "status_raw",
            status->irq_status
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            overtemperature,
            "active",
            status->overtemperature_irq
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            overtemperature,
            "enabled",
            configuration->overtemperature_irq_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            dcdc2_undervoltage,
            "active",
            status->dcdc2_undervoltage_irq
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            dcdc2_undervoltage,
            "enabled",
            configuration->dcdc2_undervoltage_irq_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            dcdc3_undervoltage,
            "active",
            status->dcdc3_undervoltage_irq
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            dcdc3_undervoltage,
            "enabled",
            configuration->dcdc3_undervoltage_irq_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            interrupts,
            "pwron_rising_enabled",
            configuration->pwron_rising_irq_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            interrupts,
            "pwron_falling_enabled",
            configuration->pwron_falling_irq_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            interrupts,
            "pwron_short_press_enabled",
            configuration->pwron_short_press_irq_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            interrupts,
            "pwron_long_press_enabled",
            configuration->pwron_long_press_irq_enabled
        ) != NULL);
}

static bool web_power_api_add_protection(
    cJSON *response,
    const axp313a_configuration_t *configuration
)
{
    if ((response == NULL) ||
        (configuration == NULL)) {

        return false;
    }

    cJSON *protection =
        cJSON_AddObjectToObject(
            response,
            "protection"
        );

    if (protection == NULL) {
        return false;
    }

    return
        (cJSON_AddBoolToObject(
            protection,
            "startup_pwrok_monitoring",
            configuration
                ->startup_pwrok_monitoring_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            protection,
            "pwrok_low_restart",
            configuration->pwrok_low_restart_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            protection,
            "overtemperature_shutdown",
            configuration
                ->overtemperature_shutdown_enabled
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            protection,
            "overtemperature_threshold_c",
            configuration->overtemperature_threshold_c
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            protection,
            "dldo1_overcurrent_shutdown",
            configuration
                ->dldo1_overcurrent_shutdown_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            protection,
            "dcdc1_undervoltage_shutdown",
            configuration
                ->dcdc1_undervoltage_shutdown_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            protection,
            "dcdc2_undervoltage_shutdown",
            configuration
                ->dcdc2_undervoltage_shutdown_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            protection,
            "dcdc3_undervoltage_shutdown",
            configuration
                ->dcdc3_undervoltage_shutdown_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            protection,
            "dcdc_overvoltage_shutdown",
            configuration
                ->dcdc_overvoltage_shutdown_enabled
        ) != NULL);
}

static bool web_power_api_add_shutdown(
    cJSON *response,
    const axp313a_configuration_t *configuration
)
{
    if ((response == NULL) ||
        (configuration == NULL)) {

        return false;
    }

    cJSON *shutdown =
        cJSON_AddObjectToObject(
            response,
            "shutdown"
        );

    if (shutdown == NULL) {
        return false;
    }

    return
        (cJSON_AddBoolToObject(
            shutdown,
            "reverse_sequence",
            configuration
                ->reverse_shutdown_sequence_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            shutdown,
            "pwrok_delay",
            configuration->shutdown_delay_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            shutdown,
            "long_press_enabled",
            configuration->long_press_shutdown_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            shutdown,
            "restart_after_long_press",
            configuration
                ->restart_after_long_press_shutdown_enabled
        ) != NULL);
}

static bool web_power_api_add_wakeup(
    cJSON *response,
    const axp313a_configuration_t *configuration
)
{
    if ((response == NULL) ||
        (configuration == NULL)) {

        return false;
    }

    cJSON *wakeup =
        cJSON_AddObjectToObject(
            response,
            "wakeup"
        );

    if (wakeup == NULL) {
        return false;
    }

    return
        (cJSON_AddBoolToObject(
            wakeup,
            "sleep_wakeup_enabled",
            configuration->sleep_wakeup_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            wakeup,
            "irq_wakeup_enabled",
            configuration->irq_wakeup_enabled
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            wakeup,
            "lower_pwrok",
            configuration->lower_pwrok_during_wakeup
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            wakeup,
            "preserve_voltage_settings",
            configuration
                ->preserve_voltage_settings_during_wakeup
        ) != NULL);
}

static bool web_power_api_add_raw_registers(
    cJSON *response,
    const axp313a_configuration_t *configuration
)
{
    if ((response == NULL) ||
        (configuration == NULL)) {

        return false;
    }

    cJSON *raw =
        cJSON_AddObjectToObject(
            response,
            "raw_registers"
        );

    if (raw == NULL) {
        return false;
    }

    return
        (cJSON_AddNumberToObject(
            raw,
            "dcdc_control",
            configuration->dcdc_control_raw
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            raw,
            "power_control",
            configuration->power_control_raw
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            raw,
            "shutdown_control",
            configuration->shutdown_control_raw
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            raw,
            "wakeup_control",
            configuration->wakeup_control_raw
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            raw,
            "output_monitor",
            configuration->output_monitor_raw
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            raw,
            "irq_control",
            configuration->irq_control_raw
        ) != NULL);
}

static bool web_power_api_add_battery(
    cJSON *response,
    const battery_service_info_t *battery,
    bool service_running
)
{
    if ((response == NULL) ||
        (battery == NULL)) {

        return false;
    }

    cJSON *battery_json =
        cJSON_AddObjectToObject(
            response,
            "battery"
        );

    if (battery_json == NULL) {
        return false;
    }

    return
        (cJSON_AddStringToObject(
            battery_json,
            "charger",
            "ETA6003"
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            battery_json,
            "service_running",
            service_running
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            battery_json,
            "measurement_valid",
            battery->measurement_valid
        ) != NULL) &&
        (cJSON_AddBoolToObject(
            battery_json,
            "present",
            battery->battery_present
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            battery_json,
            "voltage_mv",
            battery->voltage_mv
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            battery_json,
            "level_percent",
            battery->level_percent
        ) != NULL) &&
        (cJSON_AddNumberToObject(
            battery_json,
            "last_update_ms",
            (double)battery->last_update_ms
        ) != NULL);
}

static esp_err_t web_power_api_get_handler(
    httpd_req_t *request
)
{
    if (request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    power_service_snapshot_t power = {0};

    const esp_err_t power_result =
        power_service_get_snapshot(
            &power
        );

    if (power_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to get power snapshot: %s",
            esp_err_to_name(power_result)
        );

        return web_api_send_message(
            request,
            "503 Service Unavailable",
            false,
            "Power service is not available"
        );
    }

    battery_service_info_t battery = {0};

    const esp_err_t battery_result =
        battery_service_get_info(
            &battery
        );

    const bool battery_service_running =
        battery_service_is_running();

    if ((battery_result != ESP_OK) &&
        (battery_result != ESP_ERR_INVALID_STATE)) {

        ESP_LOGW(
            TAG,
            "Failed to get battery information: %s",
            esp_err_to_name(battery_result)
        );
    }

    cJSON *response =
        cJSON_CreateObject();

    if (response == NULL) {
        return web_api_send_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to create power response"
        );
    }

    bool valid =
        (cJSON_AddStringToObject(
            response,
            "controller",
            "AXP313A"
        ) != NULL);

    valid = valid &&
        (cJSON_AddNumberToObject(
            response,
            "power_on_source",
            power.status.power_on_source
        ) != NULL);

    valid = valid &&
        web_power_api_add_outputs(
            response,
            &power.status
        );

    valid = valid &&
        web_power_api_add_voltages(
            response,
            &power.status
        );

    valid = valid &&
        web_power_api_add_dcdc(
            response,
            &power.status,
            &power.configuration
        );

    valid = valid &&
        web_power_api_add_interrupts(
            response,
            &power.status,
            &power.configuration
        );

    valid = valid &&
        web_power_api_add_protection(
            response,
            &power.configuration
        );

    valid = valid &&
        web_power_api_add_shutdown(
            response,
            &power.configuration
        );

    valid = valid &&
        web_power_api_add_wakeup(
            response,
            &power.configuration
        );

    valid = valid &&
        web_power_api_add_battery(
            response,
            &battery,
            battery_service_running
        );

    valid = valid &&
        web_power_api_add_raw_registers(
            response,
            &power.configuration
        );

    if (!valid) {
        cJSON_Delete(response);

        return web_api_send_message(
            request,
            "500 Internal Server Error",
            false,
            "Failed to create power response"
        );
    }

    const esp_err_t result =
        web_api_send_json(
            request,
            response
        );

    cJSON_Delete(response);

    return result;
}

esp_err_t web_power_api_register(
    httpd_handle_t server
)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    static const httpd_uri_t uri = {
        .uri = "/api/power",
        .method = HTTP_GET,
        .handler =
            web_power_api_get_handler,
        .user_ctx = NULL,
    };

    const esp_err_t result =
        httpd_register_uri_handler(
            server,
            &uri
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to register GET /api/power: %s",
            esp_err_to_name(result)
        );
    }

    return result;
}
