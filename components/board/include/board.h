#pragma once

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t board_init(void);

esp_err_t board_spi_init(void);

bool board_spi_is_initialized(void);

spi_host_device_t board_spi_get_host(void);

bool board_spi_lock(
    TickType_t timeout
);

void board_spi_unlock(void);

void board_spi_unlock_from_isr(
    BaseType_t *higher_priority_task_woken
);

#ifdef __cplusplus
}
#endif

