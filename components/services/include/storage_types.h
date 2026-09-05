/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_FILE_NAME_MAX_LENGTH  (256U)

/**
 * @brief Filesystem entry returned by a storage service.
 */
typedef struct
{
    /**
     * Entry name relative to the directory being listed.
     */
    char name[STORAGE_FILE_NAME_MAX_LENGTH];

    /**
     * File size in bytes. This value is zero for directories.
     */
    size_t size;

    /**
     * True when the entry represents a directory.
     */
    bool is_directory;

} storage_file_entry_t;

#ifdef __cplusplus
}
#endif
