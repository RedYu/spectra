/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "uds_ecu_profile.h"

#include <string.h>

#include "isotp_protocol.h"
#include "uds_protocol.h"

static bool uds_ecu_profile_text_valid(
    const char *text,
    size_t capacity,
    bool required
)
{
    const size_t length = strnlen(text, capacity);

    return (length < capacity) &&
           (!required || (length != 0U));
}

static bool uds_ecu_profile_identifier_valid(
    uint32_t identifier,
    bool extended
)
{
    return identifier <=
        (extended
            ? CAN_FRAME_EXTENDED_ID_MAX
            : CAN_FRAME_STANDARD_ID_MAX);
}

static bool uds_ecu_profile_link_length_valid(
    uint8_t length
)
{
    switch (length) {
        case 8U:
        case 12U:
        case 16U:
        case 20U:
        case 24U:
        case 32U:
        case 48U:
        case 64U:
            return true;

        default:
            return false;
    }
}

static bool uds_ecu_profile_routine_valid(
    const uds_ecu_profile_routine_t *routine
)
{
    if (!routine->enabled) {
        return true;
    }

    return (routine->identifier != 0U) &&
           (routine->option_record_length <=
            UDS_ECU_PROFILE_ROUTINE_RECORD_MAX_LENGTH) &&
           (!routine->status_enabled ||
            (routine->pending_value != routine->success_value));
}

void uds_ecu_profile_initialize(
    uds_ecu_profile_t *profile
)
{
    if (profile == NULL) {
        return;
    }

    memset(profile, 0, sizeof(*profile));

    profile->transport.bus = CAN_BUS_PRIMARY;
    profile->transport.transmit_identifier = 0x7E0U;
    profile->transport.receive_identifier = 0x7E8U;
    profile->transport.link_data_length = 8U;
    profile->transport.p2_timeout_ms =
        UDS_ECU_PROFILE_DEFAULT_P2_TIMEOUT_MS;
    profile->transport.p2_star_timeout_ms =
        UDS_ECU_PROFILE_DEFAULT_P2_STAR_TIMEOUT_MS;
    profile->transport.tester_present_interval_ms =
        UDS_ECU_PROFILE_DEFAULT_TESTER_PRESENT_MS;

    profile->programming.session_type =
        UDS_DIAGNOSTIC_SESSION_PROGRAMMING;
    profile->programming.address_length = 4U;
    profile->programming.size_length = 4U;
    profile->programming.routine_poll_interval_ms =
        UDS_ECU_PROFILE_DEFAULT_ROUTINE_POLL_MS;
    profile->programming.maximum_routine_polls =
        UDS_ECU_PROFILE_DEFAULT_ROUTINE_POLLS;
    profile->programming.reset_type = 1U;
    profile->programming.restore_default_session = true;
}

esp_err_t uds_ecu_profile_validate(
    const uds_ecu_profile_t *profile
)
{
    if ((profile == NULL) ||
        !uds_ecu_profile_text_valid(
            profile->name,
            sizeof(profile->name),
            true
        ) ||
        !uds_ecu_profile_text_valid(
            profile->description,
            sizeof(profile->description),
            false
        ) ||
        (profile->transport.bus >= CAN_BUS_COUNT) ||
        !uds_ecu_profile_identifier_valid(
            profile->transport.transmit_identifier,
            profile->transport.extended_identifier
        ) ||
        !uds_ecu_profile_identifier_valid(
            profile->transport.receive_identifier,
            profile->transport.extended_identifier
        ) ||
        !uds_ecu_profile_link_length_valid(
            profile->transport.link_data_length
        ) ||
        (!profile->transport.can_fd &&
         (profile->transport.link_data_length != 8U)) ||
        (!profile->transport.can_fd &&
         profile->transport.bit_rate_switch) ||
        (profile->transport.can_fd &&
         (profile->transport.bus != CAN_BUS_SECONDARY)) ||
        (profile->transport.p2_timeout_ms == 0U) ||
        (profile->transport.p2_timeout_ms > 60000U) ||
        (profile->transport.p2_star_timeout_ms == 0U) ||
        (profile->transport.p2_star_timeout_ms > 60000U) ||
        (profile->transport.tester_present_interval_ms == 0U) ||
        (profile->transport.tester_present_interval_ms > 60000U) ||
        (profile->programming.session_type == 0U) ||
        (profile->programming.session_type > 0x7FU) ||
        ((profile->programming.security_level != 0U) &&
         ((profile->programming.security_level & 1U) == 0U)) ||
        (profile->programming.security_level >
         UDS_SECURITY_ACCESS_LEVEL_MAX) ||
        (profile->programming.address_length == 0U) ||
        (profile->programming.address_length > 8U) ||
        (profile->programming.size_length == 0U) ||
        (profile->programming.size_length > 8U) ||
        !uds_ecu_profile_routine_valid(
            &profile->programming.erase
        ) ||
        !uds_ecu_profile_routine_valid(
            &profile->programming.verify
        ) ||
        (profile->programming.routine_poll_interval_ms == 0U) ||
        (profile->programming.routine_poll_interval_ms > 60000U) ||
        (profile->programming.maximum_routine_polls == 0U) ||
        (profile->programming.maximum_routine_polls > 100000U) ||
        (profile->programming.reset_enabled &&
         ((profile->programming.reset_type == 0U) ||
          (profile->programming.reset_type > 0x7FU)))) {

        return ESP_ERR_INVALID_ARG;
    }

    uint32_t st_min_us = 0U;

    if (isotp_protocol_st_min_to_us(
            profile->transport.st_min,
            &st_min_us
        ) != ESP_OK) {

        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}
