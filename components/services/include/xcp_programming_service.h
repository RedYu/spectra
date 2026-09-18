/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "xcp_service.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XCP_PROGRAMMING_PATH_MAX_LENGTH (128U)

typedef enum
{
    XCP_PROGRAMMING_IDLE = 0,
    XCP_PROGRAMMING_VALIDATING,
    XCP_PROGRAMMING_CONNECTING,
    XCP_PROGRAMMING_PREPARING,
    XCP_PROGRAMMING_ERASING,
    XCP_PROGRAMMING_TRANSFERRING,
    XCP_PROGRAMMING_VERIFYING,
    XCP_PROGRAMMING_RESETTING,
    XCP_PROGRAMMING_COMPLETE,
    XCP_PROGRAMMING_CANCELLED,
    XCP_PROGRAMMING_ERROR,

} xcp_programming_state_t;

typedef struct
{
    char path[XCP_PROGRAMMING_PATH_MAX_LENGTH];
    xcp_service_session_config_t transport;
    uint64_t binary_address;
    uint8_t connect_mode;
    uint8_t address_extension;
    bool clear_enabled;
    uint8_t clear_mode;
    bool program_format_enabled;
    uint8_t compression_method;
    uint8_t encryption_method;
    uint8_t programming_method;
    uint8_t access_method;
    bool verify_enabled;
    uint8_t verify_mode;
    uint8_t verify_type;
    uint32_t verify_value;
    bool reset_enabled;

} xcp_programming_config_t;

typedef struct
{
    xcp_programming_state_t state;
    xcp_programming_config_t config;
    uint64_t image_size;
    uint64_t confirmed_bytes;
    uint64_t current_address;
    uint32_t confirmed_blocks;
    uint32_t total_blocks;
    bool resume_available;
    bool resumed;
    esp_err_t last_error;

} xcp_programming_info_t;

esp_err_t xcp_programming_service_start(
    const xcp_programming_config_t *config
);

esp_err_t xcp_programming_service_resume(void);

esp_err_t xcp_programming_service_cancel(void);

esp_err_t xcp_programming_service_stop(void);

esp_err_t xcp_programming_service_discard_resume(void);

esp_err_t xcp_programming_service_get_info(
    xcp_programming_info_t *info
);

#ifdef __cplusplus
}
#endif
