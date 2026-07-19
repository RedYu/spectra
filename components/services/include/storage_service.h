#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

esp_err_t storage_service_init(void);

void storage_service_deinit(void);

bool storage_service_is_mounted(void);

esp_err_t storage_service_read_file(
    const char *path,
    char **out_data,
    size_t *out_size
);
