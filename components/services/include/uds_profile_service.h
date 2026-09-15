/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#include "uds_ecu_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UDS_PROFILE_DIRECTORY             "/config/uds/profiles"
#define UDS_PROFILE_FILE_NAME_MAX_LENGTH  (96U)
#define UDS_PROFILE_FILE_MAX_SIZE         (16384U)

typedef struct
{
    char file_name[UDS_PROFILE_FILE_NAME_MAX_LENGTH];
    char name[UDS_ECU_PROFILE_NAME_MAX_LENGTH];
    char description[UDS_ECU_PROFILE_DESCRIPTION_MAX_LENGTH];

} uds_profile_summary_t;

/**
 * @brief Create the UDS profile directory when it does not exist.
 */
esp_err_t uds_profile_service_initialize(void);

/**
 * @brief Decode and validate an ECU profile from JSON text.
 */
esp_err_t uds_profile_service_decode_json(
    const char *json,
    uds_ecu_profile_t *profile
);

/**
 * @brief Encode a validated profile as allocated JSON text.
 *
 * The caller releases the returned buffer with free().
 */
esp_err_t uds_profile_service_encode_json(
    const uds_ecu_profile_t *profile,
    char **json
);

/**
 * @brief Load and validate raw profile JSON from the SD card.
 *
 * The caller releases the returned PSRAM buffer with heap_caps_free().
 */
esp_err_t uds_profile_service_load_json(
    const char *file_name,
    char **json
);

/**
 * @brief Validate and store raw profile JSON.
 */
esp_err_t uds_profile_service_save_json(
    const char *file_name,
    const char *json
);

/**
 * @brief Load and validate one JSON ECU profile from the SD card.
 */
esp_err_t uds_profile_service_load(
    const char *file_name,
    uds_ecu_profile_t *profile
);

/**
 * @brief Validate and store one JSON ECU profile through a temporary file.
 */
esp_err_t uds_profile_service_save(
    const char *file_name,
    const uds_ecu_profile_t *profile
);

/**
 * @brief Remove one stored ECU profile.
 */
esp_err_t uds_profile_service_remove(
    const char *file_name
);

/**
 * @brief List stored profiles and load their display metadata.
 */
esp_err_t uds_profile_service_list(
    size_t offset,
    uds_profile_summary_t *profiles,
    size_t capacity,
    size_t *count,
    bool *has_more
);

#ifdef __cplusplus
}
#endif
