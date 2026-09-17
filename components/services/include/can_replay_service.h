/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_REPLAY_PATH_MAX_LENGTH (128U)

typedef enum
{
    CAN_REPLAY_IDLE = 0,
    CAN_REPLAY_RUNNING,
    CAN_REPLAY_PAUSED,
    CAN_REPLAY_COMPLETE,
    CAN_REPLAY_CANCELLED,
    CAN_REPLAY_ERROR,

} can_replay_state_t;

typedef enum
{
    CAN_REPLAY_LATE_WAIT = 0,
    CAN_REPLAY_LATE_DROP,
    CAN_REPLAY_LATE_STOP,

} can_replay_late_policy_t;

typedef struct
{
    char path[CAN_REPLAY_PATH_MAX_LENGTH];
    bool primary_enabled;
    bool secondary_enabled;
    bool replay_rx_events;
    bool replay_tx_events;
    bool preserve_bus;
    can_bus_id_t target_bus;
    bool identifier_filter_enabled;
    uint32_t identifier_min;
    uint32_t identifier_max;
    bool time_range_enabled;
    uint64_t time_start_us;
    uint64_t time_end_us;
    bool maximum_speed;
    uint32_t speed_numerator;
    uint32_t speed_denominator;
    uint32_t repeat_count;
    uint32_t start_delay_ms;
    uint32_t maximum_lag_ms;
    can_replay_late_policy_t late_policy;
    bool skip_remote_frames;
    bool stop_on_error;

} can_replay_config_t;

typedef struct
{
    can_replay_state_t state;
    can_replay_config_t config;
    uint64_t file_size;
    uint64_t file_position;
    uint64_t replay_time_us;
    uint64_t elapsed_us;
    uint64_t current_lag_us;
    uint64_t maximum_lag_us;
    uint32_t current_repeat;
    uint32_t records_read;
    uint32_t frames_selected;
    uint32_t frames_submitted;
    uint32_t frames_completed;
    uint32_t frames_failed;
    uint32_t frames_dropped;
    uint32_t frames_skipped;
    uint32_t pending_transaction;
    esp_err_t last_error;

} can_replay_info_t;

esp_err_t can_replay_service_init(void);

esp_err_t can_replay_service_start(
    const can_replay_config_t *config
);

esp_err_t can_replay_service_pause(void);

esp_err_t can_replay_service_resume(void);

esp_err_t can_replay_service_stop(void);

esp_err_t can_replay_service_get_info(
    can_replay_info_t *info
);

#ifdef __cplusplus
}
#endif
