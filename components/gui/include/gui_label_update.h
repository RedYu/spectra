/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"

/**
 * @brief Update a label only when its text has changed.
 *
 * This function compares the current LVGL label text instead of
 * keeping a separate cache tied to the lifetime of a screen. It must
 * only be called from the GUI task.
 *
 * @param[in] label Label object to update.
 * @param[in] text New null-terminated label text.
 */
static inline void gui_label_set_text_if_changed(
    lv_obj_t *label,
    const char *text
)
{
    const char *current_text =
        lv_label_get_text(
            label
        );

    if (strcmp(
            current_text,
            text
        ) != 0) {

        lv_label_set_text(
            label,
            text
        );
    }
}

/**
 * @brief Format and update a label only when its text has changed.
 *
 * Text that fits in the local buffer is compared with the current
 * label value before updating the LVGL object. Longer formatted text
 * is passed directly to LVGL. This function must only be called from
 * the GUI task.
 *
 * @param[in] label Label object to update.
 * @param[in] format printf-compatible format string.
 */
static inline void gui_label_set_text_fmt_if_changed(
    lv_obj_t *label,
    const char *format,
    ...
)
{
    char text[256];
    va_list arguments;
    va_list copy;

    va_start(arguments, format);
    va_copy(copy, arguments);

    const int length =
        vsnprintf(
            text,
            sizeof(text),
            format,
            arguments
        );

    va_end(arguments);

    if ((length >= 0) &&
        ((size_t)length < sizeof(text))) {

        gui_label_set_text_if_changed(
            label,
            text
        );
    } else {
        /*
         * Preserve LVGL formatting behavior for longer text.
         */
        lv_label_set_text_vfmt(
            label,
            format,
            copy
        );
    }

    va_end(copy);
}
