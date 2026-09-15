/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UDS_ECU_PROFILE_NAME_MAX_LENGTH       (64U)
#define UDS_ECU_PROFILE_DESCRIPTION_MAX_LENGTH (128U)
#define UDS_ECU_PROFILE_ROUTINE_RECORD_MAX_LENGTH (64U)

#define UDS_ECU_PROFILE_DEFAULT_P2_TIMEOUT_MS       (1000U)
#define UDS_ECU_PROFILE_DEFAULT_P2_STAR_TIMEOUT_MS  (5000U)
#define UDS_ECU_PROFILE_DEFAULT_TESTER_PRESENT_MS   (2000U)
#define UDS_ECU_PROFILE_DEFAULT_ROUTINE_POLL_MS     (250U)
#define UDS_ECU_PROFILE_DEFAULT_ROUTINE_POLLS       (240U)

typedef struct
{
    can_bus_id_t bus;
    uint32_t transmit_identifier;
    uint32_t receive_identifier;
    bool extended_identifier;
    bool can_fd;
    bool bit_rate_switch;
    uint8_t link_data_length;
    uint8_t block_size;
    uint8_t st_min;
    uint32_t p2_timeout_ms;
    uint32_t p2_star_timeout_ms;
    uint32_t tester_present_interval_ms;

} uds_ecu_profile_transport_t;

typedef struct
{
    bool enabled;
    uint16_t identifier;
    uint8_t option_record[
        UDS_ECU_PROFILE_ROUTINE_RECORD_MAX_LENGTH
    ];
    size_t option_record_length;
    bool status_enabled;
    uint8_t status_offset;
    uint8_t pending_value;
    uint8_t success_value;

} uds_ecu_profile_routine_t;

typedef struct
{
    uint8_t session_type;
    uint8_t security_level;
    uint8_t data_format_identifier;
    uint8_t address_length;
    uint8_t size_length;
    uint64_t default_memory_address;
    uds_ecu_profile_routine_t erase;
    uds_ecu_profile_routine_t verify;
    uint32_t routine_poll_interval_ms;
    uint32_t maximum_routine_polls;
    bool reset_enabled;
    uint8_t reset_type;
    bool restore_default_session;

} uds_ecu_profile_programming_t;

typedef struct
{
    char name[UDS_ECU_PROFILE_NAME_MAX_LENGTH];
    char description[UDS_ECU_PROFILE_DESCRIPTION_MAX_LENGTH];
    uds_ecu_profile_transport_t transport;
    uds_ecu_profile_programming_t programming;

} uds_ecu_profile_t;

/**
 * @brief Initialize an ECU profile with safe diagnostic defaults.
 */
void uds_ecu_profile_initialize(
    uds_ecu_profile_t *profile
);

/**
 * @brief Validate an ECU profile before storing or applying it.
 */
esp_err_t uds_ecu_profile_validate(
    const uds_ecu_profile_t *profile
);

#ifdef __cplusplus
}
#endif
