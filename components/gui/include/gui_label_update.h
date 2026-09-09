/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"

/*
 * Call only from the GUI task. Compare the current label text instead
 * of keeping a separate cache tied to the lifetime of a screen.
 */
static inline void gui_label_set_text_if_changed(
    lv_obj_t *label,
    const char *text
)
{
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(
            label,
            text
        );
    }
}

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
