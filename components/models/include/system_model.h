#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define SYSTEM_DEVICE_ID_MAX_LEN       32
#define SYSTEM_FW_VERSION_MAX_LEN      32
#define SYSTEM_SERIAL_MAX_LEN          32
#define SYSTEM_HW_VERSION_MAX_LEN      16
#define SYSTEM_DEVICE_NAME_MAX_LEN     32

typedef struct
{
    char device_id[SYSTEM_DEVICE_ID_MAX_LEN];

    char firmware_version[SYSTEM_FW_VERSION_MAX_LEN];

    char hardware_version[SYSTEM_HW_VERSION_MAX_LEN];

    char serial_number[SYSTEM_SERIAL_MAX_LEN];

    char device_name[SYSTEM_DEVICE_NAME_MAX_LEN];

    uint32_t uptime_sec;

    uint32_t free_heap;

    uint32_t minimum_free_heap;

    uint8_t cpu_usage;

    bool storage_ready;

    bool ota_available;

} system_model_t;

esp_err_t system_model_init(void);

esp_err_t system_model_set(
    const system_model_t *model
);

esp_err_t system_model_get_snapshot(
    system_model_t *out_model
);

esp_err_t system_model_set_storage_ready(
    bool ready
);

esp_err_t system_model_set_update_available(
    bool available
);