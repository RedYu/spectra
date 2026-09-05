/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "settings_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wi-Fi Station credentials stored in NVS.
 *
 * The password must only be kept in memory for the time required to
 * configure the Wi-Fi driver. Clear structures containing credentials
 * after use.
 */
typedef struct
{
    char ssid[
        SETTINGS_WIFI_STA_SSID_MAX_LENGTH
    ];

    char password[
        SETTINGS_WIFI_PASSWORD_MAX_LENGTH
    ];

    char credential_id[
        SETTINGS_WIFI_CREDENTIAL_ID_LENGTH
    ];

} wifi_sta_credentials_t;

/**
 * @brief Initialize the Wi-Fi credentials service.
 *
 * The default NVS flash partition must already be initialized with
 * nvs_flash_init().
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * already initialized, ESP_ERR_NO_MEM if synchronization resources
 * cannot be created, otherwise an ESP-IDF error code.
 */
esp_err_t wifi_credentials_service_init(void);

/**
 * @brief Store new Wi-Fi Station credentials in NVS.
 *
 * Generates a new random credential identifier and atomically writes
 * the SSID, password and identifier to NVS. Existing credentials are
 * replaced only after all new values have been prepared.
 *
 * An empty password selects an open network. Otherwise, the password
 * must contain from 8 to 63 bytes.
 *
 * @param[in] ssid Null-terminated SSID containing from 1 to 32 bytes.
 * @param[in] password Null-terminated Wi-Fi password.
 * @param[out] out_credential_id Receives the generated credential
 * identifier.
 * @param[in] credential_id_size Size of out_credential_id. It must be
 * at least SETTINGS_WIFI_CREDENTIAL_ID_LENGTH bytes.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if an argument or
 * password is invalid, ESP_ERR_INVALID_SIZE if an input or output
 * buffer size is invalid, ESP_ERR_INVALID_STATE if the service is not
 * initialized, ESP_ERR_TIMEOUT if the service lock cannot be acquired,
 * otherwise an ESP-IDF error code.
 */
esp_err_t wifi_credentials_service_set(
    const char *ssid,
    const char *password,
    char *out_credential_id,
    size_t credential_id_size
);

/**
 * @brief Read Wi-Fi Station credentials from NVS.
 *
 * The output structure is cleared before reading. The caller should
 * securely clear it again after applying the credentials.
 *
 * @param[out] credentials Destination credentials structure.
 *
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no complete
 * credential record exists, ESP_ERR_INVALID_ARG if credentials is
 * NULL, ESP_ERR_INVALID_STATE if the service is not initialized,
 * ESP_ERR_TIMEOUT if the service lock cannot be acquired, otherwise an
 * ESP-IDF error code.
 */
esp_err_t wifi_credentials_service_get(
    wifi_sta_credentials_t *credentials
);

/**
 * @brief Remove Wi-Fi Station credentials from NVS.
 *
 * Calling this function when no credentials are stored succeeds.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the service is
 * not initialized, ESP_ERR_TIMEOUT if the service lock cannot be
 * acquired, otherwise an ESP-IDF error code.
 */
esp_err_t wifi_credentials_service_clear(void);

/**
 * @brief Check whether a complete credential record exists in NVS.
 *
 * This function only checks the NVS record. It does not compare it with
 * the SSID or credential identifier stored in the settings model.
 *
 * @param[out] configured Set to true when a complete credential record
 * exists.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if configured is NULL,
 * ESP_ERR_INVALID_STATE if the service is not initialized,
 * ESP_ERR_TIMEOUT if the service lock cannot be acquired, otherwise an
 * ESP-IDF error code.
 */
esp_err_t wifi_credentials_service_get_configured(
    bool *configured
);

#ifdef __cplusplus
}
#endif
