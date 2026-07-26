#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SD_CARD_MOUNT_POINT "/sdcard"

esp_err_t sd_card_driver_init(void);

esp_err_t sd_card_driver_mount(void);

esp_err_t sd_card_driver_unmount(void);

esp_err_t sd_card_driver_check(void);

bool sd_card_driver_is_mounted(void);

#ifdef __cplusplus
}
#endif
