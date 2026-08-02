#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Export a pending core dump from flash to the SD card.
 *
 * When a valid core dump is present, it is copied to the SD card and
 * erased from flash only after the file has been written, flushed and
 * closed successfully. If the SD card is unavailable, the core dump
 * remains in flash for a later export attempt.
 *
 * @return ESP_OK when no core dump is present or the export completes
 * successfully, ESP_ERR_INVALID_STATE if the SD card is unavailable,
 * otherwise an ESP-IDF error code.
 */
esp_err_t crash_dump_service_export_to_sd(void);

#ifdef __cplusplus
}
#endif
